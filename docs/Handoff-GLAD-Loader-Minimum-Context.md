# Handoff: the Windows Release runner cannot create an OpenGL context

**Owner:** HRC owner of `engine/src/OpenGL/OpenGLGraphicsFactory.cpp`, together
with whoever owns the CI environment decision
**Raised by:** GPU skinning pixel verification work on branch `avalonia`
**Evidence runs:**
- CI run `37935492302` (commit `94390cf`) — loader failure and GPU test failure
  <https://github.com/chenjiefeng2001/Game-Engine-Demo/actions/runs/37935492302>
- CI run `37957896840` (commit `5b96bb9`) — direct runner capability probe
  <https://github.com/chenjiefeng2001/Game-Engine-Demo/actions/runs/37957896840>
**Status:** awaiting owner decision on the verification environment

---

## 1. Summary

The Windows Release runner **cannot create a GLFW OpenGL context at all**. A
default context, created with no version hint whatsoever, fails:

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

## 2. Why the tests fail

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

## 3. Superseded guidance

An earlier revision of this document suggested lowering the loader window to a
minimum version such as 3.3. **That suggestion is withdrawn and must not be
acted on.** Two independent findings contradict it:

1. The runner cannot create a 3.3 context either — the default context with no
   version hint already fails. Lowering the requested version changes nothing.
2. 3.3 would be unsafe even on a capable machine. See section 4.

A `gladLoadGL`-style strategy — resolving entry points by name without the
version gate — is likewise **not** the remedy for this runner. It cannot create
a context. It may still be worth considering separately on a machine that can
provide one, but it does not address the current failure.

## 4. The real lower bound is GL 4.5, not 3.3

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

## 5. Action required from the owner

**Decide which CI environment will host the GPU pixel verification.** The
verification stays part of CI; it is not being downgraded to a local-only check.
The decision is about where it runs, not whether it exists.

Suggested first avenue, to be assessed rather than assumed:

- The Linux path already provisions `xvfb-run -a -s "-screen 0 1280x800x24"`
  with `LIBGL_ALWAYS_SOFTWARE=1` (see `.github/workflows/cmake-multi-platform.yml`),
  which is the existing mechanism for supplying a display and software GL. This
  is independent of the Windows runner's capability problem.

**Known constraint:** the Linux build is currently blocked by a separate,
pre-existing RHI portability defect, so the software GL path cannot be assumed
to work until that is resolved. The RHI blocker must **not** be worked around in
order to make a GPU job pass — no suppressing RHI, no dropping the all-target
build gate. If the current Linux build boundary prevents execution, coordinate a
usable verification environment through the appropriate owner instead of
retracting the tests.

Constraints to preserve in whichever environment is chosen:

1. A failed context creation, a failed shader compile, or a failed pixel
   assertion must still **FAIL**. Do not convert these to skips.
2. The production rendering window keeps its 4.6 requirement; rendering semantics
   are unchanged.
3. The environment must provide at least GL 4.5 core, per section 4.

## 6. Verification assets

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

## 7. How to verify once an environment is chosen

- Local: `tests/test_renderer` is expected to be 13/13, with
  `GPUSkinningPixelTest.*` 3/3. On a machine with a working GL 4.5+ context the
  probe should report `GL_VERSION` and create both the 4.5 and 4.6 core contexts.
- CI: run the chosen job on `avalonia` via `workflow_dispatch` — the workflow's
  `push` trigger listens only to `master`, so pushing to `avalonia` alone will not
  start a run. Confirm the three `GPUSkinningPixelTest` cases **actually execute**
  rather than failing at `0 ms`, and report their individual results, not only the
  suite summary.

## 8. Out of scope

- The GPU pixel tests and `assets/shaders/skinned_lit.*` are validated and are not
  to be reverted or weakened.
- RHI Linux portability, test fixtures, MSVC ASan interception, Vulkan/VMA are
  separate tracked items.
- No HRC source is modified by this item; the raiser holds no authorisation to do so.
