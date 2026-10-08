<#
.SYNOPSIS
    Provision the shaderc artifact so that the SPIR-V compilation path is
    actually built and verified. See docs/Pending-Decisions-Brief.md section 8.9.

.DESCRIPTION
    Why this script exists
    ---------------------
    engine/CMakeLists.txt resolves shaderc from hard coded paths under
    third_party/shaderc/build. Those artifacts are produced by a build of the
    shaderc submodule and are ignored by git, so a fresh checkout has none of
    them. Previously nothing in the repository or in CI produced the Debug and
    Release artifacts, so the gate at engine/CMakeLists.txt was never satisfied
    on CI and GLSL to SPIR-V compilation was silently disabled. The full suite
    passes identically either way, so a green run did not show that the SPIR-V
    path had been verified at all.

    This script provisions the artifacts the gate requires, per platform.

    Runtime library matching
    ------------------------
    On Windows the consumer side follows the root CMakeLists.txt policy, which
    is per configuration: Debug uses /MDd and the other configurations use /MD.
    The existing local build tree forced a single value, CMAKE_MSVC_RUNTIME_LIBRARY
    equal to MultiThreadedDLL, which overwrote the Debug configuration as well.
    That mismatch between a /MDd consumer and a /MD artifact is what produced the
    LNK2038 inconsistent dll linkage failure on sandbox/SpirVTest. This script
    therefore does not force one runtime value. It enables the shared CRT so that
    shaderc uses the dynamic runtime, and lets the multi configuration generator
    apply the correct value per configuration, which matches the consumer.

    On Linux there is no equivalent CRT choice, so the Windows runtime settings
    are deliberately not applied. The build matches the platform's own
    configuration instead.

    RelWithDebInfo with ASan is not handled here. That configuration needs a
    separately instrumented artifact and is provisioned by
    tools/build_shaderc_asan_rwd.ps1.

.PARAMETER Config
    Configurations to build. Defaults to Debug and Release.

.PARAMETER Force
    Rebuild even when the artifacts already exist.

.PARAMETER Target
    Only report what would happen. Useful for auditing.

.EXAMPLE
    pwsh -NoProfile -File tools/build_shaderc_ci.ps1

.EXAMPLE
    pwsh -NoProfile -File tools/build_shaderc_ci.ps1 -Config Release -WhatIf
#>
[CmdletBinding()]
param(
    [string[]] $Config = @('Debug', 'Release'),
    [switch] $Force,
    [switch] $WhatIf
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Step  { param([string]$m) Write-Host "==> $m" -ForegroundColor Cyan }
function Write-Ok    { param([string]$m) Write-Host "    OK   $m" -ForegroundColor Green }
function Write-Info  { param([string]$m) Write-Host "    ..   $m" -ForegroundColor Gray }
function Write-Fail  { param([string]$m) Write-Host "    FAIL $m" -ForegroundColor Red }

$repoRoot  = Split-Path -Parent $PSScriptRoot
$shadercSrc = Join-Path $repoRoot 'third_party/shaderc'
$buildDir  = Join-Path $shadercSrc 'build'
$libDir    = Join-Path $buildDir 'libshaderc'

if ($WhatIf) { $Force = $false }

# ── 1. 前置检查 ─────────────────────────────────────────────────────────────
Write-Step "Checking prerequisites"

if (-not (Test-Path (Join-Path $shadercSrc 'CMakeLists.txt'))) {
    Write-Fail "shaderc submodule is missing at $shadercSrc"
    Write-Host  "    Initialize it with: git submodule update --init --recursive" -ForegroundColor Yellow
    exit 1
}
Write-Ok "shaderc submodule present"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Fail "cmake is not on PATH"
    exit 1
}
Write-Ok "cmake $((& cmake --version | Select-Object -First 1) -replace '.*cmake version\s*','')"

# ── 2. 平台判定 ─────────────────────────────────────────────────────────────
$isWindowsHost = $IsWindows
if ($isWindowsHost) {
    $arch      = 'x64'
    # 刻意**不指定** -G。此前硬编码 "Visual Studio 17 2022"，而 CI runner 镜像已迁移
    # 到 Visual Studio 18 (2026)，该版本不再有对应实例，cmake 直接报
    # "could not find any instance of Visual Studio"。交给 cmake 选默认生成器，
    # 与主工程 configure（同样不传 -G）保持一致；VS 系列生成本身即 multi-config，
    # 目录布局不变。
    $generator = '(cmake default on this host)'
    $cmakeArgs = @(
        "-A$arch",
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
    # with one uniform layout on every platform.
    if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
        Write-Fail "ninja is required on non Windows hosts for the multi configuration layout"
        exit 1
    }
    $generator = 'Ninja Multi-Config'
    # No CRT selection on this platform, matching its own configuration instead.
    $cmakeArgs = @(
        '-G', $generator,
        '-DSHADERC_ENABLE_SHARED_CRT=ON',
        '-DSHADERC_SKIP_TESTS=ON',
        '-DSHADERC_SKIP_EXAMPLES=ON',
        '-DSPIRV_SKIP_TESTS=ON',
        '-DSPIRV_SKIP_EXECUTABLES=ON'
    )
    $artifact = { param($c) Join-Path $libDir "$c/libshaderc_combined.a" }
}
Write-Ok "platform: $(if($isWindowsHost){'windows'}else{'linux'})  generator: $generator"

