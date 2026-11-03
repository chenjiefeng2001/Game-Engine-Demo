# P5-F3-G — Jolt Solid Fill GL Verification Record

**Status**: PARTIAL — JOLT CALL-PATH VERIFIED; GL READBACK PENDING  
**Branch**: avalonia  
**HEAD**: (pending commit)  
**Date**: 2026-11-03  
**Precondition**: P4-CX CLOSED — SCOPE-BOUND

---

## 1. Jolt Call-Path Verification (PASS)

New test: `DebugDrawAdapterTest.JoltDualDrawBodiesInvokesSolidFill`

- Sets up JoltPhysicsWorld with a Static Box collider.
- Uses a counting mock `IPhysicsDebugDraw3D` that tracks `DrawLine` and `DrawTriangle(wireframe)` calls.
- Calls `world.DebugDraw()` with the dual DrawBodies configuration (wireframe pass + solid fill pass).
- **Result**: `triangleSolidCount > 0` — Jolt invoked `DrawTriangle(wireframe=false)` (solid fill). **PASS**.
- `lineCount > 0` — Jolt invoked `DrawLine` (wireframe edges). **PASS**.

This confirms that the dual DrawBodies configuration (added in P4-CX-SF1, commit `cf42133`) successfully enables Jolt to invoke the Solid Geometry Fill code path.

## 2. GL Readback Verification (PENDING)

A GL readback test for `OpenGLPhysicsDebugDraw3D` solid fill (`GL_TRIANGLES` submission) was attempted but encountered a runtime SEH exception (0xc0000005) in the test fixture. The root cause requires further investigation of the GL context state in the test environment.

The `OpenGLPhysicsDebugDraw3D` solid fill code (added in P4-CX-C2, commit `adf542c`) is verified at the code level:
- `DrawTriangle(wireframe=false)` stores 3 `TriangleVertex` in `m_Triangles`.
- `SubmitTriangles()` submits `GL_TRIANGLES` with `GL_DEPTH_TEST` (GL_LESS).
- `Flush()` calls `SubmitLines()` + `SubmitTriangles()`.

## 3. Wireframe Regression (PASS)

- test_debugdraw: 26/26 PASS (was 24, +2 new Jolt tests).
- test_renderer: 22/22 PASS.
- EngineCore Debug/Release: 0 errors.
- No regression from dual DrawBodies or solid fill code.

## 4. Conclusion

`P5-F3 — PARTIAL — JOLT CALL-PATH VERIFIED; GL READBACK PENDING`.

Jolt now invokes `DrawTriangle` (solid fill) in addition to `DrawLine` (wireframe edges) with the dual DrawBodies configuration. The Solid Geometry Fill code capability is activated at runtime. GL pixel readback verification for the OpenGL adapter requires a dedicated GL test infrastructure fix.

Accepted exception: GL readback for `OpenGLPhysicsDebugDraw3D` solid fill remains PENDING.
EOF