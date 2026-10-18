# ============================================================
# nhi1_stress.ps1 - NHI-1 (Native Hang Investigation) stress harness
#
# Purpose: reproduce the "gate ALL GREEN + exit=0 printed, but the
#          process never terminates" failure found by Stage A/A2, and
#          capture evidence (thread census + minidump + managed stacks)
#          at the moment of the hang.
#
# This tool is INVESTIGATION infrastructure only:
#   - it never changes gate verdicts;
#   - a timeout is reported as `HANG`, not as PASS/FAIL of a gate;
#   - it does not substitute a watchdog for root-causing the defect.
#
# Usage:
#   powershell -NoProfile -File tools/nhi1_stress.ps1 `
#       -Scenario fbo -Iterations 40 -HangTimeoutSec 15 -Dump
#
# Scenarios map to the A2 gate's native failure injection points:
#   happy | appinit | fbo | session | frame | tick | playbind
#
# Exit code: 0 = no hang observed; 2 = at least one hang captured
#            (the hang itself is the finding, not a tool failure).
# ============================================================
param(
    [ValidateSet("happy", "appinit", "fbo", "session", "frame", "tick", "playbind")]
    [string]$Scenario = "fbo",
    [string]$Gate = "--boot",
    [int]$Iterations = 40,
    [int]$HangTimeoutSec = 15,
    [switch]$Dump,
    [string]$OutDir = "logs/nhi1"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repoRoot "editor_avalonia\bin\Debug\net9.0\AvaloniaEditor.exe"
if (-not (Test-Path $exe)) { throw "editor binary not found: $exe (dotnet build first)" }

$inject = @{
    happy    = $null
    appinit  = "EDITOR_BRIDGE_FAIL_APPLICATION_INIT"
    fbo      = "EDITOR_BRIDGE_FAIL_FBO"
    session  = "EDITOR_BRIDGE_FAIL_SESSION_ATTACH"
    frame    = "EDITOR_BRIDGE_FAIL_FRAME"
    tick     = "EDITOR_BRIDGE_FAIL_RUNTIME_TICK"
    playbind = "EDITOR_BRIDGE_FAIL_PLAY_BIND"
}
$allVars = @("EDITOR_BRIDGE_FAIL_APPLICATION_INIT", "EDITOR_BRIDGE_FAIL_FBO",
             "EDITOR_BRIDGE_FAIL_SESSION_ATTACH", "EDITOR_BRIDGE_FAIL_FRAME",
             "EDITOR_BRIDGE_FAIL_RUNTIME_COMMAND", "EDITOR_BRIDGE_FAIL_RUNTIME_TICK",
             "EDITOR_BRIDGE_FAIL_PLAY_BIND")

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$log = Join-Path $OutDir "nhi1-$Scenario-$stamp.log"

function Write-Log([string]$line) {
    $text = "[{0:HH:mm:ss.fff}] {1}" -f (Get-Date), $line
    Write-Host $text
    Add-Content -Path $log -Value $text
}

# Tool paths installed via `dotnet tool install -g dotnet-stack|dotnet-dump`
$toolDir = Join-Path $env:USERPROFILE ".dotnet\tools"
if (Test-Path $toolDir) { $env:Path = "$toolDir;$env:Path" }

Write-Log "NHI-1 stress: scenario=$Scenario gate=$Gate iterations=$Iterations hangTimeout=${HangTimeoutSec}s dump=$($Dump.IsPresent)"
Write-Log "binary: $exe"

$hangs = 0; $clean = 0; $otherExit = 0
$swAll = [System.Diagnostics.Stopwatch]::StartNew()

for ($i = 1; $i -le $Iterations; $i++) {
    foreach ($v in $allVars) { [System.Environment]::SetEnvironmentVariable($v, $null, "Process") }
    if ($inject[$Scenario]) { [System.Environment]::SetEnvironmentVariable($inject[$Scenario], "1", "Process") }

    $out = Join-Path $OutDir "run-$Scenario-$i.out"
    $err = Join-Path $OutDir "run-$Scenario-$i.err"
    # 直接用 Process API：Start-Process -PassThru 的 ExitCode 在重定向场景下不可靠。
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $exe
    $psi.Arguments = $Gate
    $psi.WorkingDirectory = $repoRoot
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $stdoutTask = $p.StandardOutput.ReadToEndAsync()
    $stderrTask = $p.StandardError.ReadToEndAsync()

    if ($p.WaitForExit($HangTimeoutSec * 1000)) {
        $stdoutTask.Result | Out-File -FilePath $out -Encoding utf8
        $stderrTask.Result | Out-File -FilePath $err -Encoding utf8
        $code = $p.ExitCode
        $p.Dispose()
        $verdict = (Select-String -Path $out -Pattern "ALL GREEN|FAIL" -ErrorAction SilentlyContinue |
                    Select-Object -Last 1).Line
        if ($code -eq 0 -and $verdict -match "ALL GREEN") {
            $clean++
            Write-Log ("run {0,3}: exit=0 GREEN" -f $i)
        } else {
            $otherExit++
            Write-Log ("run {0,3}: exit={1} verdict='{2}'" -f $i, $code, $verdict)
        }
        continue
    }

    # ── hang detected: capture evidence BEFORE killing ──────────────
    $hangs++
    $pr = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
    $threads = if ($pr) { $pr.Threads.Count } else { 0 }
    $cpu = if ($pr) { [math]::Round($pr.CPU, 2) } else { 0 }
    $ws = if ($pr) { [math]::Round($pr.WorkingSet64 / 1MB, 1) } else { 0 }
    Write-Log ("run {0,3}: HANG pid={1} threads={2} cpu={3}s ws={4}MB" -f $i, $p.Id, $threads, $cpu, $ws)
    Write-Log ("         last gate line: {0}" -f ((Select-String -Path $out -Pattern "ALL GREEN|FAIL|exit=" |
                Select-Object -Last 1).Line))

    # 线程来源普查：把每个线程的 StartAddress 映射回模块。
    # 没有 cdb/procdump 时，这是判断"是 GL 驱动线程还是 ASan/ntdll 线程卡住"的最便宜证据。
    $censusPath = Join-Path $OutDir "hang-$Scenario-$i-$($p.Id).threads.txt"
    try {
        if (-not $pr) { throw "process object is gone; cannot census threads" }
        $modules = @()
        foreach ($m in $pr.Modules) {
            $modules += [pscustomobject]@{
                Name = $m.ModuleName
                Base = [int64]$m.BaseAddress.ToInt64()
                Size = [int64]$m.ModuleMemorySize
            }
        }
        $census = @("pid=$($p.Id) threads=$($pr.Threads.Count) modules=$($modules.Count)",
                    "clrLoaded=" + [bool]($modules | Where-Object { $_.Name -match "coreclr|clrjit" }),
                    "editorBridgeLoaded=" + [bool]($modules | Where-Object { $_.Name -match "EditorBridge" }),
                    "graphicsLoaded=" + [bool]($modules | Where-Object { $_.Name -match "opengl32|gdi32|d3d11|dxgi" }),
                    "asanLoaded=" + [bool]($modules | Where-Object { $_.Name -match "clang_rt|asan" }),
                    "", "tid`tstate`twait`towner-module`tstart-address")
        foreach ($t in $pr.Threads) {
            $addr = [int64]$t.StartAddress.ToInt64()
            $owner = "?"
            foreach ($m in $modules) {
                if ($addr -ge $m.Base -and $addr -lt ($m.Base + $m.Size)) { $owner = $m.Name; break }
            }
            $census += ("{0}`t{1}`t{2}`t{3}`t0x{4:X}" -f $t.Id, $t.ThreadState, $t.WaitReason, $owner, $addr)
        }
        Set-Content -Path $censusPath -Value $census -Encoding utf8
        Write-Log "         thread census: $censusPath"
        $census | Select-Object -First 5 | ForEach-Object { Write-Log "           $_" }
    } catch {
        Write-Log "         thread census failed: $($_.Exception.Message)"
    }

    if ($Dump) {
        # 顺序很重要：先抓托管栈（进程还活着时最可靠），再抓 minidump。
        $stackOut = Join-Path $OutDir "hang-$Scenario-$i-$($p.Id).stack.txt"
        try {
            dotnet-stack report --process-id $p.Id *>&1 | Out-File -FilePath $stackOut -Encoding utf8
            Write-Log "         managed stacks: $stackOut"
        } catch {
            Write-Log "         dotnet-stack failed: $($_.Exception.Message)"
        }

        $dmp = Join-Path $OutDir "hang-$Scenario-$i-$($p.Id).dmp"
        Remove-Item $dmp -ErrorAction SilentlyContinue
        Start-Process -FilePath "rundll32.exe" `
            -ArgumentList "C:\Windows\System32\comsvcs.dll, MiniDump $($p.Id) $dmp full" `
            -NoNewWindow -Wait | Out-Null
        if (Test-Path $dmp) {
            Write-Log ("         minidump: {0} ({1} MB)" -f $dmp, [math]::Round((Get-Item $dmp).Length / 1MB, 1))
        } else {
            Write-Log "         minidump FAILED"
        }
    }

    # Does it ever exit on its own? Bound the observation, then reclaim.
    $stillAlive = -not $p.WaitForExit(120000)
    if ($stillAlive) {
        Write-Log "         still alive after +120s (hard hang)"
        try { $p.Kill($true) } catch { }
    } else {
        Write-Log "         exited on its own, code=$($p.ExitCode)"
    }
}

foreach ($v in $allVars) { [System.Environment]::SetEnvironmentVariable($v, $null, "Process") }

Write-Log "---- summary ----"
Write-Log "scenario=$Scenario clean=$clean hang=$hangs otherExit=$otherExit elapsed=$([math]::Round($swAll.Elapsed.TotalSeconds,1))s"
Write-Log "log: $log"
if ($hangs -gt 0) { exit 2 } else { exit 0 }
