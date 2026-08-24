# ============================================================
# integrity_gate.ps1 - Evidence Integrity Gate
#
# Gates (see docs/Evidence-Baseline.md):
#   I1 Test Registration   : every tests/**/*Test.cpp must be in CMakeLists
#   I2 Test Count Integrity: actual gtest discovery must match baseline
#   I3 Dogfood Execution   : every DF smoke must be backed by an executed test
#
# Usage:
#   powershell -File tools/integrity_gate.ps1 [-SkipBuild]
# Exit code: 0 = all green; non-zero = failures
# ============================================================
param(
    [switch]$SkipBuild,
    [string]$BaselinePath = "docs/Evidence-Baseline.md",
    [string]$TestsDir = "tests",
    [string]$CMakeLists = "tests/CMakeLists.txt",
    [string]$BuildDir = "build",
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
$env:ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE = "1"

$failures = New-Object System.Collections.Generic.List[string]
$excluded = @{}   # target -> reason (parsed from baseline)

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
# I2 Test Count Integrity
# ============================================================
Write-Host ""
Write-Host "=== I2 Test Count Integrity ==="
if (-not $SkipBuild) {
    Write-Host "building in-scope targets..."
    foreach ($t in ($inScope.Keys | Sort-Object)) {
        cmake --build $BuildDir --target $t --config $Config 2>&1 | Out-Null
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
# I3 Dogfood Execution
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
    "DF07|test_content|Dogfood07.LoadRunRestart"
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
    $runOut = cmd /c "`"$($exeObj.FullName)`" --gtest_filter=$filter 2>&1"
    $ranOk  = ($runOut | Select-String "\[  PASSED  \]" -Quiet)
    $hadFail = ($runOut | Select-String "\[  FAILED  \]" -Quiet)
    if (-not $ranOk -or $hadFail) {
        $failures.Add("I3: ${df} filter '$filter' in $t did not pass")
        Write-Host "  FAIL  ${df}: $t : $filter"
        $dfStatus[$df] = $false; $i3ok = $false
    } else {
        $dfStatus[$df] = if ($dfStatus.ContainsKey($df)) { $dfStatus[$df] } else { $true }
    }
}
foreach ($df in ($dfStatus.Keys | Sort-Object)) {
    if ($dfStatus[$df]) { Write-Host "  ok    $df (evidence executable and passing)" }
}
if ($i3ok) { Write-Host "I3 PASS: DF01-DF07 all backed by executed tests" }

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