# ── 3. Configure ────────────────────────────────────────────────────────────
# A build tree may have been produced from a different source location, for
# example a restored CI cache. CMake refuses to reconfigure across source
# directories, so such a tree is discarded rather than reused.
# CMake records source and home directories with forward slashes and its own
# drive letter casing, while the PowerShell providers return backslashes. Both
# sides are therefore normalized before comparison, otherwise an identical tree
# looks foreign and would be discarded and rebuilt on every invocation, which is
# exactly the cost this script exists to avoid.
function Get-NormalizedPath {
    param([string] $Path)
    if (-not $Path) { return '' }
    return (($Path -replace '\\', '/').TrimEnd('/')).ToLowerInvariant()
}

$cachePath = Join-Path $buildDir 'CMakeCache.txt'
if (Test-Path $cachePath) {
    $cachedSource = (Select-String -Path $cachePath -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=' |
                     Select-Object -First 1).Line.Split('=', 2)[1]
    $expectedSource = (Resolve-Path $shadercSrc).Path
    if ((Get-NormalizedPath $cachedSource) -ne (Get-NormalizedPath $expectedSource)) {
        Write-Info "discarding build tree created from a different source directory"
        Remove-Item -Recurse -Force $buildDir
    } else {
        Write-Info "reusing build tree from the same source directory"
    }
}

if (-not (Test-Path $cachePath)) {
    Write-Step "Configuring shaderc into $buildDir"
    $cfgOut = & cmake -S $shadercSrc -B $buildDir @cmakeArgs 2>&1
    $cfgOut | ForEach-Object { Write-Host "  $_" }
    if ($LASTEXITCODE -ne 0) {
        # 镜像升级会改变默认生成器；此时缓存里的生成器与当前默认不符，cmake 会
        # 直接拒绝重新配置。丢弃后重试一次，而不是把这个失败原样抛出。
        $text = ($cfgOut | Out-String)
        if ($text -match 'generator.*does not match|CMake Error: Generator') {
            Write-Info "cached generator no longer matches this host's default; discarding and retrying"
            Remove-Item -Recurse -Force $buildDir
            $cfgOut = & cmake -S $shadercSrc -B $buildDir @cmakeArgs 2>&1
            $cfgOut | ForEach-Object { Write-Host "  $_" }
        }
        if ($LASTEXITCODE -ne 0) {
            Write-Fail "configure failed with exit $LASTEXITCODE"
            exit $LASTEXITCODE
        }
    }
} else {
    Write-Step "Reusing existing shaderc build tree"
    # A stale tree may carry a forced runtime value from an earlier attempt,
    # which is exactly the configuration that produced LNK2038.
    $forced = Select-String -Path $cachePath -Pattern '^CMAKE_MSVC_RUNTIME_LIBRARY:UNINITIALIZED=' -ErrorAction SilentlyContinue
    if ($forced -and $isWindowsHost) {
        Write-Info "removing forced CMAKE_MSVC_RUNTIME_LIBRARY=$($forced.Line.Split('=')[-1]) (per configuration mismatch)"
        & cmake -U CMAKE_MSVC_RUNTIME_LIBRARY -S $shadercSrc -B $buildDir @cmakeArgs
        if ($LASTEXITCODE -ne 0) { Write-Fail "reconfigure failed"; exit $LASTEXITCODE }
    }
}

# ── 4. Build ────────────────────────────────────────────────────────────────
foreach ($c in $Config) {
    $lib = & $artifact $c
    if ((Test-Path $lib) -and (-not $Force)) {
        Write-Ok "$c already provisioned -> $lib"
        continue
    }
    Write-Step "Building shaderc ($c)"
    & cmake --build $buildDir --config $c --parallel
    if ($LASTEXITCODE -ne 0) {
        Write-Fail "build failed for $c with exit $LASTEXITCODE"
        exit $LASTEXITCODE
    }
}

# ── 5. Verify the artifacts the gate will look for ──────────────────────────
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