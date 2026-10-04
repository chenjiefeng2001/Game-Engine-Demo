# ============================================================
# build_shaderc_asan_rwd.ps1 - RelWithDebInfo ASan shaderc artifact
#
# WHY
#   The repository declares RelWithDebInfo to be an AddressSanitizer
#   configuration (root CMakeLists.txt applies /fsanitize=address via
#   `$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>`).  shaderc is a
#   prebuilt static lib, and only two artifacts used to exist:
#
#     build/libshaderc/Debug/shaderc_combined.lib     /MDd + ASan
#     build/libshaderc/Release/shaderc_combined.lib   /MD, no ASan
#
#   `optimized` (Release/MinSizeRel/RelWithDebInfo) therefore fell back to
#   the Release lib, so ASan objects (annotation value 1) were linked
#   against non-ASan shaderc (annotation value 0), producing 492
#   LNK2038 annotate_string/annotate_vector mismatches.  Only
#   sandbox/SpirVTest referenced shaderc symbols, so only it failed.
#
#   This script regenerates the missing artifact: /MD + ASan, built into a
#   SEPARATE directory so the existing Debug/Release libs are never
#   rebuilt or overwritten.
#
# WHY NOT COMMIT THE .lib
#   It is ~255 MB.  More importantly `third_party/shaderc` is a git
#   SUBMODULE whose .gitignore contains `build-*/`, so this artifact is
#   intentionally untracked.  A committed 255 MB build product would also
#   be machine-specific.  The in-tree source build regenerates it
#   deterministically instead.
#
# DEVIATION FROM THE DEBUG/RELEASE RECIPE (intentional, documented)
#   -DSHADERC_ENABLE_WERROR_COMPILE=OFF   (the working tree uses ON)
#     ASan can introduce additional diagnostics; treating them as errors
#     would block the build for reasons unrelated to codegen.  This only
#     controls whether warnings are fatal - it does not change generated
#     code, so the artifact stays comparable to the Debug ASan variant.
#   -DBUILD_TESTING=OFF / -DSHADERC_SKIP_TESTS=ON
#     Only shaderc_combined is needed; skipping tests/examples/executables
#     keeps the build to the minimum.
#
# USAGE
#   pwsh -File tools/build_shaderc_asan_rwd.ps1
#   pwsh -File tools/build_shaderc_asan_rwd.ps1 -SkipReconfigure
#
#   Then build RelWithDebInfo as usual:
#   cmake --build build --config RelWithDebInfo
#
#   NOTE: engine/CMakeLists.txt picks the artifact up with EXISTS() at
#   CONFIGURE time, so the main build must be re-configured after this
#   script runs.  That is what -Reconfigure (default) does.
# ============================================================
[CmdletBinding()]
param(
    # Re-run CMake on the main build dir so the RelWithDebInfo mapping
    # sees the freshly built artifact (EXISTS() is evaluated at configure).
    [switch]$SkipReconfigure,

    # Main build directory, relative to repo root.
    [string]$MainBuildDir = 'build'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot   = Split-Path -Parent $PSScriptRoot
$shadercSrc = Join-Path $repoRoot 'third_party/shaderc'
$asanBuild  = Join-Path $shadercSrc 'build-asan-rwd'
$tpDir      = Join-Path $shadercSrc 'third_party'

if (-not (Test-Path -LiteralPath $shadercSrc)) {
    throw "shaderc source tree not found: $shadercSrc"
}

# ── 1. Configure ──────────────────────────────────────────────────────
# Directory names must match the vendored layout exactly; note it is
# `abseil_cpp`, NOT `absl` (passing the wrong one fails with a confusing
# "add_subdirectory given source ... is not an existing directory").
$cfgArgs = @(
    '-S', $shadercSrc
    '-B', $asanBuild
    '-G', 'Visual Studio 17 2022'
    "-DSHADERC_ABSL_DIR=$tpDir/abseil_cpp"
    "-DSHADERC_EFFCEE_DIR=$tpDir/effcee"
    "-DSHADERC_GLSLANG_DIR=$tpDir/glslang"
    "-DSHADERC_GOOGLE_TEST_DIR=$tpDir/googletest"
    "-DSHADERC_RE2_DIR=$tpDir/re2"
    '-DBUILD_EXTERNAL=ON'
    '-DALLOW_EXTERNAL_GTEST=OFF'
    '-DALLOW_EXTERNAL_SPIRV_TOOLS=OFF'
    '-DENABLE_SPIRV=ON'
    '-DENABLE_OPT=ON'
    '-DENABLE_PCH=ON'
    '-DENABLE_GLSLANG_BINARIES=ON'
    '-DSHADERC_ENABLE_HLSL=ON'
    '-DSHADERC_ENABLE_SHARED_CRT=ON'      # /MD - must match RelWithDebInfo CRT
    '-DSHADERC_ENABLE_WGSL_OUTPUT=OFF'
    '-DSHADERC_ENABLE_WERROR_COMPILE=OFF' # see DEVIATION note above
    '-DBUILD_TESTING=OFF'
    '-DSHADERC_SKIP_TESTS=ON'
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL'
    '-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=/Zi /Ob1 /DNDEBUG /fsanitize=address'
)

Write-Host '[1/3] Configuring ASan RelWithDebInfo shaderc...' -ForegroundColor Cyan
& cmake @cfgArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed (exit $LASTEXITCODE)" }

# ── 2. Build ──────────────────────────────────────────────────────────
Write-Host '[2/3] Building shaderc_combined (RelWithDebInfo)...' -ForegroundColor Cyan
& cmake --build $asanBuild --config RelWithDebInfo --target shaderc_combined
if ($LASTEXITCODE -ne 0) { throw "shaderc build failed (exit $LASTEXITCODE)" }

# ── 3. Verify the artifact is actually ASan-instrumented ─────────────
# Cheap guard against silently producing a non-ASan lib, which would
# reintroduce the exact LNK2038 this artifact exists to fix.
$lib = Join-Path $asanBuild 'libshaderc/RelWithDebInfo/shaderc_combined.lib'
if (-not (Test-Path -LiteralPath $lib)) { throw "artifact missing: $lib" }

$bytes = [System.IO.File]::ReadAllBytes($lib)
$text  = [System.Text.Encoding]::ASCII.GetString($bytes)
$hasAnnotate = $text.Contains('__sanitizer_annotate_contiguous_container')
$hasAsan     = $text.Contains('__asan_')
$mb          = [Math]::Round($bytes.Length / 1MB, 1)

if (-not ($hasAnnotate -and $hasAsan)) {
    throw ("artifact is NOT ASan-instrumented " +
           "(annotate=$hasAnnotate asan=$hasAsan). RelWithDebInfo would " +
           "fall back to the non-ASan Release shaderc and LNK2038 would return.")
}
Write-Host ("      OK: {0} MB, ASan symbols present" -f $mb) -ForegroundColor Green

# ── 4. Re-configure the main build so EXISTS() picks it up ────────────
if (-not $SkipReconfigure) {
    $mainBuild = Join-Path $repoRoot $MainBuildDir
    if (Test-Path -LiteralPath $mainBuild) {
        Write-Host '[3/3] Re-configuring main build...' -ForegroundColor Cyan
        & cmake -S $repoRoot -B $mainBuild
        if ($LASTEXITCODE -ne 0) { throw "main re-configure failed (exit $LASTEXITCODE)" }
        Write-Host '      RelWithDebInfo will now link the ASan shaderc.' -ForegroundColor Green
    } else {
        Write-Warning "main build dir '$mainBuild' not found; skipping re-configure."
        Write-Warning 'Run: cmake -S . -B <builddir>   before building RelWithDebInfo.'
    }
} else {
    Write-Host '[3/3] Skipped re-configure (-SkipReconfigure).' -ForegroundColor Yellow
    Write-Host '      Remember: cmake -S . -B <builddir>  then build RelWithDebInfo.' -ForegroundColor Yellow
}

Write-Host 'Done. Build RelWithDebInfo with:' -ForegroundColor Cyan
Write-Host '  cmake --build build --config RelWithDebInfo' -ForegroundColor Cyan
exit 0