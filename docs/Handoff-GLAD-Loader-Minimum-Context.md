# Handoff: no CI runner can currently host the GPU pixel verification

**Owner:** HRC owner of `engine/src/OpenGL/OpenGLGraphicsFactory.cpp`, plus the
RHI owner and whoever owns the CI verification environment
**Raised by:** GPU skinning pixel verification work on branch `avalonia`
**Evidence runs:**
- CI run `37935492302` (commit `94390cf`) — loader failure and GPU test failure
  <https://github.com/chenjiefeng2001/Game-Engine-Demo/actions/runs/37935492302>
- CI run `37957896840` (commit `5b96bb9`) — Windows runner capability probe
  <https://github.com/chenjiefeng2001/Game-Engine-Demo/actions/runs/37957896840>
- CI run `37989594539` (commit `5f551b5`) — Linux software GL capability probe
  and from-scratch Linux build failure
  <https://github.com/chenjiefeng2001/Game-Engine-Demo/actions/runs/37989594539>
**Status:** awaiting two separate owner decisions, see §7

---

## 1. Summary

Two different goals are being conflated, and they have different answers.

| Verification goal | Linux + Xvfb + llvmpipe (measured) | Status |
|---|---|---|
| OpenGL 3.3 core, which the GPU pixel tests request | 4.5 core context **created** | **environment satisfies it** |
| GL 4.5 DSA entry points, the engine's highest real call requirement | 4.5 core available | **version capability satisfies it** |
| OpenGL 4.6 core, which the production render window requests | **creation failed** | environment does not satisfy it |
| Building `test_renderer` on Linux | fails in `RHIWindow.cpp` on `windows.h` | **blocked by a build dependency** |

**The central conclusion: Linux software GL has been demonstrated to have the
context capability the GPU pixel tests need. The current blocker is that
`EngineCore` compiles RHI sources unconditionally, so the test target cannot be
built at all. The blocker is not Mesa lacking the OpenGL version the tests
require.**

The production 3D viewport is a separate question from the pixel tests. It asks
for a 4.6 core context explicitly, which llvmpipe does not provide. Those two
questions must not be answered with a single probe result.

## 2. Windows runner: no context at all

The Windows Release runner **cannot create a GLFW OpenGL context**. A default
context, created with no version hint whatsoever, fails:

```
[GLPROBE] default context: CREATE FAILED
```

This is a direct observation on `windows-latest` from run `37957896840`. Because
no context exists, the run cannot report `GL_VERSION`, `GL_VENDOR`,
`GL_RENDERER` or `GL_SHADING_LANGUAGE_VERSION`, and an explicit 4.5 core
context could not be attempted.

**The problem must not be attributed to GLAD loader initialisation.** The loader
window, the GPU pixel tests' own 3.3 window, and a plain default window all fail
through the same GLFW + driver path. Changing the version GLAD loads at cannot
create a context that does not exist.

### Evidence wording

What is directly verified is: *the current runner cannot create a GLFW OpenGL
context.* No runner evidence establishes the cause beyond that. The absence of a
usable OpenGL ICD is a plausible explanation but is **not** confirmed here, and
this document does not assert it.

## 3. Why the tests fail on Windows

`engine/src/OpenGL/OpenGLGraphicsFactory.cpp` creates a 1x1 hidden window named
`"GLAD Loader"` solely to obtain a current context for `gladLoadGLContext`, and
requests a 4.6 core context for it (`OpenGLGraphicsFactory.cpp:62`, with the
`gladLoadGLContext` call at `:66`; GLFW hints immediately before line 62 request
`4` / `6`).

On the runner that window fails **57 times** across the suite (run `37935492302`):

```
[error] [OpenGLGraphicsFactory] Failed to create temporary GLAD loader window
```

GLAD therefore never initialises, and the three GPU skinning pixel tests fail in
their fixture before any rendering occurs (run `37935492302`):

```
[ RUN      ] GPUSkinningPixelTest.BindPoseDrawsGeometryInCentreBandOnly
[  FAILED  ] GPUSkinningPixelTest.BindPoseDrawsGeometryInCentreBandOnly (0 ms)
[ RUN      ] GPUSkinningPixelTest.TranslatedBoneMovesGeometryIntoRightBand
[  FAILED  ] GPUSkinningPixelTest.TranslatedBoneMovesGeometryIntoRightBand (0 ms)
[ RUN      ] GPUSkinningPixelTest.SkinnedProgramIsInUseAndBackgroundIsStable
[  FAILED  ] GPUSkinningPixelTest.SkinnedProgramIsInUseAndBackgroundIsStable (0 ms)
```

