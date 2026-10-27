<#
.SYNOPSIS
    Commit C standing regression gate: selection notification + status bar sync.

.DESCRIPTION
    Runs the real Avalonia UI:
      - C1: after the view model auto-selects the newly created entity, the
            Hierarchy ListBox SelectedItem must follow (model-right != control-right);
      - C2: one selection change raises exactly one SelectedEntity notification,
            and re-selecting the same value raises none;
      - C3: StatusBar.Text must equal Vm.StatusText on every path, including the
            event-driven refresh that has no View handler to finish the job;
      - C4: closing the window clears the list selection and the status bar.

    Runner only: no native ABI change, no runtime behavior change, no fixture write.

.NOTES
    Must be run with PowerShell 7 (`pwsh`): $psi.ArgumentList does not exist in PS 5.1.
    Exactly ONE attempt per run -- no retry, no delay.

.EXAMPLE
    pwsh -File tools/run_selection_status_gate.ps1
    pwsh -File tools/run_selection_status_gate.ps1 -Config Release -TimeoutSec 180
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
    Write-Host "[selection-status] FAIL: managed host not found: $exe" -ForegroundColor Red
    Write-Host "[selection-status] build it first: dotnet build editor_avalonia" -ForegroundColor Yellow
    exit 1
}
if (-not (Test-Path $nativeDll)) {
    Write-Host "[selection-status] FAIL: native bridge not found: $nativeDll" -ForegroundColor Red
    Write-Host "[selection-status] build it first: cmake --build build --config $Config --target EditorBridge" -ForegroundColor Yellow
    exit 1
}

$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $exe
$psi.WorkingDirectory = $repoRoot
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.ArgumentList.Add('--selstatus')

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
    if ($line -match '^\[SELECTION-STATUS') { Write-Host "    $($line.Trim())" }
}

$marker = '[SELECTION-STATUS GATE] ALL GREEN'
$green = (-not $timedOut) -and ($exitCode -eq 0) -and ($stdout -match [regex]::Escape($marker))

if ($green) {
    Write-Host ("[selection-status] GREEN  exit={0}, {1:F1}s" -f $exitCode, $sw.Elapsed.TotalSeconds) -ForegroundColor Green
    exit 0
}

$detail = if ($timedOut) { "timeout after ${TimeoutSec}s" } else { "exit=$exitCode, $([int]$sw.Elapsed.TotalSeconds)s" }
Write-Host "[selection-status] FAIL  $detail (expected marker '$marker')" -ForegroundColor Red
foreach ($line in (($stderr -split "`r?`n") + ($stdout -split "`r?`n"))) {
    if ($line -match 'FAIL|Unhandled|Exception') { Write-Host "  $($line.Trim())" -ForegroundColor Red }
}
exit 1
