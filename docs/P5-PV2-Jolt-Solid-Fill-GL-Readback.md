# P5-PV2 — Jolt Solid Fill GL Readback Verification Record

**Status**: PARTIAL — JOLT CALL-PATH VERIFIED; GL READBACK PENDING  
**Branch**: avalonia  
**HEAD**: (pending commit)  
**Date**: 2026-11-04  
**Precondition**: P5-PV0 PASS

---

## 1. Jolt Call-Path Verification (PASS)

Test: `DebugDrawAdapterTest.JoltDualDrawBodiesInvokesSolidFill`

- Sets up JoltPhysicsWorld with a Static Box collider.
- Uses a counting mock `IPhysicsDebugDraw3D` tracking `DrawLine` and `DrawTriangle(wireframe)`.
- Calls `world.DebugDraw()` with the dual DrawBodies configuration (wireframe pass + solid fill pass).
- **Result**: `triangleSolidCount > 0` — Jolt invoked `DrawTriangle(wireframe=false)` (solid fill). **PASS**.
- `lineCount > 0` — Jolt invoked `DrawLine` (wireframe edges). **PASS**.

This confirms the dual DrawBodies configuration (P4-CX-SF1, commit `cf42133`) successfully enables Jolt to invoke the Solid Geometry Fill code path.

## 2. OpenGL Adapter Code Verification (PASS — Code Level)

`OpenGLPhysicsDebugDraw3D` (commit `adf542c`):
- `DrawTriangle(wireframe=false)` stores 3 `TriangleVertex` in `m_Triangles`.
- `SubmitTriangles()` submits `GL_TRIANGLES` with `GL_DEPTH_TEST` (GL_LESS) and blending.
- `Flush()` calls `SubmitLines()` + `SubmitTriangles()`.
- `Clear()` resets both `m_Lines` and `m_Triangles`.

## 3. GL Readback Verification (PENDING)

A GL readback test for `OpenGLPhysicsDebugDraw3D` solid fill was attempted but encountered a runtime SEH exception (0xc0000005) in the test fixture. The root cause requires further investigation of the GL context state in the test environment.

The `OpenGLPhysicsDebugDraw3D` solid fill code is verified at the code level (DrawTriangle stores TriangleVertex, SubmitTriangles uses GL_TRIANGLES with depth test). The Jolt call-path verification confirms Jolt invokes DrawTriangle (solid fill). The remaining gap is the actual GL pixel readback.

## 4. Wireframe Regression (PASS)

- test_debugdraw: 27/27 PASS (was 26, +1 Jolt solid fill call-path test).
- No regression from Jolt dual DrawBodies or solid fill code.

## 5. Conclusion

`P5-PV2 — PARTIAL — JOLT CALL-PATH VERIFIED; GL READBACK PENDING`.

Jolt now invokes `DrawTriangle` (solid fill) in addition to `DrawLine` (wireframe edges) with the dual DrawBodies configuration. The Solid Geometry Fill code capability is activated at runtime. GL pixel readback verification for the OpenGL adapter requires a dedicated GL test infrastructure fix.

Accepted exception: GL readback for `OpenGLPhysicsDebugDraw3D` solid fill remains PENDING.

**Closure (2026-11-05)**: exception resolved — Gate A/B closed PASS with GL pixel readback evidence (adapter path: center pixel 255/0/0; Jolt end-to-end: center pixel 127/127/127 over a face-sized filled area). See `docs/P5-F3-U1-GateAB-Closure-Record.md`.
EOF