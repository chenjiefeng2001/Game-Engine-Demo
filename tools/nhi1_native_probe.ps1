# ============================================================
# nhi1_native_probe.ps1 - NHI-1.4 native-only teardown harness
#                      + NHI-1.1 native live-hang attach
#
# 目的：
#   1) 二分问题：去掉 Avalonia/CLR/managed Host 后 native teardown hang 是否仍存在；
#   2) 一旦命中 hang 或"退出停滞"，**先 attach 取 native 栈与 dump，再回收进程**。
#
# 关键机制（DONE-then-stall detector）：
#   probe 的最后一行是 "DONE rc=.. total=..ms"，之后就是进程退出阶段。
#   观测到 DONE 后若进程在 StallSec 内没有退出，说明进入了"退出延迟增长/hang"状态，
#   此时 attach 的现场与真正 hang 相同，但命中窗口远大于等一个完整 watchdog 超时。
#
# 纪律：
#   - 每个 scenario 一个独立进程；watchdog 只回收，**timeout 永远记为 FINDING**；
#   - "正常退出" 只是 lifecycle outcome，不等于功能 PASS；
#   - hang 时取栈顺序：~*k → ~*kv → .dump /ma；不为多取命令破坏唯一现场；
#   - 不进 CTest、不改 I2=240、不改 production 代码。
#
# 用法：
#   powershell -NoProfile -File tools/nhi1_native_probe.ps1 -Scenarios A-fbo -Iterations 80 -AttachCdb
# ============================================================
param(
    # -Iterations 0 = 不设次数目标（长批次后台跑；由 -StopOnFinding / -MaxMinutes 收敛）
    [int]$Iterations = 20,
    [int]$MaxMinutes = 240,
    [switch]$ContinueAfterFinding,   # 默认命中即停；显式传本开关才继续
    [int]$WatchdogSec = 30,
    [int]$StallSec = 5,
    [string]$Scenarios = "",
    [string]$Probe = "build\nhi1\Debug\nhi1_native_probe.exe",
    [string]$OutDir = "logs/nhi1",
    [string]$CdbPath = "",
    [int]$CdbTimeoutSec = 240,
    [switch]$AttachCdb,
    [switch]$NoDump,
    [ValidateSet("seq", "reload")]
    [string]$Mode = "seq"
)

# 本仓库约定：含 CJK 的 .ps1 必须带 UTF-8 BOM（PowerShell 5.1 否则按 ANSI 解码并解析错位）。
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$probePath = Join-Path $repoRoot $Probe
if (-not (Test-Path $probePath)) {
    throw "probe not found: $probePath (cmake -S tools/nhi1_native_teardown_probe -B build/nhi1 ...)"
}
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

$all = @("A-fbo", "A-appinit", "A-session",
         "B-tick-managed", "B-tick-legacy",
         "B-frame-managed", "B-frame-legacy",
         "C-legacy-session")
if ($Scenarios) { $all = $Scenarios.Split(",") | ForEach-Object { $_.Trim() } }

# MSVC ASan interception 必须在进程启动前放行（否则 interception_win.cpp:193 直接 abort，
# 每次运行都会变成假 finding）。与 tools/run_test.cmd 同一纪律。
$env:ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE = "1"

