# Handoff: GLAD loader window requests a 4.6 core context it does not need

**Owner:** HRC owner of `engine/src/OpenGL/OpenGLGraphicsFactory.cpp`
**Raised by:** GPU skinning pixel verification work on branch `avalonia`
**Evidence run:** CI run `37935492302`
  <https://github.com/chenjiefeng2001/Game-Engine-Demo/actions/runs/37935492302>
**Commit under test:** `94390cf`
**Status:** awaiting owner assessment — no source change made by the raiser

---

## 1. Problem

`engine/src/OpenGL/OpenGLGraphicsFactory.cpp` creates a 1x1 hidden window named
`"GLAD Loader"` whose only purpose is to obtain a current context for
`gladLoadGLContext`. That window requests a **4.6 core** context.

On the real Windows Release runner that window creation **fails**. Across the
suite it fails **57 times**:

```
[error] [OpenGLGraphicsFactory] Failed to create temporary GLAD loader window
```

Because GLAD never initialises, the three GPU skinning pixel tests fail in the
fixture before any rendering occurs:

```
[ RUN      ] GPUSkinningPixelTest.BindPoseDrawsGeometryInCentreBandOnly
[  FAILED  ] GPUSkinningPixelTest.BindPoseDrawsGeometryInCentreBandOnly (0 ms)
[ RUN      ] GPUSkinningPixelTest.TranslatedBoneMovesGeometryIntoRightBand
[  FAILED  ] GPUSkinningPixelTest.TranslatedBoneMovesGeometryIntoRightBand (0 ms)
[ RUN      ] GPUSkinningPixelTest.SkinnedProgramIsInUseAndBackgroundIsStable
[  FAILED  ] GPUSkinningPixelTest.SkinnedProgramIsInUseAndBackgroundIsStable (0 ms)
```

The `0 ms` durations and the total absence of any GLSL compile output show the
tests never reached shader compilation or drawing.

## 2. Precise code location

Working-tree line numbers as observed at commit `94390cf`:

| Location | Content | Role |
|---|---|---|
| `engine/src/OpenGL/OpenGLGraphicsFactory.cpp:62` | `glfwCreateWindow(1, 1, "GLAD Loader", nullptr, nullptr)` | the loader window |
| `engine/src/OpenGL/OpenGLGraphicsFactory.cpp:66` | `int version = gladLoadGLContext(&m_GL, glfwGetProcAddress);` | loader invocation |

The GLFW version hints immediately preceding line 62 request `4` / `6` with
`GLFW_OPENGL_CORE_PROFILE`. Note there is a **second** `4.6` hint block near
line 140, belonging to the real `"Editor Offscreen"` rendering window; **that
one is production rendering and must not be changed by this fix.**

## 3. What is already established

- A **3.3 core context does create successfully on the same runner.** The
  previous run failed with `GL 4.6 core context creation failed`; after the GPU
  tests were moved to request 3.3 directly, that error no longer appears for
  these tests (the single remaining occurrence belongs to `GL46DeviceTest`,
  which requests 4.6 by design and skips).
- The fault is therefore in the **factory's GLAD loader path**, not in the
  shaders, the skinning logic, the uniform plumbing, or the pixel assertions.
- Locally, the pixel verification passes **3/3** at a 3.3 core context with real
  shader compilation, linking, `SetMat4Array`, offscreen draw and pixel
  readback. Regional pixel evidence: bind pose `centre=784, left=0, right=0`;
  bone translated +X `centre=140, left=0, right=448`.

## 4. Suggested direction (NOT a verified fix)

Have the owner determine the minimum context version the loader context actually
requires, and lower **only** the `"GLAD Loader"` window's hint accordingly.

**This is explicitly a candidate, not a proven remedy.** Section 5 explains why
it must not be applied unverified.

## 5. Acceptance conditions — the version gate is real

Lowering the loader window to 3.3 is **not** automatically safe. `gladLoadGLContext`
is version-gated, so the function table it produces depends on the version of the
context that is current **at load time**.

Confirmed in the vendored loader, `third_party/glad/src/gl.c`:

- `gladLoadGLContext` calls `glad_gl_find_core_gl` (`gl.c:3364` region), which
  reads the current context version and derives boolean flags, e.g.
  `gl.c:3352` `VERSION_3_3 = (major == 3 && minor >= 3) || major > 3;` and
  `gl.c:3359` `VERSION_4_6 = (major == 4 && minor >= 6) || major > 4;`
