<#
.SYNOPSIS
    HRC-3 P2 standing regression gates: --p2frame (M1-M7) + --p2ui (U1-U8).

.DESCRIPTION
    P2 has been promoted from an experiment gate to a production-path capability,
    so it must live in the STANDING gate set -- otherwise the capability stays
    implemented while future regressions fall outside the normal gate surface.

    This runner is wiring/documentation only:
      - it changes NO runtime behavior, NO native code, NO ABI;
      - it does NOT add native inventory, so the I2 baseline stays 243.

    Discipline inherited from the NHI-1 lesson:
      - every scenario runs in its OWN process (GL/GLFW teardown contamination);
      - MSVC getenv issue -> the app sets ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE
        itself before the first P/Invoke, so no extra env is needed;
      - exactly ONE attempt per scenario. There is deliberately NO retry/delay:
        a failure must stay a failure and be read, not papered over.

    Expected evidence when green (per scenario):
      --p2frame : "[P2-MANAGED GATE] ALL GREEN"   (M1 version lockstep, M2 packet
                  offsets/size, M3/M5 atomic+monotonic, M4 no stale reuse,
                  M6/M7 inventory explicit)
      --p2ui    : "[P2-UI GATE] ALL GREEN"       (U1 byte-exact bitmap<->packet,
                  U2 production ViewportImage.Source bound, U3 display identity,
                  U4 no regression / no stale replay, U5 not clear-only)

.NOTES
    Repo root is the working directory: gates load assets/gp01/manifest.json
    via a relative path.

.EXAMPLE
    powershell -File tools/run_p2_gates.ps1
    powershell -File tools/run_p2_gates.ps1 -TimeoutSec 240
#>
param(
    [string]$Config = "Debug",
    [int]$TimeoutSec = 240
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$exe = Join-Path $repoRoot "editor_avalonia\bin\$Config\net9.0\AvaloniaEditor.exe"
$nativeDll = Join-Path $repoRoot "build\bridge\$Config\EditorBridge.dll"

if (-not (Test-Path $exe)) {
    Write-Host "[p2-gates] FAIL: managed host not found: $exe" -ForegroundColor Red
    Write-Host "[p2-gates] build it first: dotnet build editor_avalonia" -ForegroundColor Yellow
    exit 1
}
if (-not (Test-Path $nativeDll)) {
    Write-Host "[p2-gates] FAIL: native bridge not found: $nativeDll" -ForegroundColor Red
    Write-Host "[p2-gates] build it first: cmake --build build --config $Config --target EditorBridge" -ForegroundColor Yellow
    exit 1
}

# One process per scenario. Env: none required (the app sets the ASan var itself).
$scenarios = @(
    [pscustomobject]@{ Name = 'p2frame'; Args = @('--p2frame'); Marker = '[P2-MANAGED GATE] ALL GREEN'; Covers = 'M1-M7 (managed fetch mirror)' },
    [pscustomobject]@{ Name = 'p2ui';    Args = @('--p2ui');    Marker = '[P2-UI GATE] ALL GREEN';       Covers = 'U1-U8 (UI proof: frame displayed)' }
)

$failures = New-Object System.Collections.Generic.List[string]
$results = @()

foreach ($s in $scenarios) {
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $exe
    $psi.WorkingDirectory = $repoRoot
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    foreach ($a in $s.Args) { $psi.ArgumentList.Add($a) }

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

    $green = (-not $timedOut) -and ($exitCode -eq 0) -and ($stdout -match [regex]::Escape($s.Marker))

    # Surface the gate's own evidence lines so a run is readable without opening a file.
    foreach ($line in ($stdout -split "`r?`n")) {
        if ($line -match '^\[(P2-M|P2-U|P2-UI)') {
            Write-Host "    $($line.Trim())"
        }
    }

    $sw.Stop()
    $status = if ($green) { 'GREEN' } else { 'FAIL' }
    $detail = if ($timedOut) { "timeout after ${TimeoutSec}s" } else { "exit=$exitCode, $([int]$sw.Elapsed.TotalSeconds)s" }
    Write-Host ("[p2-gates] {0,-8} {1,-7} {2}  ({3})" -f $s.Name, $status, $detail, $s.Covers)

    if (-not $green) {
        $failures.Add("$($s.Name): $detail (expected marker '$($s.Marker)')")
        foreach ($line in (($stderr -split "`r?`n") + ($stdout -split "`r?`n"))) {
            if ($line -match 'FAIL|Unhandled|Exception') {
                $failures.Add("  $($line.Trim())")
            }
        }
    }

    $results += [pscustomobject]@{ Name = $s.Name; Green = $green; ExitCode = $exitCode }
}

if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "[p2-gates] FAILURES:" -ForegroundColor Red
    foreach ($f in $failures) { Write-Host "  $f" -ForegroundColor Red }
    exit 1
}

Write-Host ""
Write-Host "[p2-gates] ALL GREEN ($($results.Count) scenarios, I2 unchanged: no native inventory added)" -ForegroundColor Green
exit 0
