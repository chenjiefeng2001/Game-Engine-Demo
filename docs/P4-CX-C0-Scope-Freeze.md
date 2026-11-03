# P4-CX-C0 — Jolt DebugRenderer Completeness Scope Freeze

**Status**: FROZEN  
**Branch**: avalonia  
**HEAD**: d5e877f  
**Date**: 2026-11-03  
**Precondition**: P4-E Gate B CLOSED; P4-C initial scope CLOSED — SCOPE-BOUND

---

## 1. Purpose

P4-CX (Jolt DebugRenderer Completeness) is an **independent follow-up task** to P4-C. It does **not** reopen the P4-C closure record. It addresses the gap identified in P4-C: Solid Debug Geometry Fill was never verified.

---

## 2. Current Verified Capabilities (P4-C baseline)

| Capability | Status | Evidence |
|------------|--------|----------|
| DrawLine (collider wireframe) | ✅ VERIFIED | JoltDebugRenderer::DrawLine → m_Draw->DrawLine |
| DrawText3D position markers | ✅ VERIFIED | JoltDebugRenderer::DrawText3D draws 3-axis cross |
| JoltDebugRenderer wired to JoltPhysicsWorld | ✅ VERIFIED | JoltPhysicsWorld::DebugDraw uses JoltDebugRenderer |

---

## 3. Identified Gaps

| Gap | Detail |
|-----|--------|
| Solid Geometry Fill | `JoltDebugRenderer::DrawTriangle` splits the triangle into 3 `Line`s (wireframe edges only). No `Triangle` storage, no `m_Draw->DrawTriangle(..., false)` call. |
| OpenGLPhysicsDebugDraw3D::DrawTriangle(wireframe=false) | No-op (empty body). No triangle vertex storage, no `GL_TRIANGLES` submission. |
| Solid triangle visible output | Not verified. No real OpenGL solid-triangle evidence. |

---

## 4. Scope (Frozen)

### In scope (P4-CX-C1..C4)
1. `JoltDebugRenderer::DrawTriangle` stores `Triangle{v0,v1,v2,color}` instead of 3 `Line`s.
2. `JoltDebugRenderer::Flush` calls `m_Draw->DrawTriangle(v0,v1,v2,color, false)` (wireframe=false → solid fill) per stored Triangle.
3. `OpenGLPhysicsDebugDraw3D` supports Triangle storage + `SubmitTriangles()` (`GL_TRIANGLES`).
4. `OpenGLPhysicsDebugDraw3D::DrawTriangle(wireframe=false)` stores triangle vertices; `Flush` renders Lines + Triangles.
5. `DrawLine` → `m_Draw->DrawLine` unchanged (collider wireframe preserved).
6. `DrawText3D` position-marker behavior unchanged (3-axis cross, `inString` unused).
7. `JoltPhysicsWorld::DebugDraw` drawSettings unchanged (`mDrawShapeWireframe = true`).

### Out of scope
- Real 3D text rendering for DrawText3D (`inString`).
- Solid Sphere/Box/Capsule (they early-return when `!wireframe`).
- Modifying Jolt upstream or JoltPhysicsWorld drawSettings.
- T-5 DebugDraw API changes.
- P4-B/P4-E contracts.

---

## 5. Known Uncertainty (recorded, not a blocker)

`JoltPhysicsWorld::DebugDraw` sets `drawSettings.mDrawShapeWireframe = true`. Whether Jolt's `DrawBodies` invokes `DrawTriangle` (solid faces) when wireframe=true depends on Jolt's internal implementation (headers not in repo). If Jolt does not invoke `DrawTriangle`, Solid Fill will not be visible. Remedy (independent decision, not part of C0): set `mDrawShapeWireframe = false` in `JoltPhysicsWorld::DebugDraw` at the cost of wireframe edges.

Solid Fill **code capability** is delivered regardless; **visible output** depends on Jolt invocation.

---

## 6. Exit Criteria (P4-CX-C4)

- `JoltDebugRenderer::DrawTriangle` stores Triangle; `Flush` calls `m_Draw->DrawTriangle(..., false)`.
- `OpenGLPhysicsDebugDraw3D` renders solid triangles (`GL_TRIANGLES`) with depth test.
- Wireframe `DrawLine` → `m_Draw->DrawLine` preserved (no collider wireframe regression).
- `DrawText3D` position markers preserved.
- Debug/Release tests pass.
- Real OpenGL solid-triangle output verified (test with GL context).
