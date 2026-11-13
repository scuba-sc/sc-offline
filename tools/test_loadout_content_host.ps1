param(
    [Parameter(Mandatory=$true)][string]$SdkArchive,
    [Parameter(Mandatory=$true)][string]$CoreSourceDir,
    [Parameter(Mandatory=$true)][string]$CMakeExe,
    [Parameter(Mandatory=$true)][string]$GeneratorInstance
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$expected = '5b88cc99fe7fa254458aaf6c5bdf3a3318b20611c8fe664a172e0ae210145be4'
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $SdkArchive).Hash.ToLowerInvariant() -ne $expected) { throw 'SDK archive SHA-256 mismatch' }
$coreHead = (& git -C $CoreSourceDir rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $coreHead -ne 'e2dc4ac933b5565417fb067735ddc12104c8030d') { throw 'Core source commit mismatch' }
if ((& git -C $CoreSourceDir status --porcelain)) { throw 'Core source must be clean' }
$run = Join-Path ([IO.Path]::GetTempPath()) ('loadout-content-host-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
Write-Host "Isolated proof directory: $run"
Expand-Archive -LiteralPath $SdkArchive -DestinationPath (Join-Path $run 'sdk')
$include = Join-Path $run 'sdk/sco-sdk-2.4.1-dev.3/include'
$moduleBuild = Join-Path $run 'module-build'
$hostBuild = Join-Path $run 'host-build'
$module = Join-Path $moduleBuild 'test-plugins/loadout/loadout.dll'
$ctest = Join-Path (Split-Path -Parent $CMakeExe) 'ctest.exe'
function Run-Checked([string]$program, [string[]]$arguments) {
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "$program failed with exit code $LASTEXITCODE" }
}
$generator = @('-G', 'Visual Studio 17 2022', '-A', 'x64', "-DCMAKE_GENERATOR_INSTANCE=$GeneratorInstance")
Run-Checked $CMakeExe (@('-S', (Join-Path $repo 'plugins/loadout'), '-B', $moduleBuild) + $generator + @("-DSCO_SDK_INCLUDE_DIR=$include"))
Run-Checked $CMakeExe @('--build', $moduleBuild, '--config', 'Release', '--target', 'sc-offline-loadout', '--parallel')
if (-not (Test-Path -LiteralPath $module -PathType Leaf)) { throw 'Loadout module was not produced' }
Run-Checked $CMakeExe (@('-S', $repo, '-B', $hostBuild) + $generator + @("-DSCO_HOST_SOURCE_DIR=$CoreSourceDir", '-DSCO_LOADOUT_CONTENT_HOST_TEST=ON', "-DSCO_LOADOUT_TEST_MODULE=$module", "-DSCO_LOADOUT_TEST_ROOT=$(Join-Path $run 'fixture')"))
Run-Checked $CMakeExe @('--build', $hostBuild, '--config', 'Release', '--target', 'loadout_content_host_test', '--parallel')
Run-Checked $ctest @('--test-dir', $hostBuild, '-C', 'Release', '-R', '^loadout-content-real-core-host$', '--output-on-failure')
