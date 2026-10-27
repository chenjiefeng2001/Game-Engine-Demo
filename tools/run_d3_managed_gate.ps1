<#
.SYNOPSIS
    HRC-3 D3：D3-Managed gate runner（M8-A 前置 + M1~M7 / M8-B）。

.DESCRIPTION
    分层执行，顺序不可交换：

        M8-A  artifact prerequisite   —— SHA256(build/bridge/<Config>) == SHA256(bin/<Config>)
                                          fail-closed；不 copy、不 retry、不自动修复
        M1~M7 D3 semantic / ABI evidence   —— 由 AvaloniaEditor.exe --d3managed 执行
        M8-B  loaded ABI identity          —— 同上进程内读取实际加载的 native 版本

    M8-A 未通过时，M1~M7 与 M8-B 一律标记 NOT RUN：
    这是 **artifact prerequisite failure**，**不是** "D3 v1.3 broken"。

    本 runner 刻意**不**修改 AvaloniaEditor.csproj 的 staging 语义，
    也**不**修改 tools/run_p2_gates.ps1。Release 的 staging defect 属于
    独立的 build/deployment hygiene slice，此处只如实暴露。

.EXAMPLE
    pwsh -NoProfile -ExecutionPolicy Bypass -File tools\run_d3_managed_gate.ps1
    pwsh -NoProfile -ExecutionPolicy Bypass -File tools\run_d3_managed_gate.ps1 -Config Release
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    # 自增超时；不 retry、不 sleep-retry
    [int]$TimeoutSeconds = 600
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$csproj    = Join-Path $repoRoot "editor_avalonia\AvaloniaEditor.csproj"
$exe       = Join-Path $repoRoot "editor_avalonia\bin\$Config\net9.0\AvaloniaEditor.exe"
$nativeDll = Join-Path $repoRoot "build\bridge\$Config\EditorBridge.dll"
$stagedDll = Join-Path $repoRoot "editor_avalonia\bin\$Config\net9.0\EditorBridge.dll"

function Say($m) { Write-Host $m }

Say "[d3-gate] ===== D3-Managed gate : $Config ====="

# ─────────────────────────────────────────────────────────────────────────────
# M8-A : artifact prerequisite（fail-closed）
# ─────────────────────────────────────────────────────────────────────────────
$m8aOk = $true

if (-not (Test-Path $nativeDll)) {
    Say "[D3-M8A] ARTIFACT PREREQUISITE FAILURE: native bridge missing: $nativeDll"
    Say "[d3-gate]   build it: cmake --build build --config $Config --target EditorBridge"
    $m8aOk = $false
}
if (-not (Test-Path $stagedDll)) {
    Say "[D3-M8A] ARTIFACT PREREQUISITE FAILURE: staged DLL missing: $stagedDll"
    $m8aOk = $false
}

if ($m8aOk) {
    $hNative = (Get-FileHash -LiteralPath $nativeDll -Algorithm SHA256).Hash
    $hStaged = (Get-FileHash -LiteralPath $stagedDll -Algorithm SHA256).Hash
    Say "[D3-M8A] build  SHA256 = $hNative"
    Say "[D3-M8A] staged SHA256 = $hStaged"
    if ($hNative -ne $hStaged) {
        # 典型成因：csproj 硬编码 ..\build\bridge\Debug\，Release 会把 Debug 的
        # DLL 部署过去（或什么都没部署）。这里只报告，不修复。
        Say "[D3-M8A] ARTIFACT PREREQUISITE FAILURE: staged DLL is not the $Config build"
        Say "[d3-gate]   (expected $nativeDll to be deployed verbatim)"
        Say "[d3-gate]   this is a build/deployment hygiene defect, NOT a D3 contract failure"
        $m8aOk = $false
    }
}

if (-not $m8aOk) {
    Say "[D3-M1..M7] NOT RUN (blocked by artifact prerequisite)"
    Say "[D3-M8B]    NOT RUN (blocked by artifact prerequisite)"
    Say "[d3-gate] RESULT: BLOCKED / ARTIFACT PREREQUISITE FAILURE ($Config)"
    exit 2
}

Say "[D3-M8A] artifact identity OK"

# ─────────────────────────────────────────────────────────────────────────────
# M1~M7 + M8-B
# ─────────────────────────────────────────────────────────────────────────────
if (-not (Test-Path $exe)) {
    Say "[d3-gate] FAIL: managed host not found: $exe"
    Say "[d3-gate]   build it: dotnet build editor_avalonia\AvaloniaEditor.csproj -c $Config -t:Rebuild"
    exit 1
}

# Rebuild 而非增量：csproj 的 PreserveNewest 在增量 build 下不会重新拷贝，
# 会让 M8-A 因陈旧 artifact 而误判。这是**调用方式**，不改变 staging 语义。
Say "[d3-gate] building managed host ($Config, -t:Rebuild) ..."
$buildOut = & dotnet build $csproj -c $Config -t:Rebuild -v m 2>&1 | Out-String
if ([regex]::Matches($buildOut, 'error CS\d+').Count -gt 0) {
    Say "[d3-gate] FAIL: managed build errors"
    ($buildOut -split "`r?`n" | Where-Object { $_ -match 'error CS' } | Select-Object -First 8) |
        ForEach-Object { Say "  $($_.Trim())" }
    exit 1
}

