param(
    [Parameter(Mandatory = $true)][string]$Scpm,
    [Parameter(Mandatory = $true)][string]$FakeScp,
    [Parameter(Mandatory = $true)][string]$BuildDir
)

$ErrorActionPreference = 'Stop'
$testRoot = Join-Path $BuildDir ("scp-integration-" + [guid]::NewGuid().ToString('N'))
$resolvedBuild = [IO.Path]::GetFullPath($BuildDir).TrimEnd('\') + '\'
$resolvedTest = [IO.Path]::GetFullPath($testRoot)
if (-not $resolvedTest.StartsWith($resolvedBuild, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe integration test directory: $resolvedTest"
}

$credentialId = 'scp-integration-password'
$credentialTarget = "wtssh/$credentialId"
$testPassword = 'ScpmIntegration42!'
$oldDataDir = $env:WTSSH_DATA_DIR
$oldScpPath = $env:WTSCP_SCP_PATH
$oldCapture = $env:WTSSH_TEST_CAPTURE
$oldAskpassMode = $env:WTSCP_ASKPASS_MODE
$oldCredentialId = $env:WTSSH_CREDENTIAL_ID

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

try {
    New-Item -ItemType Directory -Path $testRoot | Out-Null
    $env:WTSSH_DATA_DIR = $testRoot
    $env:WTSCP_SCP_PATH = $FakeScp
    $capture = Join-Path $testRoot 'scp-capture.txt'
    $env:WTSSH_TEST_CAPTURE = $capture

    $backendOutput = & $Scpm --credential-backend
    Assert-True ($LASTEXITCODE -eq 0) 'The selected SCP credential backend is unavailable.'
    Assert-True (($backendOutput -join "`n") -match '^windows-credential-manager \(available\)$') 'Unexpected SCP credential backend.'

    $localFile = Join-Path $testRoot 'local file.txt'
    [IO.File]::WriteAllText($localFile, 'scp integration payload', [Text.UTF8Encoding]::new($false))
    $secondLocalFile = Join-Path $testRoot 'second local file.bin'
    [IO.File]::WriteAllText($secondLocalFile, 'second scp payload', [Text.UTF8Encoding]::new($false))
    $keyLine = "key-host`tscp.example&echo-owned`talice`t2222`tC:\\Key Files\\id_ed25519`tSCP key test`tkey-id`tkey`tx`tlocalhost:7.0`t127.0.0.1:9000:internal:80"
    [IO.File]::WriteAllText((Join-Path $testRoot 'hosts.db'), "# wtssh-v3`n$keyLine`n", [Text.UTF8Encoding]::new($false))

    & $Scpm --send key-host $localFile $secondLocalFile '/remote path/' | Out-Null
    Assert-True ($LASTEXITCODE -eq 0) 'Key-based fake SCP launch failed.'
    $captured = [IO.File]::ReadAllLines($capture)
    Assert-True ($captured -contains 'ARG=-P') 'SCP did not receive its uppercase port option.'
    Assert-True ($captured -contains 'ARG=2222') 'SCP did not receive the configured port.'
    Assert-True ($captured -contains 'ARG=C:\Key Files\id_ed25519') 'The spaced key path was not preserved as one argument.'
    Assert-True ($captured -contains "ARG=$localFile") 'The spaced local file path was not preserved as one argument.'
    Assert-True ($captured -contains "ARG=$secondLocalFile") 'The second local file was not passed as a separate argument.'
    Assert-True ($captured -contains 'ARG=alice@scp.example&echo-owned:/remote path/') 'The remote target was not preserved as one argument.'
    Assert-True (-not ($captured -contains 'ARG=-X')) 'SCP incorrectly inherited X11 forwarding.'
    Assert-True (-not ($captured -contains 'ARG=-L')) 'SCP incorrectly inherited SSH local forwarding.'

    & cmdkey.exe "/generic:$credentialTarget" '/user:alice' "/pass:$testPassword" | Out-Null
    Assert-True ($LASTEXITCODE -eq 0) 'Unable to create the temporary SCP credential.'
    $passwordLine = "password-host`tscp.example`talice`t22`t`tSCP password test`t$credentialId`tpassword`toff`t`t"
    [IO.File]::WriteAllText((Join-Path $testRoot 'hosts.db'), "# wtssh-v3`n$passwordLine`n", [Text.UTF8Encoding]::new($false))

    & $Scpm --send password-host $localFile '~/upload.txt' | Out-Null
    Assert-True ($LASTEXITCODE -eq 0) 'Saved-password fake SCP launch failed.'
    $captured = [IO.File]::ReadAllLines($capture)
    Assert-True ($captured -contains 'ARG=PreferredAuthentications=password') 'SCP password authentication was not constrained.'
    Assert-True ($captured -contains 'ENV_SSH_ASKPASS_REQUIRE=force') 'SCP SSH_ASKPASS_REQUIRE was not set.'
    Assert-True ($captured -contains 'ENV_WTSCP_ASKPASS_MODE=1') 'SCP askpass mode was not set.'
    Assert-True ($captured -contains "ENV_WTSSH_CREDENTIAL_ID=$credentialId") 'The shared credential ID was not passed.'
    Assert-True (-not (($captured -join "`n").Contains($testPassword))) 'The SCP password leaked into arguments or environment variables.'

    $env:WTSCP_ASKPASS_MODE = '1'
    $env:WTSSH_CREDENTIAL_ID = $credentialId
    $askpassOutput = & $Scpm 'alice@scp.example password:'
    Assert-True ($LASTEXITCODE -eq 0) 'SCP askpass failed to read the shared credential.'
    Assert-True (($askpassOutput -join "`n") -eq $testPassword) 'SCP askpass returned an unexpected value.'
    $env:WTSCP_ASKPASS_MODE = $oldAskpassMode

    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $Scpm --send password-host (Join-Path $testRoot 'missing.txt') '~/missing.txt' 2>$null | Out-Null
    $missingExitCode = $LASTEXITCODE
    $ErrorActionPreference = $savedErrorActionPreference
    Assert-True ($missingExitCode -ne 0) 'SCP accepted a missing local file.'

    Write-Output 'SCP integration tests passed.'
}
finally {
    & cmdkey.exe "/delete:$credentialTarget" 2>$null | Out-Null
    $env:WTSSH_DATA_DIR = $oldDataDir
    $env:WTSCP_SCP_PATH = $oldScpPath
    $env:WTSSH_TEST_CAPTURE = $oldCapture
    $env:WTSCP_ASKPASS_MODE = $oldAskpassMode
    $env:WTSSH_CREDENTIAL_ID = $oldCredentialId
    if (Test-Path -LiteralPath $resolvedTest) {
        if (-not $resolvedTest.StartsWith($resolvedBuild, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unsafe path: $resolvedTest"
        }
        Remove-Item -LiteralPath $resolvedTest -Recurse -Force
    }
}
