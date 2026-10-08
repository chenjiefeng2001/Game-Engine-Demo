<#
.SYNOPSIS
    Materialise the pinned shader compiler dependencies and build the artifact,
    so that the SPIR-V path is actually compiled and verified.
    See docs/Pending-Decisions-Brief.md section 8.9.

.DESCRIPTION
    Why this script exists
    ---------------------
    engine/CMakeLists.txt resolves shaderc from hard coded paths under
    third_party/shaderc/build. Those artifacts come from a build of the shaderc
    submodule and are ignored by git, so a fresh checkout has none of them.
    Previously nothing in the repository or in CI produced the debug and release
    artifacts, so the activation gate was never satisfied and GLSL to SPIR-V
    compilation was silently disabled. The full suite passes identically either
    way, so a green run did not show that the SPIR-V path had been verified.

    Transitive dependency provenance (B')
    --------------------------------------
    shaderc does not carry its own SPIRV-Tools, glslang or SPIRV-Headers sources,
    and it does not declare them as git submodules. It declares them in a
    committed manifest, DEPS, together with the upstream tool
    utils/git-sync-deps. That manifest and that tool are the provenance: the
    revisions are owned by the shaderc repository, not by this script.

    This script therefore invokes the upstream tool rather than cloning anything
    itself, and deliberately contains no revision of its own. Anything already
    present in the local dependency directories is discarded first, because a
    previous state may have carried revisions that do not match the manifest,
    and a build made against those is not evidence about the pinned graph.

    Runtime library matching
    ------------------------
    On Windows the consumer side follows the root CMakeLists.txt policy, which is
    per configuration: debug uses the debug runtime and the other configurations
    use the release runtime. An earlier local tree pinned a single release
    runtime value, which overwrote the debug configuration as well, so the debug
    artifact carried the release runtime while debug consumers use the debug
    runtime. That mismatch is what produced an inconsistent dll linkage failure
    when the sandbox shader binary linked. No runtime value is forced here; the
    shared CRT is enabled and the multi configuration generator chooses per
    configuration. On Linux there is no equivalent choice and the platform
    matches its own configuration.

    RelWithDebInfo with ASan is not handled here. That configuration needs a
    separately instrumented artifact, provisioned by
    tools/build_shaderc_asan_rwd.ps1.

.PARAMETER Config
    Configurations to build. Defaults to Debug and Release.

.EXAMPLE
    pwsh -NoProfile -File tools/build_shaderc_ci.ps1
#>
[CmdletBinding()]
param(
    [string[]] $Config = @('Debug', 'Release')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Step { param([string]$m) Write-Host "==> $m" -ForegroundColor Cyan }
function Write-Ok   { param([string]$m) Write-Host "    OK   $m" -ForegroundColor Green }
function Write-Info { param([string]$m) Write-Host "    ..   $m" -ForegroundColor Gray }
function Write-Fail { param([string]$m) Write-Host "    FAIL $m" -ForegroundColor Red }

$repoRoot   = Split-Path -Parent $PSScriptRoot
$shadercSrc = Join-Path $repoRoot 'third_party/shaderc'
$buildDir   = Join-Path $shadercSrc 'build'
$libDir     = Join-Path $buildDir 'libshaderc'
$depsFile   = Join-Path $shadercSrc 'DEPS'
$syncTool   = Join-Path $shadercSrc 'utils/git-sync-deps'

# ── 1. Prerequisites ────────────────────────────────────────────────────────
Write-Step "Checking prerequisites"

if (-not (Test-Path (Join-Path $shadercSrc 'CMakeLists.txt'))) {
    Write-Fail "shaderc submodule is missing at $shadercSrc"
    Write-Host  "    Initialize it with: git submodule update --init --recursive" -ForegroundColor Yellow
    exit 1
}
if (-not (Test-Path $depsFile)) { Write-Fail "shaderc DEPS manifest missing: $depsFile"; exit 1 }
if (-not (Test-Path $syncTool)) { Write-Fail "shaderc dependency sync tool missing: $syncTool"; exit 1 }
Write-Ok "shaderc submodule, DEPS manifest and git-sync-deps present"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Fail "cmake is not on PATH"
    exit 1
}
Write-Ok "cmake $((& cmake --version | Select-Object -First 1) -replace '.*cmake version\s*','')"

# git-sync-deps is a python script. The interpreter is validated by actually
# running it, because on Windows hosts a python3 name can resolve to a store
# stub that exists on PATH but cannot execute, and picking it would fail later
# with an unrelated message.
$python = $null
foreach ($cand in @('python3', 'python')) {
    if (-not (Get-Command $cand -ErrorAction SilentlyContinue)) { continue }
    $ver = (& $cand --version 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -eq 0 -and $ver -match 'Python\s+\d+\.\d+') {
        $python = $cand
        Write-Ok "python interpreter: $cand ($ver)"
        break
    }
    Write-Info "$cand is present but not usable ($ver)"
}
if (-not $python) {
    Write-Fail "no usable python interpreter found for the upstream dependency sync tool"
    exit 1
}

# ── 2. Platform ─────────────────────────────────────────────────────────────
$isWindowsHost = $IsWindows
if ($isWindowsHost) {
    # No -G: the runner image default may differ from any pinned generator
    # version, and cmake's default is also what the main project configure uses.
    # Visual Studio generators are multi configuration, so the output layout the
    # artifact paths rely on is unchanged.
    $generatorDesc = '(cmake default on this host)'
    $cmakeArgs = @(
        '-A x64',
        '-DSHADERC_ENABLE_SHARED_CRT=ON',
        '-DSHADERC_SKIP_TESTS=ON',
        '-DSHADERC_SKIP_EXAMPLES=ON',
        '-DSPIRV_SKIP_TESTS=ON',
        '-DSPIRV_SKIP_EXECUTABLES=ON'
    )
    $artifact = { param($c) Join-Path $libDir "$c/shaderc_combined.lib" }
} else {
    # Ninja Multi-Config gives the same per configuration output directories as
    # the Visual Studio generator, so engine/CMakeLists.txt resolves artifacts
    # with one uniform layout on every platform. No CRT choice is applied here.
    if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
        Write-Fail "ninja is required on non Windows hosts for the multi configuration layout"
        exit 1
    }
    $generatorDesc = 'Ninja Multi-Config'
    $cmakeArgs = @(
        '-G', 'Ninja Multi-Config',
        '-DSHADERC_ENABLE_SHARED_CRT=ON',
        '-DSHADERC_SKIP_TESTS=ON',
        '-DSHADERC_SKIP_EXAMPLES=ON',
        '-DSPIRV_SKIP_TESTS=ON',
        '-DSPIRV_SKIP_EXECUTABLES=ON'
    )
    $artifact = { param($c) Join-Path $libDir "$c/libshaderc_combined.a" }
}
Write-Ok "platform: $(if($isWindowsHost){'windows'}else{'linux'})  generator: $generatorDesc"