- Each version block then gates itself. `glad_gl_load_GL_VERSION_4_3` opens with
  `if(!context->VERSION_4_3) return;`, and the loader proceeds through
  `GL_VERSION_4_0` up to `GL_VERSION_4_6`.

Consequence: if GLAD is loaded while a 3.3 context is current, **every pointer
in the 4.0–4.6 groups stays NULL** — including ones the engine already calls:

| glad member | engine call sites | GL version |
|---|---|---|
| `.DispatchCompute` | 2 | 4.3 |
| `.MemoryBarrier` | 2 | 4.3 |
| `.GenVertexArrays` | 2 | 3.0 |
| `.BindBufferBase` | 5 | function pointer available from GL 3.0; whether a given call is valid depends on the buffer target passed |

On `.BindBufferBase`, distinguish the function from the binding target. The
function itself has been core since OpenGL 3.0, so its pointer would still be
populated by a 3.3 load. The target is what raises the requirement:
`GL_UNIFORM_BUFFER` requires 3.1, while `GL_SHADER_STORAGE_BUFFER` requires
**4.3**. All five engine call sites currently pass `GL_SHADER_STORAGE_BUFFER`:

- `engine/src/OpenGL/...`: `gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, s.binding, s.handle)`
- `gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, esbo)`
- `m_GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_ParticleSSBO)`
- `m_GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_CountSSBO)`
- `m_GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_ParticleSSBO)`

so these are 4.3-dependent in practice even though the entry point is not.
Verify each site's actual target rather than inferring from the function name.

Note that a NULL pointer is not the only failure mode here. `.BindBufferBase` is
a good illustration: its pointer *would* survive a 3.3 load, yet the engine
calls it with `GL_SHADER_STORAGE_BUFFER`, a 4.3 target. So a loader lowered to
3.3 can leave a **populated pointer leading to an invalid call** as well as NULL
4.x pointers. Both cases must be checked.

### Conditions to satisfy before accepting any fix

1. **Enumerate the real consumers.** Confirm which GL entry points above the
   chosen loader version are reachable from production paths, and confirm the
   table is populated before any such call. A NULL dereference here would turn a
   clear error into a crash.
2. **Verify the loaded table, not just the context.** Assert that the pointers
   required by the chosen loader version are non-NULL after `gladLoadGLContext`,
   and that any 4.x pointer the engine may reach is either NULL by design or
   populated.
3. **Leave the production rendering context alone.** The `"Editor Offscreen"`
   window near line 140 keeps its current 4.6 requirement. Rendering semantics
   must not change.
4. **Preserve the failure signal.** If the loader still cannot obtain a context,
   the existing `Failed to create temporary GLAD loader window` error must remain
   and must keep failing the test rather than degrading to a skip.
5. **Note the shared-table interaction.** `gladLoadGLContext` is called at three
   sites (`OpenGLGraphicsFactory.cpp:66`, `:155`, `:295`), and lines 66 and 155
   both target `m_GL`. Determine whether a lower loader window can leave `m_GL`
   partially populated or affect the later real-window load, and make the chosen
   approach consistent across all three sites.

## 6. How to verify

- Local: build the renderer and run `tests/test_renderer`. Expected for the
  pixel tests is **3/3**; the whole target **12/12**. Confirm no regression in
  the other `gladLoadGLContext` consumers.
- CI: re-run the Windows Release job on `avalonia` via `workflow_dispatch`
  (`push` alone will not trigger it — the workflow triggers only on `master`).
  Confirm the three `GPUSkinningPixelTest` cases **actually execute** rather
  than fail at `0 ms`, and report their individual pass/fail state, not only the
  suite result.
- Linux remains blocked by a separate, pre-existing RHI portability defect and
  is out of scope here.

## 7. Out of scope

- `RHI`, fixtures, ASan interception, Vulkan/VMA — separate tracked items.
- The GPU pixel tests and `assets/shaders/skinned_lit.*` are already validated
  and are **not** to be reverted or weakened as part of this fix.
- The raiser holds no authorisation to modify HRC-owned files and has made no
  source change for this item.