# Rebuild 之后必须**重新**验证 M8-A：build 可能刚刚改写了 staged artifact。
$hNative2 = (Get-FileHash -LiteralPath $nativeDll -Algorithm SHA256).Hash
$hStaged2 = (Get-FileHash -LiteralPath $stagedDll -Algorithm SHA256).Hash
if ($hNative2 -ne $hStaged2) {
    Say "[D3-M8A] ARTIFACT PREREQUISITE FAILURE (post-build): staged DLL diverged after build"
    Say "[D3-M8A]   build  SHA256 = $hNative2"
    Say "[D3-M8A]   staged SHA256 = $hStaged2"
    Say "[D3-M1..M7] NOT RUN (blocked by artifact prerequisite)"
    Say "[D3-M8B]    NOT RUN (blocked by artifact prerequisite)"
    Say "[d3-gate] RESULT: BLOCKED / ARTIFACT PREREQUISITE FAILURE ($Config)"
    exit 2
}

Say "[d3-gate] running managed gate (--d3managed) ..."
$env:ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE = "1"

$gateLog = Join-Path $env:TEMP "d3_managed_gate_$Config.log"
if (Test-Path $gateLog) { Remove-Item $gateLog -Force }

$proc = Start-Process -FilePath $exe `
    -ArgumentList '--d3managed' `
    -WorkingDirectory $repoRoot `
    -NoNewWindow -PassThru `
    -RedirectStandardOutput $gateLog `
    -RedirectStandardError "$gateLog.err"

# gate 自己打印的成功标记。用于区分"断言失败"与"断言全过但进程没退出"。
$greenMarker = '[D3-MANAGED GATE] M1-M7 + M8-B ALL GREEN'

function Get-GateLogText {
    if (Test-Path $gateLog) { return (Get-Content $gateLog -Raw) }
    return ''
}

$finished = $proc.WaitForExit($TimeoutSeconds * 1000)
if (-not $finished) {
    Say "[d3-gate] process did not terminate within ${TimeoutSeconds}s; killing"
    try { $proc.Kill($true) } catch { }
    try { $proc.WaitForExit(15000) | Out-Null } catch { }
}

foreach ($f in @($gateLog, "$gateLog.err")) {
    if (Test-Path $f) { Get-Content $f | ForEach-Object { Say "  $_" } }
}

# ── 三态分类（刻意不把 linger 洗成绿色）──
#   ASSERTIONS GREEN  + PROCESS EXIT GREEN     -> exit 0
#   ASSERTIONS GREEN  + PROCESS EXIT LINGER    -> exit 3（整体仍非绿色）
#   ASSERTIONS RED / INDETERMINATE             -> exit 1
$logText = Get-GateLogText
$assertionsGreen = $logText.Contains($greenMarker)

if (-not $finished) {
    if ($assertionsGreen) {
        Say "[d3-gate] assertions : GREEN  (M1-M7 + M8-B all reported green)"
        Say "[d3-gate] process   : LINGER (did not exit within ${TimeoutSeconds}s)"
        Say "[d3-gate] RESULT: ASSERTIONS GREEN / PROCESS EXIT LINGER / verdict=FAIL ($Config)"
        Say "[d3-gate]   the gate did its work; shutdown is unresolved. NOT a D3 transport verdict."
        exit 3
    }
    Say "[d3-gate] assertions : RED or INDETERMINATE (no success marker in log)"
    Say "[d3-gate] process   : TIMEOUT after ${TimeoutSeconds}s"
    Say "[d3-gate] RESULT: ASSERTIONS RED / TIMEOUT / verdict=FAIL ($Config)"
    exit 1
}

$code = $proc.ExitCode
Say "[d3-gate] process exit code = $code"

if ($code -eq 0 -and $assertionsGreen) {
    Say "[d3-gate] assertions : GREEN"
    Say "[d3-gate] process   : EXIT GREEN"
    Say "[d3-gate] RESULT: ASSERTIONS GREEN / PROCESS EXIT GREEN ($Config)  [M8-A + M1-M7 + M8-B]"
    exit 0
}

if ($assertionsGreen) {
    # 进程退出了但非 0：断言说绿、可信度不足，仍按非绿色处理。
    Say "[d3-gate] assertions : GREEN (marker present) but exit code $code - not trusted"
    Say "[d3-gate] RESULT: ASSERTIONS GREEN / PROCESS EXIT ANOMALY / verdict=FAIL ($Config)"
    exit 1
}

Say "[d3-gate] assertions : RED"
Say "[d3-gate] RESULT: ASSERTIONS RED / verdict=FAIL ($Config)  [M8-A passed; M1-M7 / M8-B produced a verdict]"
exit 1