# scenario -> 进程启动前必须设置的注入（bootstrap 类故障在 Create 时就注入）
$inject = @{
    "A-fbo"          = "EDITOR_BRIDGE_FAIL_FBO"
    "A-appinit"      = "EDITOR_BRIDGE_FAIL_APPLICATION_INIT"
    "A-session"      = "EDITOR_BRIDGE_FAIL_SESSION_ATTACH"
    "B-tick-managed" = $null   # probe 自己在 Play 之后用 _putenv_s 注入
    "B-tick-legacy"  = $null
    "B-frame-managed" = $null
    "B-frame-legacy" = $null
    "C-legacy-session" = $null
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$log = Join-Path $OutDir "nhi1.4-native-$stamp.log"
$csv = Join-Path $OutDir "nhi1.4-native-$stamp.csv"
$rows = @()

function Write-Log([string]$line) {
    $text = "[{0:HH:mm:ss.fff}] {1}" -f (Get-Date), $line
    Write-Host $text
    Add-Content -Path $log -Value $text
}

function Resolve-Cdb {
    if ($CdbPath -and (Test-Path $CdbPath)) { return $CdbPath }
    # WinDbg 装在 WindowsApps 包目录里，直接执行会被拒（Access is denied）；
    # 因此优先使用可执行副本目录，其次才回落到包目录。
    $local = Join-Path $env:TEMP "nhi1-tools\cdb\cdb.exe"
    if (Test-Path $local) { return $local }
    $pkg = Get-ChildItem "C:\Program Files\WindowsApps" -Recurse -Filter "cdb.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "amd64" } |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $pkg) { return $null }
    $dir = Join-Path $env:TEMP "nhi1-tools\cdb"
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    Copy-Item (Join-Path (Split-Path -Parent $pkg) "*") $dir -Force -ErrorAction SilentlyContinue
    if (Test-Path (Join-Path $dir "cdb.exe")) { return (Join-Path $dir "cdb.exe") }
    return $pkg
}

function Get-ThreadCensus([int]$procId, [string]$path) {
    try {
        $pr = Get-Process -Id $procId -ErrorAction Stop
        $mods = @()
        foreach ($m in $pr.Modules) {
            $mods += [pscustomobject]@{ Name = $m.ModuleName
                                         Base = [int64]$m.BaseAddress.ToInt64()
                                         Size = [int64]$m.ModuleMemorySize }
        }
        $census = @("pid=$procId threads=$($pr.Threads.Count) modules=$($mods.Count)",
                    "editorBridge=" + [bool]($mods | Where-Object { $_.Name -match "EditorBridge" }),
                    "opengl32=" + [bool]($mods | Where-Object { $_.Name -match "opengl32|gdi32|d3d11|dxgi" }),
                    "asan=" + [bool]($mods | Where-Object { $_.Name -match "clang_rt|asan" }),
                    "", "tid`tstate`twait`towner-module`tstart-address")
        foreach ($t in $pr.Threads) {
            $addr = [int64]$t.StartAddress.ToInt64()
            $owner = "?"
            foreach ($m in $mods) {
                if ($addr -ge $m.Base -and $addr -lt ($m.Base + $m.Size)) { $owner = $m.Name; break }
            }
            $census += ("{0}`t{1}`t{2}`t{3}`t0x{4:X}" -f $t.Id, $t.ThreadState, $t.WaitReason, $owner, $addr)
        }
        Set-Content -Path $path -Value $census -Encoding utf8
        return $census
    } catch {
        return @("census failed: " + $_.Exception.Message)
    }
}

# 解析 probe 最后一行：DONE rc=<code> total=<ms>ms
function Get-Done([string]$outFile) {
    if (-not (Test-Path $outFile)) { return $null }
    $line = Select-String -Path $outFile -Pattern "DONE rc=" -ErrorAction SilentlyContinue |
        Select-Object -Last 1
    if (-not $line) { return $null }
    $m = [regex]::Match($line.Line, "DONE rc=(-?\d+) total=([\d.]+)ms")
    if (-not $m.Success) { return $null }
    return [pscustomobject]@{ Rc = [int]$m.Groups[1].Value; TotalMs = [double]$m.Groups[2].Value }
}

$cdb = if ($AttachCdb) { Resolve-Cdb } else { $null }
Write-Log "NHI-1 native probe: scenarios=$($all -join ',') iterations=$Iterations watchdog=${WatchdogSec}s stall=${StallSec}s mode=$Mode"
Write-Log "probe: $probePath"
Write-Log ("cdb: {0}" -f $(if ($cdb) { $cdb } else { "(not attached)" }))
$csvHeader = "scenario,iteration,pid,verdict,rc,wall_ms,probe_total_ms,exit_phase_ms,threads,cpu_s,ws_mb,cdb,stack_file,dump_file"
Set-Content -Path $csv -Value $csvHeader -Encoding utf8
$run = 0
$stopReason = ""

# 批次开始前清掉上一轮可能残留的 probe 进程（它们会锁住 DLL 并污染统计）
$leftovers = Get-Process -Name "nhi1_native_probe" -ErrorAction SilentlyContinue
if ($leftovers) {
    Write-Log ("cleanup: {0} leftover probe process(es) from earlier runs: {1}" -f
        $leftovers.Count, (($leftovers | ForEach-Object { "$($_.Id)(started $($_.StartTime))" }) -join ", "))
    foreach ($lp in $leftovers) { try { $lp.Kill() } catch { } }
}

$summary = @{}
$findings = @{}
$swAll = [System.Diagnostics.Stopwatch]::StartNew()

foreach ($scenario in $all) {
    $ok = 0; $unexpected = 0; $bad = 0
    $exitPhases = @()

    for ($i = 1; ($Iterations -le 0) -or ($i -le $Iterations); $i++) {
        $run++
        if ($Iterations -le 0) {
            if (($swAll.Elapsed.TotalMinutes) -ge $MaxMinutes) {
                $stopReason = "time budget reached ($MaxMinutes min, runs=$run)"
                Write-Log "STOP: $stopReason"
                break
            }
            if ($i % 50 -eq 0) { Write-Log ("progress: {0} runs, {1:N1} min elapsed, lastExitPhaseMax={2}ms" -f $run, $swAll.Elapsed.TotalMinutes, $(if ($exitPhases.Count) { ($exitPhases | Measure-Object -Maximum).Maximum } else { -1 })) }
        }
        foreach ($k in $inject.Keys) {
            if ($inject[$k]) { [System.Environment]::SetEnvironmentVariable($inject[$k], $null, "Process") }
        }
        if ($inject[$scenario]) { [System.Environment]::SetEnvironmentVariable($inject[$scenario], "1", "Process") }

        if ($Mode -eq "reload") {
            # 针对性 stressor：让 18MB EditorBridge.dll 每次都是"新文件"，
            # 逼 loader / AV 重新扫描（首次观测到 hang 的批次紧跟在 cmake 拷贝之后）。
            # DLL 被仍存活的进程映射时会 Copy 失败（IOException）——记录后继续，不中断批次。
            try {
                Copy-Item (Join-Path $repoRoot "build\bridge\Debug\EditorBridge.dll") `
                          (Join-Path (Split-Path -Parent $probePath) "EditorBridge.dll") -Force
            } catch {
                if ($i -eq 1 -or $i % 10 -eq 0) {
                    Write-Log "             (reload stressor skipped: $($_.Exception.Message.Split('.')[0]))"
                }
            }
        }

        $out = Join-Path $OutDir "nhi14-$scenario-$i.out"
        $err = Join-Path $OutDir "nhi14-$scenario-$i.err"
        Remove-Item $out, $err -ErrorAction SilentlyContinue
        $p = Start-Process -FilePath $probePath -ArgumentList $scenario -WorkingDirectory $repoRoot `
            -NoNewWindow -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
        $procId = $p.Id
        $swRun = [System.Diagnostics.Stopwatch]::StartNew()

        # ── 轮询：DONE-then-stall detector ────────────────────────────────
        $doneAt = $null
        $verdict = "exited"
        while ($true) {
            if ($p.HasExited) { break }
            if ($null -eq $doneAt) {
                $d = Get-Done $out
                if ($d) { $doneAt = Get-Date }
            }
            if ($null -ne $doneAt -and ((Get-Date) - $doneAt).TotalSeconds -ge $StallSec) {
                $verdict = "STALL"; break
            }
            if ($swRun.Elapsed.TotalSeconds -ge $WatchdogSec) { $verdict = "HANG"; break }
            Start-Sleep -Milliseconds 100
        }

        $wall = [math]::Round($swRun.Elapsed.TotalMilliseconds)
        $done = Get-Done $out
        $inner = if ($done) { $done.TotalMs } else { -1 }
        $rc = if ($done) { $done.Rc } else { -1 }
        $exitPhase = if ($inner -ge 0) { [math]::Round($wall - $inner) } else { -1 }

        if ($verdict -eq "exited") {
            if ($exitPhase -ge 0) { $exitPhases += $exitPhase }
            if ($rc -eq 0) {
                $ok++
                Write-Log ("{0,-16} #{1,3} pid={2,-6} rc=0 wall={3,6}ms probe={4,7}ms exitPhase={5,7}ms" -f $scenario, $i, $procId, $wall, $inner, $exitPhase)
            } else {
                $unexpected++
                Write-Log ("{0,-16} #{1,3} pid={2,-6} UNEXPECTED rc={3}" -f $scenario, $i, $procId, $rc)
            }
            Add-Content -Path $csv -Value "$scenario,$i,$procId,exited,$rc,$wall,$inner,$exitPhase,,,,," -Encoding utf8
            try { $p.Dispose() } catch { }
            continue
        }

        # ── STALL / HANG：先取证，再回收 ────────────────────────────────
        $bad++
        $pr = $null
        try { $pr = Get-Process -Id $procId -ErrorAction Stop } catch { }
        $threads = if ($pr) { $pr.Threads.Count } else { 0 }
        $cpu = if ($pr) { [math]::Round($pr.CPU, 2) } else { 0 }
        $ws = if ($pr) { [math]::Round($pr.WorkingSet64 / 1MB, 1) } else { 0 }
        Write-Log ("{0,-16} #{1,3} pid={2,-6} FINDING {3} wall={4}ms probe={5}ms exitPhase={6}ms threads={7} cpu={8}s ws={9}MB" -f $scenario, $i, $procId, $verdict, $wall, $inner, $exitPhase, $threads, $cpu, $ws)
        $findings += "$scenario#$i pid=$procId verdict=$verdict threads=$threads cpu=$cpu exitPhase=${exitPhase}ms"

        $censusPath = Join-Path $OutDir "nhi14-$verdict-$scenario-$i-$procId.threads.txt"
        $census = Get-ThreadCensus -procId $procId -path $censusPath
        $census | Select-Object -First 5 | ForEach-Object { Write-Log "             $_" }

        $stackFile = ""; $dumpFile = ""
        if ($cdb) {
            $stackFile = Join-Path $OutDir "nhi14-$verdict-$scenario-$i-$procId.cdb.txt"
            if (-not $NoDump) { $dumpFile = Join-Path $OutDir "nhi14-$verdict-$scenario-$i-$procId.dmp" }
            $cmd = "~*k; ~*kv"
            if ($dumpFile) { $cmd += "; .dump /ma `"$dumpFile`"" }
            $cmd += "; q"
            $sym = "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$repoRoot\build\bridge\Debug"
            # 注意：cdb 的 -c 参数必须作为**单个**引号包裹的字符串传入；
            # 用数组传参会让 PowerShell 把 "~*k; ~*kv; q" 拆散（cdb 报 0n2 / CommandLine 错乱）。
            $cdbArgs = "-p $procId -y `"$sym`" -c `"$cmd`""
            $cdbProc = Start-Process -FilePath $cdb -ArgumentList $cdbArgs `
                -NoNewWindow -PassThru -RedirectStandardOutput "$stackFile.txt" -RedirectStandardError "$stackFile.err"
            $attachStart = Get-Date
            $attached = $cdbProc.WaitForExit($CdbTimeoutSec * 1000)
            if (-not $attached) { try { $cdbProc.Kill() } catch { } }
            $dumpNote = if ($dumpFile -and (Test-Path $dumpFile)) { "{0}MB" -f [math]::Round((Get-Item $dumpFile).Length / 1MB, 1) } else { "(none)" }
            Write-Log ("             cdb attach={0} in {1:N1}s stacks={2}.txt dump={3}" -f
                $(if ($attached) { "OK" } else { "TIMEOUT" }), ((Get-Date) - $attachStart).TotalSeconds, $stackFile, $dumpNote)
            $stackFile = "$stackFile.txt"
        }

        # 判定纪律：
        #   attach 成功 -> 现场已冻结（栈 + dump），可以回收；
        #   attach 失败 -> **不主动破坏现场**，保留活体进程并停止批次（capture failure != 无问题）。
        if ($stackFile -and (Test-Path "$stackFile.txt") -and (Get-Item "$stackFile.txt").Length -gt 0) {
            try { $p.Kill() } catch { }
            try { $p.WaitForExit(15000) | Out-Null } catch { }
        } else {
            Write-Log "             CAPTURE FAILURE: 进程保持存活（pid=$procId），批次停止以便人工/重试 attach"
            $stopReason = "capture failure on live process pid=$procId"
        }
        if ($stopReason -eq "") { $stopReason = "finding captured ($verdict)" }
        if (-not $ContinueAfterFinding) {
            Write-Log "STOP: $stopReason"
            break
        }
        Add-Content -Path $csv -Value "$scenario,$i,$procId,$verdict,$rc,$wall,$inner,$exitPhase,$threads,$cpu,$ws,$(if ($stackFile) { 'cdb' } else { 'no-cdb' }),$stackFile,$dumpFile" -Encoding utf8
    }

    if ($stopReason) { break }
    $summary[$scenario] = [pscustomobject]@{ Expected = $ok; Unexpected = $unexpected; Bad = $bad
                                              ExitPhaseMax = if ($exitPhases.Count) { ($exitPhases | Measure-Object -Maximum).Maximum } else { -1 }
                                              ExitPhaseAvg = if ($exitPhases.Count) { [math]::Round(($exitPhases | Measure-Object -Average).Average) } else { -1 } }
    Write-Log ("{0,-16} summary: expected={1} unexpected={2} stallOrHang={3} exitPhase avg={4}ms max={5}ms" -f
        $scenario, $ok, $unexpected, $bad, $summary[$scenario].ExitPhaseAvg, $summary[$scenario].ExitPhaseMax)
}

if ($stopReason -eq "") { $stopReason = "batch finished normally (runs=$run)" }
Write-Log "stop reason: $stopReason"
Write-Log "---- matrix ----"
foreach ($k in $summary.Keys) {
    $s = $summary[$k]
    Write-Log ("{0,-16} expected={1,3} unexpected={2,3} stallOrHang={3,3} exitPhaseAvg={4,6}ms max={5,7}ms" -f $k, $s.Expected, $s.Unexpected, $s.Bad, $s.ExitPhaseAvg, $s.ExitPhaseMax)
}
Write-Log ("elapsed={0}s  findings={1}" -f [math]::Round($swAll.Elapsed.TotalSeconds, 1), $findings.Count)
# 逐次证据落盘（per-run: pid / verdict / rc / wall / probe total / exit-phase / cdb / dump 路径）
if ($rows.Count -gt 0) { $rows | Out-File -FilePath $csv -Append -Encoding utf8 }
Write-Log "log: $log"
Write-Log "csv: $csv"

# 退出码：2 = 至少一个 FINDING（停滞/挂死）。绝不把 timeout 变成成功。
if ($findings.Count -gt 0) { exit 2 } else { exit 0 }
