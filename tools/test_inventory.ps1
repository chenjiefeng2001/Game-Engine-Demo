# test_inventory.ps1 — P6-0/0B machine-generatable test inventory
#
# Generates the authoritative test baseline per configuration:
#   - Native gtest targets: discovery counts (gtest_list_tests) + executed
#     pass/fail/skip counts (parsed from ctest LastTest.log)
#   - CTest entries (incl. fixture tests)
#   - Managed MSTest static counts ([TestMethod] per C# project)
#
# Execution is delegated to ctest (it owns fixtures, timeouts, and CWD rules);
# this script never runs gtest binaries directly for pass/fail counting.
#
# Usage:
#   powershell -File tools/test_inventory.ps1 -Configurations Debug,Release
#   powershell -File tools/test_inventory.ps1 -Configurations Debug -SkipExecute
#
# Output:
#   artifacts/test_inventory/inventory-<config>.json
#   artifacts/test_inventory/inventory-<config>.md
#   artifacts/test_inventory/summary.md (aggregate across configurations)

param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
    [string[]]$Configurations = @("Debug", "Release"),
    [switch]$SkipExecute,
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if (-not $BuildDir) { $BuildDir = Join-Path $RepoRoot "build" }
$outDir = Join-Path $RepoRoot "artifacts\test_inventory"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$commit = (git -C $RepoRoot rev-parse HEAD)
$branch = (git -C $RepoRoot rev-parse --abbrev-ref HEAD)
$subDirtyRaw = git -C $RepoRoot status --short | Where-Object { $_ -match "^ m|^M  third_party|^..." -and $_ -match "third_party/" }
$generatedAt = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")

$allConfigs = [ordered]@{}