The `0 ms` durations and the complete absence of any GLSL compile output show
these tests never reached shader compilation or drawing.

Note there is a second 4.6 hint block near `OpenGLGraphicsFactory.cpp:140`,
belonging to the real `"Editor Offscreen"` rendering window. That is production
rendering and its 4.6 requirement is **not** in question here.

## 4. Superseded guidance

An earlier revision of this document suggested lowering the loader window to a
minimum version such as 3.3. **That suggestion is withdrawn and must not be
acted on.** Two independent findings contradict it:

1. The runner cannot create a 3.3 context either — the default context with no
   version hint already fails. Lowering the requested version changes nothing.
2. 3.3 would be unsafe even on a capable machine. See section 5.

A `gladLoadGL`-style strategy — resolving entry points by name without the
version gate — is likewise **not** the remedy for this runner. It cannot create
a context. It may still be worth considering separately on a machine that can
provide one, but it does not address the current failure.

## 5. The real lower bound is GL 4.5, not 3.3

Independently of the runner, the engine does **not** only need 3.3. Parsing all
19 version blocks in `third_party/glad/src/gl.c` and mapping the 110 glad
members the engine actually references gives this distribution:

| GL version of the called member | count |
|---|---|
**4.5** | **12** |
4.3 | 4 |
4.2 | 1 |
3.x and below | 93 |

The 12 GL 4.5 members are all Direct State Access entry points, each confirmed
as a real call site rather than a name collision, all on the production buffer
path in `GL46Device.cpp` (a clean, non-HRC file) via `GladGLContext& gl = *m_GL`:

```
GL46Device.cpp:178  gl.CreateBuffers(1, &glBuf);
GL46Device.cpp:202  gl.NamedBufferStorage(glBuf, ...);
GL46Device.cpp:205  gl.MapNamedBufferRange(glBuf, ...);
GL46Device.cpp:225  gl.NamedBufferSubData(glBuf, ...);
```

`gladLoadGLContext` is version gated, so a 3.3 load would leave every one of
these pointers NULL and break `CreateBuffer` outright. **GL 4.5 is the real
lower bound.** This finding matters for any future environment choice: an
environment providing less than 4.5 cannot run this engine's buffer path at all.

Two related details, for completeness:

- The only GL 4.6 member referenced anywhere in the engine,
  `NamedRenderbufferStorageMultisampleCoverageEXT`, appears solely in **comments**
  (e.g. `OpenGLAntiAliasing.cpp:162`). The genuine upper bound is 4.5, not 4.6.
- `.BindBufferBase` illustrates why member-level analysis is not sufficient on its
  own. Its function pointer has been core since GL 3.0, but all five engine call
  sites pass `GL_SHADER_STORAGE_BUFFER`, a 4.3 target. A populated pointer is not
  automatically a safe one.

## 6. Linux software GL: capable, but blocked by a build dependency

A standalone probe (`tools/gl_capability_probe.cpp`, job `gl-capability-probe` on
`ubuntu-latest`) was run under the same Xvfb plus software GL configuration the CI
Test step already uses. Measured on run `37989594539`:

```
GL_VERSION    = 4.5 (Compatibility Profile) Mesa 25.2.8-0ubuntu0.24.04.4
GL_VENDOR     = Mesa
GL_RENDERER   = llvmpipe (LLVM 20.1.2, 256 bits)
GLSL          = 4.50
profile mask  = 0x2 (compatibility)
--------------------------------------------------
4.5 core context: CREATED  (reports 4.5 (Core Profile))
4.6 core context: CREATE FAILED
```

Identical results with `GALLIUM_DRIVER=llvmpipe` forced and with default driver
selection. Note what this confirms and what it does not: it was measured, not
inferred from a Mesa feature list, and it shows 4.6 core creation genuinely fails
even though the driver advertises a 4.6 feature set elsewhere.

**The GPU pixel tests request 3.3 core and the engine's highest real call
requirement is 4.5. Linux therefore satisfies the pixel tests' context
requirement, with headroom.**

### The actual blocker is the build, not OpenGL support

The Linux build fails on a Windows-only include:

```
engine/CMakeFiles/EngineCore.dir/src/RHI/RHIWindow.cpp.o
RHIWindow.cpp:105 -> #include <windows.h>  ->  not found
```

The dependency is hard and has no partial path:

```
test_renderer
  -> target_link_libraries(... COMMON_LIBS ...)      tests/CMakeLists.txt:19
     COMMON_LIBS = EngineCore
       -> EngineCore STATIC                          engine/CMakeLists.txt:70
          -> src/RHI/*.cpp                           engine/CMakeLists.txt:30  (unconditional)
             -> RHIWindow.cpp
                #define GLFW_EXPOSE_NATIVE_WIN32     L22   (no platform guard)
                #include <GLFW/glfw3native.h>        L23
```

