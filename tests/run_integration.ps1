param(
    [Parameter(Mandatory = $true)][string]$Wtssh,
    [Parameter(Mandatory = $true)][string]$FakeSsh,
    [Parameter(Mandatory = $true)][string]$BuildDir
)

$ErrorActionPreference = 'Stop'
$testRoot = Join-Path $BuildDir ("integration-" + [guid]::NewGuid().ToString('N'))
$resolvedBuild = [IO.Path]::GetFullPath($BuildDir).TrimEnd('\') + '\'
$resolvedTest = [IO.Path]::GetFullPath($testRoot)
if (-not $resolvedTest.StartsWith($resolvedBuild, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe integration test directory: $resolvedTest"
}

$credentialId = 'integration-test-password'
$credentialTarget = "wtssh/$credentialId"
$testPassword = 'WtsshIntegration42!'
$oldDataDir = $env:WTSSH_DATA_DIR
$oldSshPath = $env:WTSSH_SSH_PATH
$oldCapture = $env:WTSSH_TEST_CAPTURE
$oldDisplay = $env:DISPLAY
$oldAskpassMode = $env:WTSSH_ASKPASS_MODE
$oldCredentialId = $env:WTSSH_CREDENTIAL_ID

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

try {
    New-Item -ItemType Directory -Path $testRoot | Out-Null
    $env:WTSSH_DATA_DIR = $testRoot

    $backendOutput = & $Wtssh --credential-backend
    Assert-True ($LASTEXITCODE -eq 0) 'The selected credential backend is unavailable.'
    Assert-True (($backendOutput -join "`n") -match '^windows-credential-manager \(available\)$') 'Unexpected Windows credential backend.'

    $legacyLine = "legacy`tlegacy.example`tolduser`t2200`t`told format"
    [IO.File]::WriteAllText((Join-Path $testRoot 'hosts.db'), $legacyLine + "`n", [Text.UTF8Encoding]::new($false))
    $listOutput = & $Wtssh --list
    Assert-True ($LASTEXITCODE -eq 0) 'Legacy list command failed.'
    Assert-True (($listOutput -join "`n") -match 'legacy') 'Legacy record was not loaded.'
    $migrated = [IO.File]::ReadAllLines((Join-Path $testRoot 'hosts.db'))
    Assert-True ($migrated[0] -eq '# wtssh-v2') 'Legacy database did not receive the v2 header.'
    Assert-True (($migrated[1] -split "`t", -1).Count -eq 10) 'Legacy database did not migrate to ten columns.'

    $env:WTSSH_SSH_PATH = $FakeSsh
    $env:DISPLAY = 'parent-display:99.0'
    $capture = Join-Path $testRoot 'x11-capture.txt'
    $env:WTSSH_TEST_CAPTURE = $capture
    $x11Line = "xhost`tx.example&echo-owned`talice`t2222`tC:\\Key Files\\id_ed25519`tX test`tx-id`tauto`tx`tlocalhost:7.0"
    [IO.File]::WriteAllText((Join-Path $testRoot 'hosts.db'), "# wtssh-v2`n$x11Line`n", [Text.UTF8Encoding]::new($false))
    & $Wtssh --connect xhost | Out-Null
    Assert-True ($LASTEXITCODE -eq 0) 'X11 fake SSH launch failed.'
    $captured = [IO.File]::ReadAllLines($capture)
    Assert-True ($captured -contains 'ARG=-X') 'The -X option was not passed.'
    Assert-True ($captured -contains 'ARG=C:\Key Files\id_ed25519') 'The spaced key path was not preserved as one argument.'
    Assert-True ($captured -contains 'ARG=alice@x.example&echo-owned') 'The SSH target or shell-metacharacter handling was incorrect.'
    Assert-True ($captured -contains 'ENV_DISPLAY=localhost:7.0') 'The per-host DISPLAY did not override the child environment.'
    Assert-True ($env:DISPLAY -eq 'parent-display:99.0') 'The parent DISPLAY was unexpectedly changed.'

    $trustedLine = "trusted`ty.example`tbob`t22`t`tY test`ty-id`tauto`ty`tlocalhost:8.0"
    [IO.File]::WriteAllText((Join-Path $testRoot 'hosts.db'), "# wtssh-v2`n$trustedLine`n", [Text.UTF8Encoding]::new($false))
    & $Wtssh --connect trusted | Out-Null
    Assert-True ($LASTEXITCODE -eq 0) 'Trusted X11 fake SSH launch failed.'
    $captured = [IO.File]::ReadAllLines($capture)
    Assert-True ($captured -contains 'ARG=-Y') 'The -Y option was not passed.'
    Assert-True ($captured -contains 'ENV_DISPLAY=localhost:8.0') 'Trusted X11 DISPLAY was not passed.'

    & cmdkey.exe "/generic:$credentialTarget" '/user:alice' "/pass:$testPassword" | Out-Null
    Assert-True ($LASTEXITCODE -eq 0) 'Unable to create temporary Credential Manager entry.'
    $passwordLine = "passwordhost`tp.example`talice`t22`t`tPassword test`t$credentialId`tpassword`toff`t"
    [IO.File]::WriteAllText((Join-Path $testRoot 'hosts.db'), "# wtssh-v2`n$passwordLine`n", [Text.UTF8Encoding]::new($false))
    & $Wtssh --connect passwordhost | Out-Null
    Assert-True ($LASTEXITCODE -eq 0) 'Saved-password fake SSH launch failed.'
    $captured = [IO.File]::ReadAllLines($capture)
    Assert-True ($captured -contains 'ARG=PreferredAuthentications=password') 'Password authentication was not constrained.'
    Assert-True ($captured -contains 'ARG=NumberOfPasswordPrompts=1') 'Password prompt count was not constrained.'
    Assert-True ($captured -contains 'ENV_SSH_ASKPASS_REQUIRE=force') 'SSH_ASKPASS_REQUIRE was not set.'
    Assert-True ($captured -contains 'ENV_WTSSH_ASKPASS_MODE=1') 'Askpass mode was not set.'
    Assert-True ($captured -contains "ENV_WTSSH_CREDENTIAL_ID=$credentialId") 'Credential reference was not passed.'
    Assert-True (-not (($captured -join "`n").Contains($testPassword))) 'The password leaked into SSH arguments or captured environment.'

    $env:WTSSH_ASKPASS_MODE = '1'
    $env:WTSSH_CREDENTIAL_ID = $credentialId
    $askpassOutput = & $Wtssh 'alice@p.example password:'
    Assert-True ($LASTEXITCODE -eq 0) 'Askpass helper failed to read Credential Manager.'
    Assert-True (($askpassOutput -join "`n") -eq $testPassword) 'Askpass returned an unexpected value.'

    Write-Output 'Integration tests passed.'
}
finally {
    & cmdkey.exe "/delete:$credentialTarget" 2>$null | Out-Null
    $env:WTSSH_DATA_DIR = $oldDataDir
    $env:WTSSH_SSH_PATH = $oldSshPath
    $env:WTSSH_TEST_CAPTURE = $oldCapture
    $env:DISPLAY = $oldDisplay
    $env:WTSSH_ASKPASS_MODE = $oldAskpassMode
    $env:WTSSH_CREDENTIAL_ID = $oldCredentialId
    if (Test-Path -LiteralPath $resolvedTest) {
        if (-not $resolvedTest.StartsWith($resolvedBuild, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unsafe path: $resolvedTest"
        }
        Remove-Item -LiteralPath $resolvedTest -Recurse -Force
    }
}