foreach ($cfg in $Configurations) {
    Write-Host "=== configuration: $cfg ===" -ForegroundColor Cyan

    # ---- native targets: discovery via gtest_list_tests -------------------
    $exeDir = Join-Path $BuildDir "tests\$cfg"
    $targets = @()
    if (Test-Path $exeDir) {
        $targets = Get-ChildItem $exeDir -Filter "test_*.exe" | Sort-Object Name
    }

    $nativeResults = @()
    foreach ($exe in $targets) {
        $name = $exe.BaseName
        $discovered = 0; $suites = 0
        $list = & $exe.FullName --gtest_list_tests 2>&1
        foreach ($line in $list) {
            if ($line -match "^\s+\S") { $discovered++ }
            elseif ($line -match "^\S.*\.$") { $suites++ }
        }
        $nativeResults += [ordered]@{
            target = $name
            suites = $suites
            discoveredTests = $discovered
        }
    }

    # ---- native execution: parsed from ctest ------------------------------
    $executed = [ordered]@{}
    $ctestEntries = -1
    $ctestPassed = -1
    $ctestFailed = -1
    $lastTestLog = Join-Path $BuildDir "Testing\Temporary\LastTest.log"
    if (-not $SkipExecute) {
        # Schedule first so a fixup build can run before tests (ctest runs tests only).
        Push-Location $BuildDir
        $ctestOut = & ctest -C $cfg -N 2>&1
        Pop-Location
        $totalMatch = ($ctestOut | Select-String -Pattern "Total Tests: (\d+)").Matches.Groups[1].Value
        if ($totalMatch) { $ctestEntries = [int]$totalMatch }

        Push-Location $BuildDir
        $PrevEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & ctest -C $cfg --output-on-failure 2>&1 | Tee-Object -FilePath (Join-Path $outDir "ctest-$cfg-run.log")
        $ctestExit = $LASTEXITCODE
        $ErrorActionPreference = $PrevEap
        Pop-Location

        if (Test-Path $lastTestLog) {
            $log = Get-Content $lastTestLog -Raw
            # split per-test blocks: ctest marks each test with "N/M Test #K: <name> ..."
            # LastTest.log classic format: "N/M Test: <name>" (also accept
            # newer "N/M Test #K: <name>" console style for future ctest).
            $blocks = [regex]::Split($log, "(?m)^\d+/\d+ Test(?: #\d+)?: ([^\r\n]+)[ .]*\r?\n")
            for ($i = 1; $i -lt $blocks.Count; $i += 2) {
                $testName = $blocks[$i].Trim()
                $block = $blocks[$i + 1]
                $run = $null
                if ($block -match "(?m)^\[==========\] Running (\d+) tests? from (\d+) test suites?\.") {
                    $run = @{ tests = [int]$Matches[1]; suites = [int]$Matches[2] }
                }
                $passed = $null; $failed = $null; $skipped = 0
                if ($block -match "(?m)^\[  PASSED  \] (\d+) tests?\.*\s*$") { $passed = [int]$Matches[1] }
                if ($block -match "(?m)^\[  FAILED  \] (\d+) tests?,") { $failed = [int]$Matches[1] }
                if ($block -match "(?m)^\[  SKIPPED \] (\d+) tests?") { $skipped = [int]$Matches[1] }
                $executed[$testName] = [ordered]@{
                    ran = if ($run) { $run.tests } else { -1 }
                    passed = $passed
                    failed = $failed
                    skipped = $skipped
                }
            }
        } else {
            Write-Warning "LastTest.log not found; executed counts unavailable"
        }
    }

    # ---- managed static counts -------------------------------------------
    $managedProjects = @()
    $csTestFiles = Get-ChildItem -Path (Join-Path $RepoRoot "tests") -Recurse -Filter "*.cs" |
        Where-Object { $_.FullName -notmatch "\\(obj|bin)\\|Imported" }
    $byProject = $csTestFiles | Group-Object { $_.Directory.Parent.Name }
    $managedTotal = 0
    foreach ($g in $byProject) {
        $count = 0
        foreach ($f in $g.Group) {
            $count += ([regex]::Matches((Get-Content $f.FullName -Raw), "\[TestMethod\]")).Count
        }
        if ($count -gt 0) {
            $managedProjects += [ordered]@{ project = $g.Name; staticTestMethods = $count }
            $managedTotal += $count
        }
    }

    # ---- assemble per-config JSON ------------------------------------------
    $cfgDoc = [ordered]@{
        configuration = $cfg
        commit = $commit
        branch = $branch
        generatedAt = $generatedAt
        ctestEntries = $ctestEntries
        nativeTargetCount = $nativeResults.Count
        nativeTargets = $nativeResults
        executed = $executed
        ctestExitCode = if (-not $SkipExecute) { $ctestExit } else { $null }
        managed = [ordered]@{
            method = "static rg [TestMethod] (rebuild-of-record, not dry-run)"
            projects = $managedProjects
            staticTestMethods = $managedTotal
        }
        dirtyThirdParty = $subDirtyRaw
    }

    $jsonPath = Join-Path $outDir "inventory-$cfg.json"
    $cfgDoc | ConvertTo-Json -Depth 6 | Set-Content -Path $jsonPath -Encoding UTF8

    # ---- markdown -----------------------------------------------------------
    $md = [System.Collections.Generic.List[string]]::new()
    $md.Add("# Test Inventory — $cfg")
    $md.Add("")
    $md.Add("- commit: ``$commit`` ($branch)")
    $md.Add("- generated: $generatedAt")
    $md.Add("- ctest entries: $ctestEntries")
    $md.Add("")
    $md.Add("| native target | suites | discovered | ran | passed | failed | skipped |")
    $md.Add("|---|---|---|---|---|---|---|")
    $totD = 0; $totR = 0; $totP = 0; $totF = 0; $totS = 0
    foreach ($n in $nativeResults) {
        $tName = $n.target
        $e = $executed[$tName]
        $ran = if ($e) { $e.ran } else { "-" }
        $pas = if ($e) { $e.passed } else { "-" }
        $fai = if ($e) { $e.failed } else { "-" }
        $ski = if ($e) { $e.skipped } else { "-" }
        $md.Add("| ``$($n.target)`` | $($n.suites) | $($n.discoveredTests) | $ran | $pas | $fai | $ski |")
        $totD += [int]$n.discoveredTests
        $totR += if ($ran -match '^\d+$') { [int]$ran } else { 0 }
        $totP += if ($pas -match '^\d+$') { [int]$pas } else { 0 }
        $totF += if ($fai -match '^\d+$') { [int]$fai } else { 0 }
        $totS += if ($ski -match '^\d+$') { [int]$ski } else { 0 }
    }
    $md.Add("| **total native** | — | **$totD** | **$totR** | **$totP** | **$totF** | **$totS** |")
    $md.Add("")
    $md.Add("Managed MSTest static count: **$managedTotal** across $($managedProjects.Count) projects:")
    foreach ($p in $managedProjects) { $md.Add("- ``$($p.project)``: $($p.staticTestMethods)") }
    $md.Add("")
    $dirtyJoined = ($subDirtyRaw -join '; ')
    $md.Add("dirty third_party: $dirtyJoined")
    $mdPath = Join-Path $outDir "inventory-$cfg.md"
    $md | Set-Content -Path $mdPath -Encoding UTF8

    $allConfigs[$cfg] = [ordered]@{
        json = (Split-Path $jsonPath -Leaf)
        md = (Split-Path $mdPath -Leaf)
        nativeTargets = $nativeResults.Count
        discovered = $totD
        executed = $totR
        passed = $totP
        failed = $totF
        skipped = $totS
        managedTotal = $managedTotal
        ctestEntries = $ctestEntries
        ctestExit = if (-not $SkipExecute) { $ctestExit } else { $null }
    }
    Write-Host "native: $totD discovered / $totR executed / $totP passed / $totF failed (+$totS skipped)" -ForegroundColor Yellow
    Write-Host "managed(static): $managedTotal" -ForegroundColor Yellow
}

# ---- aggregate summary ------------------------------------------------------
$sum = [System.Collections.Generic.List[string]]::new()
$sum.Add("# Test Inventory Summary")
$sum.Add("")
$sum.Add("- commit: ``$commit`` ($branch)")
$sum.Add("- generated: $generatedAt")
$sum.Add("")
$sum.Add("| config | native targets | discovered | executed | passed | failed | skipped | managed(static) | ctest entries |")
$sum.Add("|---|---|---|---|---|---|---|---|---|")
foreach ($k in $allConfigs.Keys) {
    $c = $allConfigs[$k]
    $sum.Add("| $k | $($c.nativeTargets) | $($c.discovered) | $($c.executed) | $($c.passed) | $($c.failed) | $($c.skipped) | $($c.managedTotal) | $($c.ctestEntries) |")
}
$sumPath = Join-Path $outDir "summary.md"
$sum | Set-Content -Path $sumPath -Encoding UTF8

Write-Host ""
Get-Content $sumPath
exit 0