`src/RHI/*.cpp` is globbed into `ENGINE_SOURCES` with no condition, whereas the
adjacent `src/D3D12/*.cpp` is guarded by `if(WIN32 AND EXISTS ...)`. `EngineCore`
is a single static library, so no subset of it can be linked.

### What must not be done to claim a pass

- Building against stale `EngineCore` or other pre-existing artefacts. The
  recorded failure is from a **fresh checkout built from scratch** in run
  `37989594539`; only a from-scratch build counts as evidence either way.
- Suppressing or excluding `RHIWindow.cpp` to get a green job.
- Narrowing the all-target build gate.
- Converting a failed context, shader or pixel assertion into a skip.

## 7. Action required from the owners

**GPU pixel verification stays part of CI.** It is not being downgraded to a
local-only check. Two decisions are now separable.

### RHI owner — make the Linux build boundary correct

Decide how to handle `RHIWindow.cpp`'s Windows-only dependency so the Linux build
can produce `test_renderer`. Either guard the Win32 include and the native window
access, or split `src/RHI/*.cpp` per platform in `engine/CMakeLists.txt` the way
`src/D3D12/*.cpp` already is.

This is a build boundary fix, not a way to make a GPU job green. `RHIWindow`
cannot function on Linux today, and the OpenGL path the pixel tests use is
unaffected by it. Files involved: `engine/src/RHI/RHIWindow.cpp`,
`engine/CMakeLists.txt`.

### Verification environment owner — run the pixel tests once the build works

With the build unblocked, run the four GPU pixel cases on the Linux software GL
path. The Test step already provides `xvfb-run -a -s "-screen 0 1280x800x24"` and
`LIBGL_ALWAYS_SOFTWARE=1`; the environment is measured to be sufficient.

Acceptance is that all four cases **actually execute and pass**:

- `BindPoseDrawsGeometryInCentreBandOnly`
- `TranslatedBoneMovesGeometryIntoRightBand`
- `SkinnedProgramIsInUseAndBackgroundIsStable`
- `ProductDrawBranchChangesPixelsWithPose`

A probe reporting success, a SKIP, or a `0 ms` failure does not count.

The production 3D viewport is a **separate** item: it requests 4.6 core, which
this environment cannot provide. Resolving that is not implied by the pixel tests
passing, and rendering semantics must not change to accommodate it.

Constraints to preserve either way:

1. A failed context creation, a failed shader compile, or a failed pixel
   assertion must still **FAIL**. Do not convert these to skips.
2. The production rendering window keeps its 4.6 requirement.
3. Any environment chosen must provide at least GL 4.5 core, per section 5.

## 8. Verification assets

`GLRunnerCapabilityProbe.ReportRunnerOpenGLCapability`
(`tests/test_renderer/GL46DeviceTest.cpp`) reports the runner's capability and
records the default-context result separately from any explicit-version attempt.
It is a **diagnostic**, not an assertion: capability is not a correctness
failure, so it skips rather than fails when no context is obtainable and stays
runnable on machines without a usable driver.

**Its PASS means the diagnostic ran, not that OpenGL is available.** The
`[GLPROBE] default context: CREATE FAILED` line is the environment evidence. This
distinction is deliberate and must be preserved, so the probe cannot be mistaken
for the GPU verification passing.

Re-running it is the cheapest way for any future maintainer to re-check the
current runner instead of rediscovering the limitation through a failing GPU test.

## 9. How to verify once an environment is chosen

- Local: `tests/test_renderer` is expected to be 13/13, with
  `GPUSkinningPixelTest.*` 3/3. On a machine with a working GL 4.5+ context the
  probe should report `GL_VERSION` and create both the 4.5 and 4.6 core contexts.
- CI: run the chosen job on `avalonia` via `workflow_dispatch` — the workflow's
  `push` trigger listens only to `master`, so pushing to `avalonia` alone will not
  start a run. Confirm the three `GPUSkinningPixelTest` cases **actually execute**
  rather than failing at `0 ms`, and report their individual results, not only the
  suite summary.

## 10. Out of scope

- The GPU pixel tests and `assets/shaders/skinned_lit.*` are validated and are not
  to be reverted or weakened.
- RHI Linux portability, test fixtures, MSVC ASan interception, Vulkan/VMA are
  separate tracked items.
- No HRC source is modified by this item; the raiser holds no authorisation to do so.
