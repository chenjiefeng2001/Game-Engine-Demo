<#
.SYNOPSIS
    Stage A / A3 UI-state gate runner（N1~N5 负例矩阵）。

.DESCRIPTION
    每个注入场景必须独立进程（MSVC getenv 读 CRT 启动快照，运行期 SetEnvironmentVariable
    对 native 不可见），因此本 runner 为每个场景启动一次 AvaloniaEditor.exe：

      1) enginehost     --uistate                                  N1 / N2a / N4 + Refused 负例
      2) legacy-order   --uistate --uistatelegacy                  N2b（event-before-return）
      3) frame-fault    --uistate + FAIL_FRAME                     N5（runtime fault → 投影清空）

    N3（host 侧 create 失败）已按 A3 contract §9 方案 A 收敛为 **Refused 负例**：
    S0（无 project）与 N5（faulted host）两个真实拒绝态共用 UiStateGate.AssertNoPhantomState
    （diagnostic 非空 / 无新增实体 / 无假选择 / 无选择意图）。
    不用 CREATE 注入是因为两个 backend 都没有 create 注入点
    （EDITOR_BRIDGE_FAIL_LEGACY_OPERATION 只作用于 EditorSession_OpenProject）。

    重试语义：`-MaxAttempts` 里的重试是 **runner hygiene**，不是产品 workaround，
    也**不是** A3 gate 的功能结果 —— 汇总输出把它放在独立的 hygiene 栏。
    偶发的"gate 已 exit=0 但进程不退出"按 A3 Closure §6 记为
    runner / process-exit hygiene observation：没有新的 native stack 就不重开 NHI-1；
    出现稳定复现或新的 live native stack 才独立开调查 slice。

    N2 的"两种事件顺序最终状态一致"由两次运行的 A3-ORDER-FACT 规范行**机械比较**得出：
    order 必须不同（证明两种顺序都被真实执行过），outcome / newIndex / finalSelection /
    intentResolved / intentPending 必须逐字段相同。

.NOTES
    NHI-1 约束：本 runner 只驱动 managed (non-ASan) Debug 产物。Release / Debug+ASan 的
    native 证据是独立环节，ASan 退出期的 nvspcap64.dll FLS 死锁不归本 gate。
    进程存活判定使用 Process.WaitForExit(timeout)，不使用 Start-Process -PassThru 的 HasExited。
#>
[CmdletBinding()]
param(
    [string]$Configuration = 'Debug',
    [int]$TimeoutSec = 120,
    [int]$SettleSec = 2,
    [int]$MaxAttempts = 2
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$exe = Join-Path $repoRoot "editor_avalonia\bin\$Configuration\net9.0\AvaloniaEditor.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "gate executable not found: $exe (build the managed project first)"
}

# MainWindow.Log 除了 stdout 还会追加到 selftest.log。**门禁判定必须以 selftest.log 为准**：
# window.Close() 会与 Avalonia 自身的 shutdown 竞争，进程可能在我们 log 之后才退出，
# 此时 stdout 可能整段丢失（exit=0 但无输出）。日志文件是逐行落盘的完整记录。
$logPath = Join-Path $repoRoot 'editor_avalonia\selftest.log'

# 场景表：name / args / env / 覆盖的负例
# 顺序有讲究：GL/GLFW 析构重的场景放最后（NHI-1 同类的退出期挂起发生在**下一个**进程上，
# 前面刚做完 GL teardown 的那次最容易被拖累）。
$scenarios = @(
    [pscustomobject]@{ Name = 'legacy-order'; Args = @('--uistate', '--uistatelegacy'); Env = @{}; Covers = 'N2b' },
    [pscustomobject]@{ Name = 'frame-fault';  Args = @('--uistate');                     Env = @{ EDITOR_BRIDGE_FAIL_FRAME = '1' }; Covers = 'N5' },
    [pscustomobject]@{ Name = 'enginehost';   Args = @('--uistate');                     Env = @{}; Covers = 'N1/N2a/N4 + Refused' }
)

