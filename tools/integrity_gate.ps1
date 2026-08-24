# ============================================================
# integrity_gate.ps1 - Evidence Integrity Gate
#
# Gates (see docs/Evidence-Baseline.md):
#   I1 Test Registration   : every tests/**/*Test.cpp must be in CMakeLists
#   I2 Test Count Integrity: actual gtest discovery must match baseline
#   I3 Dogfood Execution   : every DF smoke must be backed by an executed test
#
# Execution model (v2 - heartbeat):
#   Every long-running command (build / test run) is started as a
#   background process with redirected output. The gate then polls the
#   growing log file: new gtest/msbuild marker lines are echoed as a
#   live heartbeat, and silence beyond -HeartbeatTimeoutSec declares a
#   hang (process tree killed, gate fails fast) instead of blocking
#   forever. A hard cap -MaxRunSec catches noisy infinite loops.
#   Commands still run strictly SEQUENTIALLY: suites share scratch
#   directories (gp01_scratch etc.) and must not overlap.
#
# Usage:
#   powershell -File tools/integrity_gate.ps1 [-SkipBuild]
#       [-HeartbeatTimeoutSec 240] [-MaxRunSec 1800] [-PollMs 1000]
# Exit code: 0 = all green; non-zero = failures
# ============================================================
param(
    [switch]$SkipBuild,
    [string]$BaselinePath = "docs/Evidence-Baseline.md",
    [string]$TestsDir = "tests",
    [string]$CMakeLists = "tests/CMakeLists.txt",
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [int]$HeartbeatTimeoutSec = 240,
    [int]$MaxRunSec = 1800,
    [int]$PollMs = 1000
)

$ErrorActionPreference = "Stop"
$env:ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE = "1"

$failures = New-Object System.Collections.Generic.List[string]
$excluded = @{}   # target -> reason (parsed from baseline)
$logRoot  = Join-Path (Get-Location) "logs\gate"
if (-not (Test-Path $logRoot)) { New-Item -ItemType Directory -Path $logRoot | Out-Null }

