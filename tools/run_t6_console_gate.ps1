param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Continue"
$repo = Split-Path -Parent $PSScriptRoot
$editorProj = Join-Path $repo "editor_avalonia\AvaloniaEditor.csproj"
$editorExe = Join-Path $repo "editor_avalonia\bin\$Configuration\net9.0\AvaloniaEditor.exe"
$testProj = Join-Path $repo "tests\EditorAvalonia.IntegrationTests\EditorAvalonia.IntegrationTests.csproj"
$buildLog = Join-Path $repo "build\t6console_build_$Configuration.log"
$gateLog = Join-Path $repo "build\t6console_gate_$Configuration.log"
$testLog = Join-Path $repo "build\t6ui_test_$Configuration.log"

New-Item -ItemType Directory -Force -Path (Split-Path $buildLog) | Out-Null

Write-Host "[t6] build editor ($Configuration)"
$buildOutput = & dotnet build $editorProj -c $Configuration 2>&1
$editorExit = $LASTEXITCODE
$buildOutput | Tee-Object -FilePath $buildLog
if ($editorExit -ne 0) {
    Write-Host "[t6] editor build failed: $editorExit"
    exit $editorExit
}
if (-not (Test-Path $editorExe)) {
    Write-Host "[t6] missing editor exe: $editorExe"
    exit 2
}

Write-Host "[t6] run real-window gate ($Configuration)"
$gateErrLog = "$gateLog.err"
$gateProcess = Start-Process -FilePath $editorExe `
    -ArgumentList "--t6console" `
    -WorkingDirectory $repo `
    -Wait -PassThru `
    -RedirectStandardOutput $gateLog `
    -RedirectStandardError $gateErrLog
$gateExit = $gateProcess.ExitCode
if (Test-Path $gateErrLog) {
    Get-Content $gateErrLog | Tee-Object -FilePath $gateLog -Append
}
if (-not (Test-Path $gateLog)) {
    Write-Host "[t6] real-window gate produced no log"
    exit 3
}
Get-Content $gateLog | Select-Object -Last 20
if ($gateExit -ne 0) {
    Write-Host "[t6] real-window gate failed: $gateExit"
    exit $gateExit
}

Write-Host "[t6] build UIA test project ($Configuration)"
$testBuildOutput = & dotnet build $testProj -c $Configuration 2>&1
$testBuildExit = $LASTEXITCODE
$testBuildOutput | Tee-Object -FilePath $buildLog -Append
if ($testBuildExit -ne 0) {
    Write-Host "[t6] UIA test build failed: $testBuildExit"
    exit $testBuildExit
}

$env:T6UI_EDITOR_EXE = $editorExe
$env:T6UI_REPO_ROOT = $repo
$env:T6UI_CONFIG = $Configuration

Write-Host "[t6] run UIA test ($Configuration)"
$testOutput = & dotnet test $testProj -c $Configuration --filter "FullyQualifiedName~T6ConsoleUITests" --logger "console;verbosity=detailed" 2>&1
$testExit = $LASTEXITCODE
$testOutput | Tee-Object -FilePath $testLog

Write-Host "[t6] exit=$testExit"
Write-Host "[t6] logs: $buildLog ; $gateLog ; $testLog"
exit $testExit