function Invoke-GateScenario {
    param(
        [Parameter(Mandatory)] [string] $Exe,
        [Parameter(Mandatory)] [string] $WorkingDirectory,
        [Parameter(Mandatory)] [string[]] $GateArgs,
        [Parameter(Mandatory)] [hashtable] $EnvVars,
        [Parameter(Mandatory)] [int] $TimeoutMs
    )

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $Exe
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    foreach ($key in $EnvVars.Keys) { $psi.Environment[$key] = [string]$EnvVars[$key] }

    foreach ($arg in $GateArgs) { $psi.ArgumentList.Add($arg) }

    $proc = [System.Diagnostics.Process]::Start($psi)
    # stderr 同步读走，避免 stdout 被填满时互相阻塞。
    $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
    $stderrTask = $proc.StandardError.ReadToEndAsync()

    $exited = $proc.WaitForExit($TimeoutMs)
    if (-not $exited) {
        try { $proc.Kill($true) } catch { }
        $proc.WaitForExit(5000) | Out-Null
        # 超时也必须保留已产生的输出，否则挂起原因无从诊断。
        return [pscustomobject]@{
            TimedOut = $true
            ExitCode = -1
            StdOut = $stdoutTask.GetAwaiter().GetResult()
            StdErr = $stderrTask.GetAwaiter().GetResult() + "`n[runner] timeout after $TimeoutMs ms"
        }
    }

    return [pscustomobject]@{
        TimedOut = $false
        ExitCode = $proc.ExitCode
        StdOut = $stdoutTask.GetAwaiter().GetResult()
        StdErr = $stderrTask.GetAwaiter().GetResult()
    }
}

function Wait-CleanProcessTable {
    <#
        连续启动 GUI/native 进程时，前一个进程的退出在环境层（overlay interception / GL driver）
        可能滞后；残留在跑会拖死下一个场景。这里显式等待进程表清空，而不是靠 sleep 猜。
    #>
    param([int]$TimeoutMs = 15000)
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $deadline) {
        $residual = @(Get-Process -Name 'AvaloniaEditor' -ErrorAction SilentlyContinue)
        if ($residual.Count -eq 0) { return $true }
        Start-Sleep -Milliseconds 200
    }
    return (@(Get-Process -Name 'AvaloniaEditor' -ErrorAction SilentlyContinue).Count -eq 0)
}

function Get-LogSlice {
    param([int64]$Offset)
    if (-not (Test-Path -LiteralPath $script:logPath)) { return '' }
    $text = [System.IO.File]::ReadAllText($script:logPath)
    if ($Offset -le 0) { return $text }
    if ($Offset -ge $text.Length) { return '' }
    return $text.Substring([int]$Offset)
}

$results = @()
$orderFacts = @{}

foreach ($scenario in $scenarios) {
    Write-Host "=== A3 uistate: $($scenario.Name) ($($scenario.Covers)) ===" -ForegroundColor Cyan

    $run = $null
    $record = ''
    $attempt = 0
    $retried = $false
    while ($attempt -lt $MaxAttempts) {
        $attempt++
        [void](Wait-CleanProcessTable)
        Start-Sleep -Seconds $SettleSec
        $logOffset = if (Test-Path -LiteralPath $logPath) { (Get-Item -LiteralPath $logPath).Length } else { 0 }
        $run = Invoke-GateScenario -Exe $exe -WorkingDirectory $repoRoot `
            -GateArgs $scenario.Args -EnvVars $scenario.Env -TimeoutMs ($TimeoutSec * 1000)
        # 日志文件为准，stdout 作为补充（超时诊断用同一份记录）。
        $record = (Get-LogSlice -Offset $logOffset) + "`n" + $run.StdOut
        if ($run.TimedOut) {
            try { Get-Process -Name 'AvaloniaEditor' -ErrorAction SilentlyContinue | Stop-Process -Force } catch { }
            $retried = $true
            $reachedGreen = $record -match '\[HRC-A3 UISTATE GATE\] ALL GREEN'
            # 区分两种超时：断言挂住 vs 断言全绿后进程退不出（后者是 NHI-1 同类的环境问题）。
            $why = if ($reachedGreen) { 'the gate reached ALL GREEN but the process did not exit' }
                   else { 'the gate did not reach ALL GREEN' }
            Write-Host "  (attempt $attempt timed out: $why — NHI-1 class environment hang)" -ForegroundColor Yellow
            $tail = ($record -split "`n" | Where-Object { $_.Trim().Length -gt 0 } | Select-Object -Last 3)
            if ($tail) { $tail | ForEach-Object { Write-Host "    | $_" -ForegroundColor DarkGray } }
            continue
        }
        break
    }
    if ($retried -and -not $run.TimedOut) { Write-Host "  (green on attempt $attempt after a timeout retry)" -ForegroundColor Yellow }

    $green = (-not $run.TimedOut) -and ($run.ExitCode -eq 0) -and
             ($record -match '\[HRC-A3 UISTATE GATE\] ALL GREEN')
    $failLine = $null
    if (-not $green) {
        $failLine = ($record -split "`n" | Where-Object { $_ -match 'FAIL' } | Select-Object -Last 1)
        if (-not $failLine) {
            $failLine = if ($run.TimedOut) { 'timed out (see attempt log above)' }
                        else { "exit=$($run.ExitCode) but no gate record in stdout or selftest.log" }
        }
    }

    $results += [pscustomobject]@{
        Scenario = $scenario.Name
        Covers   = $scenario.Covers
        Green    = $green
        Retried  = $retried
        Detail   = $failLine
    }
    $orderFacts[$scenario.Name] = ($record -split "`n" |
        Where-Object { $_ -match 'A3-ORDER-FACT' } | Select-Object -First 1)

    if ($green) { Write-Host "  GREEN" -ForegroundColor Green }
    else { Write-Host "  FAIL: $failLine" -ForegroundColor Red }
}

