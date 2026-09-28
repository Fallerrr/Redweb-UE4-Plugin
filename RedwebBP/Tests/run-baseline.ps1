param(
    [string]$UnrealEditorCmd = $env:UE4_EDITOR_CMD
)

$ErrorActionPreference = 'Stop'
$pluginRoot = Split-Path -Parent $PSScriptRoot
$integration = Join-Path $PSScriptRoot 'Integration'
$testProject = Join-Path $PSScriptRoot 'RedwebBPTestHost\RedwebBPTestHost.uproject'

Push-Location $integration
try {
    npm ci --ignore-scripts
    if ($LASTEXITCODE -ne 0) { throw 'Could not install the pinned Redweb integration fixture.' }
    npm test
    if ($LASTEXITCODE -ne 0) { throw 'The real Redweb server integration test failed.' }
} finally {
    Pop-Location
}

if ([string]::IsNullOrWhiteSpace($UnrealEditorCmd) -or -not (Test-Path -LiteralPath $UnrealEditorCmd)) {
    throw 'Set UE4_EDITOR_CMD to the UE4.27 UnrealEditor-Cmd.exe path to run the plugin automation tests.'
}

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$listener.Stop()
$env:REDWEBBP_TEST_PORT = [string]$port
$env:REDWEBBP_TEST_URL = "ws://127.0.0.1:$port/socket"
$env:REDWEBBP_CURRENT_TEST_URL = "ws://127.0.0.1:$port/current"

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('redwebbp-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot | Out-Null
$fixtureOut = Join-Path $tempRoot 'fixture.out.log'
$fixtureErr = Join-Path $tempRoot 'fixture.err.log'
$editorLog = Join-Path $tempRoot 'unreal-editor.log'
$fixture = Start-Process -FilePath 'node' `
    -ArgumentList @('fixture.cjs') `
    -WorkingDirectory $integration `
    -WindowStyle Hidden `
    -RedirectStandardOutput $fixtureOut `
    -RedirectStandardError $fixtureErr `
    -PassThru

try {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($fixture.HasExited) { throw "Redweb fixture failed: $(Get-Content -Raw $fixtureErr)" }
        if ((Test-Path -LiteralPath $fixtureOut) -and (Get-Content -Raw $fixtureOut) -match "REDBP_FIXTURE_READY $port") { break }
        Start-Sleep -Milliseconds 100
    }
    if (-not (Test-Path -LiteralPath $fixtureOut) -or (Get-Content -Raw $fixtureOut) -notmatch "REDBP_FIXTURE_READY $port") {
        throw 'Timed out waiting for the Redweb integration fixture.'
    }

    $arguments = @(
        $testProject,
        '-unattended', '-nop4', '-nosplash', '-NullRHI',
        '-ExecCmds="Automation RunTests RedwebBP; Quit"',
        '-testexit="Automation Test Queue Empty"',
        "-abslog=$editorLog"
    )
    $editor = Start-Process -FilePath $UnrealEditorCmd -ArgumentList $arguments -Wait -PassThru
    if ($editor.ExitCode -ne 0) {
        $log = if (Test-Path -LiteralPath $editorLog) { Get-Content -Raw $editorLog } else { '<Unreal produced no log>' }
        throw "Unreal automation failed with exit code $($editor.ExitCode):`n$log"
    }
} finally {
    if (-not $fixture.HasExited) { Stop-Process -Id $fixture.Id -Force }
    Remove-Item -LiteralPath $tempRoot -Recurse -Force
}
