[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',

    [ValidateSet('auto', 'native', 'windows', 'none')]
    [string]$CredentialBackend = 'auto',

    [string]$BuildDirectory,
    [string]$Generator = 'Ninja',
    [switch]$Clean,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repoRoot ("build/windows-" + $Configuration.ToLowerInvariant())
} elseif (-not [IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $repoRoot $BuildDirectory
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)

if ($Clean -and (Test-Path -LiteralPath $BuildDirectory)) {
    $allowedRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build')).TrimEnd('\') + '\'
    if (-not $BuildDirectory.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean a directory outside '$allowedRoot': $BuildDirectory"
    }
    Remove-Item -LiteralPath $BuildDirectory -Recurse -Force
}

$buildTesting = if ($SkipTests) { 'OFF' } else { 'ON' }
$configureArgs = @(
    '-S', $repoRoot,
    '-B', $BuildDirectory,
    '-G', $Generator,
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DWTSSH_CREDENTIAL_BACKEND=$CredentialBackend",
    "-DBUILD_TESTING=$buildTesting"
)

Write-Host "Configuring Windows build ($Configuration, backend=$CredentialBackend)..."
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host 'Building...'
& cmake --build $BuildDirectory --config $Configuration
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not $SkipTests) {
    Write-Host 'Running tests...'
    & ctest --test-dir $BuildDirectory --build-config $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$singleConfigSsh = Join-Path $BuildDirectory 'wtssh.exe'
$multiConfigSsh = Join-Path (Join-Path $BuildDirectory $Configuration) 'wtssh.exe'
$sshBinary = if (Test-Path -LiteralPath $singleConfigSsh) { $singleConfigSsh } else { $multiConfigSsh }
$singleConfigScp = Join-Path $BuildDirectory 'scpm.exe'
$multiConfigScp = Join-Path (Join-Path $BuildDirectory $Configuration) 'scpm.exe'
$scpBinary = if (Test-Path -LiteralPath $singleConfigScp) { $singleConfigScp } else { $multiConfigScp }
Write-Host "SSH manager built: $sshBinary"
Write-Host "SCP manager built: $scpBinary"
