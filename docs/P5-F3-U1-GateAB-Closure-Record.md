# P5-F3-U1 — Gate A / Gate B Closure Record

**Status**: PASS — GATE A + GATE B CLOSED; ALL FOUR EXCEPTIONS RESOLVED  
**Branch**: avalonia  
**Precondition**: Source alignment at cd33c77 (25/25 both configs); Gate A1 scoped adapter diagnostic at 766ed9e  
**Date**: 2026-11-05  

---

## 1. Purpose

Close the four exceptions accepted in `docs/P5-F3-U1-Source-Alignment-Record.md`:

| Exception | Prior status | New status |
|-----------|-------------|------------|
| Jolt Solid Fill GL Pixel Readback | PARTIAL — CALL-PATH VERIFIED; GL READBACK PENDING | **RESOLVED — GATE B PASS** |
| Adapter SEH (0xC0000005) at C0 | PENDING — SEH INVESTIGATION | **RESOLVED — TEST FIXTURE ORDERING** |
| Gate A (adapter solid-fill GL evidence) | PENDING — ADAPTER SEH BLOCKER | **PASS** |
| Gate B (Jolt solid-fill pixel proof) | PARTIAL — callback evidence only | **PASS** |

---

## 2. Adapter SEH Root Cause & Resolution (Gate A1, commit 766ed9e)

- `DebugDrawGLFixture::Teardown()` destroyed the `GladGLContext`/FBO while
  `OpenGLPhysicsDebugDraw3D dd` was still alive; `dd`'s destructor then drained
  GL delete calls through the dangling `GladGLContext& m_GL` -> SEH fault
  reported as 0xC0000005.
- The fault was in the unqualified draft's **test fixture ordering**, not in
  the production Adapter path (`engine/src/OpenGL/Resources/OpenGLPhysicsDebugDraw3D.cpp`).
- Minimal fix: scope `dd` inside a block; destruction precedes
  `fixture.Teardown()`. No production source change.
- Evidence: `DebugDrawGLTest.PV2_ScopedAdapterDiagnostic` passes Debug/Release.

---

## 3. Gate A — Adapter Solid-Fill GL Pixel Evidence (PASS)

Test: `DebugDrawGLTest.PV2_ScopedAdapterDiagnostic` (766ed9e)

- `OpenGLPhysicsDebugDraw3D.DrawTriangle(solid)` + `Flush()` into a 128x128 FBO.
- GL readback: center pixel = **255 0 0 255** (Debug & Release); corner = background.
- Confirms `GL_TRIANGLES` submission path (`SubmitTriangles`, P4-CX-C2 `adf542c`)
  rasterizes solid fill with depth test and blending.

---

## 4. Gate B — Jolt Solid Fill End-to-End Pixel Proof (PASS)

Test: `DebugDrawGLTest.JoltSolidFillPixelReadbackEndToEnd` (this slice)

Full production chain, no mocks:

```
JoltPhysicsWorld (Static Box, dual DrawBodies)
  -> JoltDebugRenderer TLS buffers (DrawLine + DrawTriangle solid)
  -> IPhysicsDebugDraw3D = Engine::OpenGLPhysicsDebugDraw3D
  -> SubmitLines (GL_LINES) + SubmitTriangles (GL_TRIANGLES)
  -> FBO 128x128 -> glReadPixels
```

- Assertions do not depend on Jolt palette constants:
  1. Filled-pixel count (**> 2500** actual run filled = face-sized area at the
     NDC square [-0.5, 0.5]^2 ~ 4k px; wireframe-only peaks ~1.5k thin pixels);
  2. center pixel non-background — actual run: **127 127 127 254** (Debug & Release);
  3. background corner (0,0) stays background.
- This is pixel evidence, not callback evidence: solid fill traverses the
  entire Jolt -> renderer -> GL -> framebuffer chain.

---

## 5. Restored Test Chain (this slice actual coordinates)

| Target | Debug | Release |
|--------|-------|---------|
| `test_debugdraw` | 27/27 PASS | 27/27 PASS |
| `test_renderer` | 23/23 PASS | 23/23 PASS |
| `test_core` | 71/71 PASS | 71/71 PASS |
| `test_bridge` | 113/113 PASS | 113/113 PASS |
| `test_pick` | 10/10 PASS | 10/10 PASS |
| **Total** | **244/244** | **244/244** |

Note on counts: Source Alignment Record's table reported 25/25/25 per
target rows (arithmetic total printed as 245) before Gate A1; at Gate A1 the
count rose to 26 (scoped diagnostic) and at this slice to 27 (Gate B E2E),
for totals 244/244 in both configs. `JPH_DEBUG_RENDERER` reaches the test
target in both configurations (target-level Debug-gated define plus Jolt
library's own public define; `DEBUG_RENDERER_IN_DEBUG_AND_RELEASE` is the
engine-side functional contract, see `third_party/CMakeLists.txt`).

## 6. Build Chain Verification (clean)

| Target | Debug | Release |
|--------|-------|---------|
| EngineCore | 0 errors | 0 errors |
| EditorBridge | 0 errors | 0 errors |
| editor_avalonia (dotnet) | Build succeeded | (managed Debug only) |

Residual warnings are pre-existing (NU1900 nuget index reachability,
CS8602/CS0414) and unchanged by this slice.

## 7. Scope and Boundaries

- No production source change (engine/, bridge/ untouched).
- Test sources: `tests/test_debugdraw/test_debugdraw.cpp` only (new Gate B
  test + Gate A1 scoped diagnostic retained).
- `#if 0` drafts (`OpenGLPhysicsDebugDrawSolidFillReadback`,
  `OpenGLPhysicsDebugDrawWireframeNoFill`, `P5F3_Sanity`) remain non-qualified;
  their functional content is now covered by the two qualified tests.
- Dirty `third_party` submodules (d3d12ma, openal-soft, vma) and
  `crashes/` artifacts preserved, untouched.

## 8. Acceptance

`P5-F3-U1 — PASS — GATE A/B CLOSED; GL PIXEL READBACK VERIFIED`
EOF