# ------------------------------------------------------------
# Heartbeat runner: start process -> tail its log as heartbeat
# Returns [pscustomobject] { ExitCode, Passed, Failed, Hung,
#                            Capped, Seconds, LastMark, LogPath }
# ------------------------------------------------------------
function Invoke-CommandWithHeartbeat {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [Parameter(Mandatory)][string]$LogPath
    )
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    Remove-Item $LogPath, "$LogPath.err" -ErrorAction SilentlyContinue

    # NOTE: PS5.1 Start-Process joins ArgumentList without re-quoting;
    # all arguments used here are space-free by construction.
    $p = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList `
         -RedirectStandardOutput $LogPath -RedirectStandardError "$LogPath.err" `
         -NoNewWindow -PassThru
    $null = $p.Handle   # cache handle NOW, else PS5.1 loses ExitCode after exit

    $offset   = [long]0
    $lastBeat = 0.0
    $lastMark = "(started)"
    $idleNextReport = 30.0
    $hung = $false; $capped = $false

    # incremental reader: decode only bytes appended since last poll
    $script:ReadNewChunk = {
        param($path, [ref]$off)
        if (-not (Test-Path $path)) { return $null }
        $fs = $null
        try {
            $fs = [System.IO.File]::Open($path, 'Open', 'Read', 'ReadWrite')
            if ($fs.Length -le $off.Value) { return $null }
            $fs.Seek($off.Value, 'Begin') | Out-Null
            $br = New-Object System.IO.BinaryReader($fs)
            $buf = $br.ReadBytes([int]($fs.Length - $off.Value))
            $off.Value = $fs.Length
            return [System.Text.Encoding]::UTF8.GetString($buf)
        } catch { return $null }
        finally { if ($fs) { $fs.Close() } }
    }

    while ($true) {
        if ($p.WaitForExit($PollMs)) { break }

        $text = & $script:ReadNewChunk $LogPath ([ref]$offset)
        $newMark = $null
        if ($text) {
            foreach ($line in ($text -split "`r?`n")) {
                if ($line -match '\[( RUN|       OK|  FAILED|==========)' -or
                    $line -match '\[\s+\d+\/\d+\]' -or
                    $line -match '(PASSED|FAILED)') {
                    $newMark = $line.Trim()
                }
            }
        }
        $now = $sw.Elapsed.TotalSeconds
        if ($newMark) {
            $lastBeat = $now
            $lastMark = $newMark
            Write-Host ("    [{0,6:n0}s] {1}" -f $now, $lastMark)
        }
        $idle = $now - $lastBeat
        if ($idle -ge $idleNextReport) {
            Write-Host ("    [{0,6:n0}s] ...waiting (no output for {1:n0}s)" -f $now, $idle)
            $idleNextReport += 30.0
        }

        if ($idle -gt $HeartbeatTimeoutSec) { $hung = $true; break }
        if ($now -gt $MaxRunSec)            { $capped = $true; break }
    }

    if ($hung -or $capped) {
        Write-Host ("    [{0,6:n0}s] KILLING process tree (pid={1})" -f `
            $sw.Elapsed.TotalSeconds, $p.Id)
        try { taskkill /PID $p.Id /T /F 2>$null | Out-Null } catch {}
        Start-Sleep -Milliseconds 500
    }

    # PS5.1: after WaitForExit(Int32) returns true, ExitCode is populated
    # only once the parameterless WaitForExit() has reaped the process.
    try {
        if (-not $p.HasExited) { $p.WaitForExit(3000) | Out-Null }
        if ($p.HasExited)      { $null = $p.WaitForExit() }
    } catch {}

    # flush whatever remained buffered before exit
    $tail = & $script:ReadNewChunk $LogPath ([ref]$offset)
    if ($tail) {
        foreach ($line in ($tail -split "`r?`n")) {
            if ($line -match '\[( RUN|       OK|  FAILED|==========)' -or
                $line -match '(PASSED|FAILED)') { $lastMark = $line.Trim() }
        }
    }

    $logText = ""
    if (Test-Path $LogPath) { $logText = Get-Content $LogPath -Raw -ErrorAction SilentlyContinue }
    $exitCode = if ($p.HasExited) { $p.ExitCode } else { $null }
    return [pscustomobject]@{
        ExitCode = $exitCode
        Passed   = ($logText -match '\[\s+PASSED\s+\]')
        Failed   = ($logText -match '\[\s+FAILED\s+\]')
        Hung     = $hung
        Capped   = $capped
        Seconds  = [int]$sw.Elapsed.TotalSeconds
        LastMark = $lastMark
        LogPath  = $LogPath
    }
}

function Show-FailureTail {
    param([hashtable]$r)
    Write-Host "      stdout tail:"
    Get-Content $r.LogPath -Tail 3 -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Host "        > $_" }
    if (Test-Path "$($r.LogPath).err") {
        Get-Content "$($r.LogPath).err" -TotalCount 2 -ErrorAction SilentlyContinue |
            ForEach-Object { Write-Host "        ! $_" }
    }
}

# --- Parse baseline ---
$baselineText = Get-Content $BaselinePath -Raw -Encoding UTF8

$inScope = @{}
foreach ($m in [regex]::Matches($baselineText, '\|\s*(test_\w+)\s*\|\s*(\d+)\s*\|')) {
    $inScope[$m.Groups[1].Value] = [int]$m.Groups[2].Value
}
foreach ($m in [regex]::Matches($baselineText, '\|\s*(test_\w+)\s*\|\s*EXCLUDED\s*\|\s*([^|]+)\|')) {
    $excluded[$m.Groups[1].Value] = $m.Groups[2].Value.Trim()
}
if ($inScope.Count -eq 0) { $failures.Add("I2: baseline parse failed (no rows)") }

# ============================================================
# I1 Test Registration
# ============================================================
Write-Host ""
Write-Host "=== I1 Test Registration ==="
$cmakeText = Get-Content $CMakeLists -Raw -Encoding UTF8
$allTestFiles = Get-ChildItem -Recurse $TestsDir -Filter "*Test.cpp" |
    ForEach-Object { $_.FullName.Substring((Get-Location).Path.Length + 1).Replace("\", "/") }

$i1ok = $true
foreach ($f in $allTestFiles) {
    # CMakeLists references sources relative to tests/ (e.g. test_core/math/X.cpp)
    $rel = $f -replace "^tests/", ""
    if ($cmakeText -notmatch [regex]::Escape($rel)) {
        $failures.Add("I1: UNREGISTERED test file: $f")
        Write-Host "  FAIL  $f (on disk but not in CMakeLists)"
        $i1ok = $false
    } else {
        Write-Host "  ok    $f"
    }
}
if ($i1ok) { Write-Host "I1 PASS: $($allTestFiles.Count) files all registered" }

# ============================================================
# I2 Test Count Integrity  (build via heartbeat runner)
# ============================================================
Write-Host ""
Write-Host "=== I2 Test Count Integrity ==="
if (-not $SkipBuild) {
    Write-Host "building in-scope targets (heartbeat mode)..."
    foreach ($t in ($inScope.Keys | Sort-Object)) {
        Write-Host "  build $t"
        $r = Invoke-CommandWithHeartbeat -FilePath "cmake" `
             -ArgumentList @("--build", $BuildDir, "--target", $t, "--config", $Config) `
             -LogPath (Join-Path $logRoot "build_$t.log")
        if ($r.ExitCode -ne 0) {
            $failures.Add("I2: build failed for $t (exit=$($r.ExitCode), see $($r.LogPath))")
            Write-Host "  FAIL  build $t"
            Show-FailureTail -r @{ LogPath = $r.LogPath }
        }
    }
}

$i2ok = $true
$total = 0
foreach ($t in ($inScope.Keys | Sort-Object)) {
    $exeObj = Get-ChildItem -Recurse $BuildDir -Filter "$t.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $exeObj) {
        $failures.Add("I2: $t exe not found (build failed?)")
        Write-Host "  FAIL  $t : exe missing"
        $i2ok = $false
        continue
    }
    # discovery is bounded and fast -- plain synchronous call is fine here
    $listOut = cmd /c "`"$($exeObj.FullName)`" --gtest_list_tests 2>nul"
    if ($LASTEXITCODE -ne 0) {
        $failures.Add("I2: $t --gtest_list_tests failed (exit=$LASTEXITCODE)")
        Write-Host "  FAIL  $t : discovery crashed"
        $i2ok = $false
        continue
    }
    # leaf test lines start with two-or-more spaces then non-space
    $count = ($listOut | Where-Object { $_ -match "^\s{2}\S" }).Count
    $expected = $inScope[$t]
    if ($count -ne $expected) {
        $failures.Add("I2: $t count mismatch - baseline=$expected actual=$count")
        Write-Host "  FAIL  $t : expected $expected, discovered $count (update baseline or fix tests)"
        $i2ok = $false
    } else {
        $total += $count
        $line = "  ok    " + $t.PadRight(16) + "$count tests"
        Write-Host $line
    }
}
foreach ($t in $excluded.Keys) {
    $line = "  excl  " + $t.PadRight(16) + "(" + $excluded[$t] + ")"
    Write-Host $line
}
if ($i2ok) { Write-Host "I2 PASS: in-scope total = $total" }

# ============================================================
# I3 Dogfood Execution  (test runs via heartbeat runner)
# ============================================================
Write-Host ""
Write-Host "=== I3 Dogfood Execution ==="
# Flat evidence table: DF | target | filter  (one row per evidence source)
$evidenceRows = @(
    "DF01|test_content|GameplayAPIV2.M001_EntityFind_ByName",
    "DF01|test_scripting|ScriptingGameplayTest.*",
    "DF02|test_content|ContentGolden.R13Golden_*",
    "DF02|test_content|DXGoldenGate.R13_Process*",
    "DF03|test_content|ContentDogfood.DF03_*",
    "DF04|test_content|DXGoldenGate.Full_Workflow_EmptyProject_To_PlayableGame",
    "DF05|test_content|Dogfood05.LoadAndRun",
    "DF06|test_content|Dogfood06.LoadAndRun",
    "DF07|test_content|Dogfood07.LoadRunRestart",
    "DF08|test_content|Dogfood08.*",
    "DF09|test_content|Dogfood09.*",
    "GP01|test_gp01|GP01.*"
)

$i3ok = $true
$dfStatus = @{}
foreach ($row in $evidenceRows) {
    $parts = $row -split "\|", 3
    $df = $parts[0]; $t = $parts[1]; $filter = $parts[2]
    $exeObj = Get-ChildItem -Recurse $BuildDir -Filter "$t.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $exeObj) {
        $failures.Add("I3: ${df} evidence target '$t' exe missing")
        Write-Host "  FAIL  ${df}: $t exe missing"
        $dfStatus[$df] = $false; $i3ok = $false; continue
    }
    Write-Host "  run   ${df} [$t] filter=$filter"
    $r = Invoke-CommandWithHeartbeat -FilePath $exeObj.FullName `
         -ArgumentList @("--gtest_filter=$filter") `
         -LogPath (Join-Path $logRoot "i3_${df}_$t.log")

    # NOTE: gtest text markers are authoritative; ExitCode participates
    # only when retrievable (defends against rare PS5.1 ExitCode loss).
    # Keep this file pure ASCII: BOM-less CJK comments break PS5.1
    # (ANSI/GBK decode can swallow the newline and merge code lines
    # into a comment -- exactly what happened to $exitOk once).
    $exitOk = if ($null -eq $r.ExitCode) { $true } else { $r.ExitCode -eq 0 }
    $ok = ($r.Passed -and -not $r.Failed -and -not $r.Hung -and
           -not $r.Capped -and $exitOk)
    if (-not $ok) {
        $why = if ($r.Hung)   { "HANG (silent > ${HeartbeatTimeoutSec}s)" }
               elseif ($r.Capped) { "CAP exceeded (${MaxRunSec}s)" }
               elseif ($r.Failed) { "test failures" }
               elseif (-not $r.Passed) { "no PASSED marker" }
               else { "exit=$($r.ExitCode)" }
        $failures.Add("I3: ${df} filter '$filter' in $t did not pass ($why)")
        Write-Host ("  FAIL  {0}: {1} : {2}  [{3}, {4}s]" -f $df, $t, $filter, $why, $r.Seconds)
        Show-FailureTail -r @{ LogPath = $r.LogPath }
        $dfStatus[$df] = $false; $i3ok = $false
    } else {
        Write-Host ("  pass  {0}  [{1}s]" -f $df, $r.Seconds)
        if (-not $dfStatus.ContainsKey($df)) { $dfStatus[$df] = $true }
    }
}
foreach ($df in ($dfStatus.Keys | Sort-Object)) {
    if ($dfStatus[$df]) { Write-Host "  ok    $df (evidence executable and passing)" }
}
if ($i3ok) { Write-Host "I3 PASS: DF01-DF09 + GP01 all backed by executed tests" }

# ============================================================
# Result
# ============================================================
Write-Host ""
Write-Host "=========================================="
if ($failures.Count -eq 0) {
    Write-Host "EVIDENCE INTEGRITY GATE: ALL GREEN (I1+I2+I3), in-scope total = $total"
    exit 0
} else {
    Write-Host "EVIDENCE INTEGRITY GATE: $($failures.Count) FAILURE(S):"
    foreach ($f in $failures) { Write-Host "  - $f" }
    exit 1
}