# ── 3. Fresh dependency materialization from the committed manifest ─────────
# Discard whatever is there. A local tree can hold dependency revisions that do
# not match the manifest, and an artifact built against those proves nothing
# about the pinned graph. Materialisation is therefore always from scratch.
Write-Step "Discarding existing dependencies and build artifacts"
foreach ($dep in @('third_party/spirv-tools', 'third_party/glslang',
                   'third_party/spirv-headers')) {
    $p = Join-Path $shadercSrc $dep
    if (Test-Path $p) {
        Remove-Item -Recurse -Force $p
        Write-Info "removed $dep"
    }
}
if (Test-Path $buildDir) {
    Remove-Item -Recurse -Force $buildDir
    Write-Info "removed existing build tree"
}

Write-Step "Materialising pinned dependencies via upstream git-sync-deps"
Write-Info "revisions come from $depsFile (owned by the shaderc repository)"
$syncOut = & $python $syncTool 2>&1
$syncOut | ForEach-Object { Write-Host "  $_" }
if ($LASTEXITCODE -ne 0) {
    Write-Fail "git-sync-deps failed with exit $LASTEXITCODE"
    exit $LASTEXITCODE
}

# Verify the three dependencies the build cannot proceed without actually exist.
foreach ($dep in @('third_party/spirv-tools', 'third_party/glslang',
                   'third_party/spirv-headers')) {
    $p = Join-Path $shadercSrc $dep
    if (-not (Test-Path (Join-Path $p 'CMakeLists.txt'))) {
        Write-Fail "dependency not materialised: $dep (no CMakeLists.txt)"
        exit 1
    }
    $head = (& git -C $p rev-parse --short=12 HEAD 2>$null | Out-String).Trim()
    Write-Ok ("{0,-28} @ {1}" -f $dep, $head)
}

# ── 4. Configure ────────────────────────────────────────────────────────────
Write-Step "Configuring shaderc into $buildDir"
$cfgOut = & cmake -S $shadercSrc -B $buildDir @cmakeArgs 2>&1
$cfgOut | ForEach-Object { Write-Host "  $_" }
if ($LASTEXITCODE -ne 0) {
    Write-Fail "configure failed with exit $LASTEXITCODE"
    exit $LASTEXITCODE
}

# ── 5. Build ────────────────────────────────────────────────────────────────
foreach ($c in $Config) {
    Write-Step "Building shaderc ($c)"
    & cmake --build $buildDir --config $c --parallel
    if ($LASTEXITCODE -ne 0) {
        Write-Fail "build failed for $c with exit $LASTEXITCODE"
        exit $LASTEXITCODE
    }
}

# ── 6. Verify the artifacts the activation gate will look for ───────────────
Write-Step "Verifying provisioned artifacts"
$missing = 0
foreach ($c in $Config) {
    $lib = & $artifact $c
    if (Test-Path $lib) {
        $size = [math]::Round((Get-Item $lib).Length / 1MB, 1)
        Write-Ok ("{0,-14} {1} MB  {2}" -f $c, $size, $lib)
    } else {
        Write-Fail "$c artifact missing: $lib"
        $missing++
    }
}

if ($missing -gt 0) {
    Write-Host ""
    Write-Fail "provisioning incomplete: $missing artifact(s) missing"
    Write-Host  "    engine/CMakeLists.txt will hard fail under REQUIRE_SHADERC=ON." -ForegroundColor Yellow
    exit 1
}

Write-Step "Provisioning complete"
Write-Host  "    Configure the project with -DREQUIRE_SHADERC=ON to prove the" -ForegroundColor Gray
Write-Host  "    SPIR-V path is enabled rather than silently degraded." -ForegroundColor Gray
exit 0