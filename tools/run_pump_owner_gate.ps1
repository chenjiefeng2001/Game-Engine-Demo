<#
.SYNOPSIS
    Commit A standing regression gate: exactly one EngineHost runtime pump owner.

.DESCRIPTION
    Runs the real Avalonia Open handler, then measures a fixed window:
      - managed PumpInvocations delta must equal native frame delta;
      - pump rate must stay near the 33 ms nominal interval (<= 50/s);
      - a second driver (previous MainWindow production timer) doubles the rate;
      - Play/Stop must not start another driver.

    This runner is wiring only: no native ABI change, no runtime behavior change.

.EXAMPLE
    powershell -File tools/run_pump_owner_gate.ps1
    powershell -File tools/run_pump_owner_gate.ps1 -Config Release -TimeoutSec 180
#>
param(
    [string]$Config = "Debug",
    [int]$TimeoutSec = 120
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$exe = Join-Path $repoRoot "editor_avalonia\bin\$Config\net9.0\AvaloniaEditor.exe"
$nativeDll = Join-Path $repoRoot "build\bridge\$Config\EditorBridge.dll"

if (-not (Test-Path $exe)) {
    Write-Host "[pump-owner] FAIL: managed host not found: $exe" -ForegroundColor Red
    Write-Host "[pump-owner] build it first: dotnet build editor_avalonia" -ForegroundColor Yellow
    exit 1
}
if (-not (Test-Path $nativeDll)) {
    Write-Host "[pump-owner] FAIL: native bridge not found: $nativeDll" -ForegroundColor Red
    Write-Host "[pump-owner] build it first: cmake --build build --config $Config --target EditorBridge" -ForegroundColor Yellow
    exit 1
}

$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $exe
$psi.WorkingDirectory = $repoRoot
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.ArgumentList.Add('--pumpowner')

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$proc = [System.Diagnostics.Process]::Start($psi)
$stdoutTask = $proc.StandardOutput.ReadToEndAsync()
$stderrTask = $proc.StandardError.ReadToEndAsync()

$exited = $proc.WaitForExit($TimeoutSec * 1000)
$timedOut = -not $exited
if ($timedOut) {
    try { $proc.Kill($true) } catch { }
    $proc.WaitForExit(5000) | Out-Null
    $exitCode = -1
} else {
    $exitCode = $proc.ExitCode
}
$stdout = $stdoutTask.GetAwaiter().GetResult()
$stderr = $stderrTask.GetAwaiter().GetResult()
$sw.Stop()

foreach ($line in ($stdout -split "`r?`n")) {
    if ($line -match '^\[PUMP-OWNER') { Write-Host "    $($line.Trim())" }
}

$marker = '[PUMP-OWNER GATE] ALL GREEN'
$green = (-not $timedOut) -and ($exitCode -eq 0) -and ($stdout -match [regex]::Escape($marker))

if ($green) {
    Write-Host ("[pump-owner] GREEN  exit={0}, {1:F1}s" -f $exitCode, $sw.Elapsed.TotalSeconds) -ForegroundColor Green
    exit 0
}

$detail = if ($timedOut) { "timeout after ${TimeoutSec}s" } else { "exit=$exitCode, $([int]$sw.Elapsed.TotalSeconds)s" }
Write-Host "[pump-owner] FAIL  $detail (expected marker '$marker')" -ForegroundColor Red
foreach ($line in (($stderr -split "`r?`n") + ($stdout -split "`r?`n"))) {
    if ($line -match 'FAIL|Unhandled|Exception') { Write-Host "  $($line.Trim())" -ForegroundColor Red }
}
exit 1
