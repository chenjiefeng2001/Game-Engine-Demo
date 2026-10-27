<#
.SYNOPSIS
    Slice D standing regression gate: production viewport pick (click -> PICK -> selection).

.DESCRIPTION
    Runs the real Avalonia UI on the EngineHost (production) backend:
      - P1  geometry premise: the frame is drawn 1:1 at the viewport origin, and the
            canonical-pixel mapper rejects out-of-frame / non-finite coordinates;
      - P2  accepted != selected: an accepted request is Pending, the selection is untouched;
      - P3  one outstanding request: a second click neither queues nor replaces it;
      - P4  fail-closed identity: an unresolved hit does not move the selection;
      - P5  the seam, once resolvable, selects the right entity (gate-only resolver);
      - P6  no-hit is a normal result and clears the selection;
      - P7  out-of-frame clicks submit no request and are never clamped;
      - P8  the legacy ViewportPick path is not used and is rejected on this backend;
      - P9  keyboard is consumed only while the viewport holds focus;
      - P10 no ABI surface change (v1.3, PICK=29, 32B packet).

    Runner only: no native ABI change, no runtime behavior change, no fixture write.

.NOTES
    Must be run with PowerShell 7 (`pwsh`): $psi.ArgumentList does not exist in PS 5.1.
    Exactly ONE attempt per run -- no retry, no delay.

    The gate flag is `--prodpick` and deliberately NOT `--prodpathpick`:
    `--prodpathpick` contains `--prodpath`, which would register the *other* gate's
    handler as well (observed: both gates ran and aborted each other).

.EXAMPLE
    pwsh -File tools/run_production_pick_gate.ps1
    pwsh -File tools/run_production_pick_gate.ps1 -Config Release -TimeoutSec 180
#>
param(
    [string]$Config = "Debug",
    [int]$TimeoutSec = 180
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$exe = Join-Path $repoRoot "editor_avalonia\bin\$Config\net9.0\AvaloniaEditor.exe"
$nativeDll = Join-Path $repoRoot "build\bridge\$Config\EditorBridge.dll"

if (-not (Test-Path $exe)) {
    Write-Host "[prodpick] FAIL: managed host not found: $exe" -ForegroundColor Red
    Write-Host "[prodpick] build it first: dotnet build editor_avalonia -c $Config" -ForegroundColor Yellow
    exit 1
}
if (-not (Test-Path $nativeDll)) {
    Write-Host "[prodpick] FAIL: native bridge not found: $nativeDll" -ForegroundColor Red
    Write-Host "[prodpick] build it first: cmake --build build --config $Config --target EditorBridge" -ForegroundColor Yellow
    exit 1
}

$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $exe
$psi.WorkingDirectory = $repoRoot
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.ArgumentList.Add('--prodpick')

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
    if ($line -match '^\[PROD-PICK') { Write-Host "    $($line.Trim())" }
}

$marker = '[PROD-PICK GATE] ALL GREEN'
$green = (-not $timedOut) -and ($exitCode -eq 0) -and ($stdout -match [regex]::Escape($marker))

if ($green) {
    Write-Host ("[prodpick] GREEN  exit={0}, {1:F1}s" -f $exitCode, $sw.Elapsed.TotalSeconds) -ForegroundColor Green
    exit 0
}

$detail = if ($timedOut) { "timeout after ${TimeoutSec}s" } else { "exit=$exitCode, $([int]$sw.Elapsed.TotalSeconds)s" }
Write-Host "[prodpick] FAIL  $detail (expected marker '$marker')" -ForegroundColor Red
foreach ($line in (($stderr -split "`r?`n") + ($stdout -split "`r?`n"))) {
    if ($line -match 'FAIL|Unhandled|Exception') { Write-Host "  $($line.Trim())" -ForegroundColor Red }
}
exit 1