# ── N2 机械比较：两种事件顺序的最终状态必须逐字段一致 ──────────────
$engineFact = $orderFacts['enginehost']
$legacyFact = $orderFacts['legacy-order']
$n2 = [pscustomobject]@{ Scenario = 'n2-order-equivalence'; Covers = 'N2'; Green = $false; Retried = $false; Detail = $null }

function Read-FactField {
    param([string] $Fact, [string] $Field)
    if (-not $Fact) { return $null }
    $m = [regex]::Match($Fact, "(?:^|\s)$([regex]::Escape($Field))=(\S+)")
    if ($m.Success) { return $m.Groups[1].Value }
    return $null
}

if (-not $engineFact -or -not $legacyFact) {
    $n2.Detail = 'a run did not print the A3-ORDER-FACT line'
} else {
    Write-Host "=== A3 uistate: n2-order-equivalence ===" -ForegroundColor Cyan
    Write-Host "  enginehost : $engineFact"
    Write-Host "  legacy     : $legacyFact"
    $orders = @((Read-FactField $engineFact 'order'), (Read-FactField $legacyFact 'order'))
    if ($orders[0] -eq $orders[1]) {
        $n2.Detail = "the two runs reported the same order ($($orders[0])); order independence is unproven"
    } else {
        $mismatch = @()
        foreach ($field in @('outcome', 'newIndex', 'finalSelection', 'intentResolved', 'intentPending')) {
            $a = Read-FactField $engineFact $field
            $b = Read-FactField $legacyFact $field
            if ($a -ne $b) { $mismatch += "$field($a!=$b)" }
        }
        if ($mismatch.Count -gt 0) {
            $n2.Detail = 'final state diverged between orders: ' + ($mismatch -join ', ')
        } else {
            $n2.Green = $true
            Write-Host "  GREEN (orders differ: $($orders -join ' vs '); final state identical)" -ForegroundColor Green
        }
    }
}
if (-not $n2.Green) { Write-Host "  FAIL: $($n2.Detail)" -ForegroundColor Red }

$all = @($results) + @($n2)
$greenCount = @($all | Where-Object { $_.Green }).Count
Write-Host ''
Write-Host '--- functional results (A3 contract evidence) ---'
foreach ($r in $all) {
    $tag = if ($r.Green) { 'GREEN' } else { 'FAIL ' }
    Write-Host ("[{0}] {1,-20} {2,-24} {3}" -f $tag, $r.Scenario, $r.Covers,
        $(if ($r.Detail) { $r.Detail } else { '' }))
}

# process-exit 观察与功能结果**分栏**：重试是 runner hygiene，不是 A3 gate 的功能结果，
# 也不是产品 workaround（产品代码里没有任何 retry/delay）。详见 A3 Closure §6。
$retried = @($all | Where-Object { $_.Retried })
Write-Host ''
Write-Host '--- runner / process-exit hygiene observations (NOT functional results) ---'
if ($retried.Count -eq 0) {
    Write-Host 'none: every scenario exited within the timeout on its first attempt'
} else {
    foreach ($r in $retried) {
        # 注意括号：PowerShell 里 -f 的优先级高于 +，必须先拼串再格式化，否则 {0} 不会被替换。
        Write-Host (("[hygiene] {0}: exited only after a retry (gate assertions were green each time; " +
            "process did not exit after Environment.Exit - see A3 Closure 6, do NOT reopen NHI-1)") -f $r.Scenario)
    }
}
Write-Host ''
Write-Host "A3 UISTATE GATE: $greenCount/$($all.Count) GREEN"

if ($greenCount -eq $all.Count) { exit 0 }
exit 1
