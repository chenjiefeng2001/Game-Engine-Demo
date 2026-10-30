param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Continue"
$repo = Split-Path -Parent $PSScriptRoot
$editorProj = Join-Path $repo "editor_avalonia\AvaloniaEditor.csproj"
$editorExe = Join-Path $repo "editor_avalonia\bin\$Configuration\net9.0\AvaloniaEditor.exe"
$buildLog = Join-Path $repo "build\uih2p1b_build_$Configuration.log"
$gateLog = Join-Path $repo "build\uih2p1b_gate_$Configuration.log"
$gateErrLog = "$gateLog.err"

New-Item -ItemType Directory -Force -Path (Split-Path $buildLog) | Out-Null

Write-Host "[uih2p1b] build editor ($Configuration)"
$buildOutput = & dotnet build $editorProj -c $Configuration 2>&1
$buildExit = $LASTEXITCODE
$buildOutput | Tee-Object -FilePath $buildLog
if ($buildExit -ne 0) {
    Write-Host "[uih2p1b] editor build failed: $buildExit"
    exit $buildExit
}
if (-not (Test-Path $editorExe)) {
    Write-Host "[uih2p1b] missing editor exe: $editorExe"
    exit 2
}

Write-Host "[uih2p1b] run P1-B gate ($Configuration)"
$p = Start-Process -FilePath $editorExe `
    -ArgumentList "--uih2p1b" `
    -WorkingDirectory $repo `
    -Wait -PassThru `
    -RedirectStandardOutput $gateLog `
    -RedirectStandardError $gateErrLog
$exit = $p.ExitCode
if (Test-Path $gateErrLog) {
    Get-Content $gateErrLog | Tee-Object -FilePath $gateLog -Append
}
if (-not (Test-Path $gateLog)) {
    Write-Host "[uih2p1b] gate produced no log"
    exit 3
}
Get-Content $gateLog | Select-Object -Last 20
Write-Host "[uih2p1b] exit=$exit"
Write-Host "[uih2p1b] logs: $buildLog ; $gateLog"
exit $exit
