param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Continue"
$repo = Split-Path -Parent $PSScriptRoot
$editorProj = Join-Path $repo "editor_avalonia\AvaloniaEditor.csproj"
$editorExe = Join-Path $repo "editor_avalonia\bin\$Configuration\net9.0\AvaloniaEditor.exe"
$testProj = Join-Path $repo "tests\EditorAvalonia.IntegrationTests\EditorAvalonia.IntegrationTests.csproj"
$buildLog = Join-Path $repo "build\m1import_editor_$Configuration.log"
$testLog = Join-Path $repo "build\m1import_gate_$Configuration.log"

New-Item -ItemType Directory -Force -Path (Split-Path $buildLog) | Out-Null

Write-Host "[m1import] build editor ($Configuration)"
$buildOutput = & dotnet build $editorProj -c $Configuration 2>&1
$editorExit = $LASTEXITCODE
$buildOutput | Tee-Object -FilePath $buildLog
if ($editorExit -ne 0) {
    Write-Host "[m1import] editor build failed: $editorExit"
    exit $editorExit
}
if (-not (Test-Path $editorExe)) {
    Write-Host "[m1import] missing editor exe: $editorExe"
    exit 2
}

$env:M1IMPORT_EDITOR_EXE = $editorExe
$env:M1IMPORT_REPO_ROOT = $repo
$env:M1IMPORT_CONFIG = $Configuration

Write-Host "[m1import] run UIA integration test ($Configuration)"
$testOutput = & dotnet test $testProj -c $Configuration --logger "console;verbosity=detailed" 2>&1
$testExit = $LASTEXITCODE
$testOutput | Tee-Object -FilePath $testLog

Write-Host "[m1import] exit=$testExit"
Write-Host "[m1import] logs: $buildLog ; $testLog"
exit $testExit
