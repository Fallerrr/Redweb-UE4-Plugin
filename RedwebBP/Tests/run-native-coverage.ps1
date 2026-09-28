param(
    [string]$UnrealEditorCmd = $env:UE4_EDITOR_CMD
)

$ErrorActionPreference = 'Stop'
$pluginRoot = Split-Path $PSScriptRoot -Parent
$testProject = Join-Path $PSScriptRoot 'RedwebBPTestHost\RedwebBPTestHost.uproject'
if ([string]::IsNullOrWhiteSpace($UnrealEditorCmd) -or -not (Test-Path -LiteralPath $UnrealEditorCmd)) {
    throw 'Set UE4_EDITOR_CMD to the UE4.27 UnrealEditor-Cmd.exe path to collect native coverage.'
}

$engineRoot = [System.IO.Path]::GetFullPath((Join-Path (Split-Path $UnrealEditorCmd -Parent) '..\..'))
$buildBat = Join-Path $engineRoot 'Build\BatchFiles\Build.bat'
if (-not (Test-Path -LiteralPath $buildBat)) { throw "Could not locate UnrealBuildTool beside $UnrealEditorCmd." }

$previousCoverageMode = $env:REDWEBBP_NATIVE_COVERAGE
try {
    $env:REDWEBBP_NATIVE_COVERAGE = '1'
    & $buildBat UE4Editor Win64 Development "-Project=$testProject" -NoHotReloadFromIDE -WaitMutex
    if ($LASTEXITCODE -ne 0) { throw 'The RedwebBP Unreal test host did not build.' }
} finally {
    $env:REDWEBBP_NATIVE_COVERAGE = $previousCoverageMode
}

$vswhere = Join-Path ([System.Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Could not locate Visual Studio Installer (vswhere.exe).' }
$vsInstall = & $vswhere -latest -products '*' -property installationPath
if ([string]::IsNullOrWhiteSpace($vsInstall)) { throw 'No Visual Studio installation was found for native coverage.' }

$coverageConsole = Join-Path $vsInstall 'Common7\IDE\Extensions\Microsoft\CodeCoverage.Console\Microsoft.CodeCoverage.Console.exe'
if (-not (Test-Path -LiteralPath $coverageConsole)) { throw 'Install the Visual Studio Code Coverage Console component to measure native C++ coverage.' }
$msvcRoot = Join-Path $vsInstall 'VC\Tools\MSVC'
$msvc = Get-ChildItem -LiteralPath $msvcRoot -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $msvc) { throw 'Could not locate the MSVC linker required for native coverage.' }
$linker = Join-Path $msvc.FullName 'bin\Hostx64\x64\link.exe'
if (-not (Test-Path -LiteralPath $linker)) { throw "Could not locate the x64 MSVC linker under $($msvc.FullName)." }

$windowsKitsRoot = Join-Path ([System.Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits\10\bin'
$sdkBin = Get-ChildItem -LiteralPath $windowsKitsRoot -Directory |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+(?:\.\d+)?$' } |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { $candidate = Join-Path $_.FullName 'x64'; if (Test-Path (Join-Path $candidate 'mt.exe')) { $candidate; break } } |
    Select-Object -First 1
if (-not $sdkBin) { throw 'Could not locate the Windows SDK manifest tool required by the MSVC linker.' }

$pluginBinary = Join-Path $pluginRoot 'Binaries\Win64\UE4Editor-RedwebBP.dll'
$linkResponse = Join-Path $pluginRoot 'Intermediate\Build\Win64\UE4Editor\Development\RedwebBP\UE4Editor-RedwebBP.dll.response'
if (-not (Test-Path -LiteralPath $pluginBinary) -or -not (Test-Path -LiteralPath $linkResponse)) {
    throw 'UnrealBuildTool did not produce the RedwebBP editor module and its link response file.'
}

$previousPath = $env:PATH
try {
    $env:PATH = "$sdkBin;$env:PATH"
    & $linker "@$linkResponse" /PROFILE
    if ($LASTEXITCODE -ne 0) { throw 'The RedwebBP module could not be linked with native coverage profiling enabled.' }
} finally {
    $env:PATH = $previousPath
}

$resultsRoot = Join-Path $PSScriptRoot 'results'
$reportRoot = Join-Path $resultsRoot ('native-coverage-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $reportRoot -Force | Out-Null
$coverageReport = Join-Path $reportRoot 'redwebbp.cobertura.xml'
$settingsTemplate = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'native-coverage.runsettings') -Raw
$escapedPluginRoot = [System.Security.SecurityElement]::Escape($pluginRoot)
$settings = Join-Path $reportRoot 'native-coverage.runsettings'
$settingsTemplate.Replace('__PLUGIN_ROOT__', $escapedPluginRoot) | Set-Content -LiteralPath $settings -Encoding UTF8
$baseline = Join-Path $PSScriptRoot 'run-baseline.ps1'
$powershell = Join-Path $PSHOME $(if ($PSVersionTable.PSEdition -eq 'Desktop') { 'powershell.exe' } else { 'pwsh.exe' })

$coverageArguments = @(
    'collect', '--settings', $settings,
    '--output', $coverageReport, '--output-format', 'cobertura',
    '--', $powershell, '-NoProfile', '-File', $baseline,
    '-UnrealEditorCmd', $UnrealEditorCmd
)
& $coverageConsole @coverageArguments
if ($LASTEXITCODE -ne 0) { throw "Native coverage collection failed. Full report/logs are retained at $reportRoot." }

node (Join-Path $PSScriptRoot 'verify-native-coverage.cjs') $coverageReport
if ($LASTEXITCODE -ne 0) { throw "Native production coverage did not reach 100%. Full report is retained at $coverageReport." }
