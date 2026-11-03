# P4-E Gate B — Implementation Contract

**Status**: FROZEN — GATE B IMPLEMENTATION AUTHORIZED  
**Branch**: avalonia  
**HEAD**: 5e15d2b  
**Date**: 2026-11-03  
**Precondition**: P4-E Gate A FROZEN; P4-E1/E2/E3/M2 VERIFIED

---

## 1. Scope

P4-E Gate B authorizes controlled RenderGraph adoption into the P4-B Production 3D Viewport.

This is **not** a full renderer migration. It is a bounded integration:
- RenderGraph executes selected Static Mesh Draw passes in the Production Viewport.
- Direct Renderer remains the default and fallback path.
- ExecuteParallel remains disabled for production.

---

## 2. Frozen Boundaries

| Category | Decision |
|----------|----------|
| Backend | OpenGL 4.6 only |
| Context | Existing EngineHost OpenGL Context |
| Thread | Existing Runtime Owner Thread |
| Execution | Serial only |
| Renderer Path | Direct Renderer default; RenderGraph opt-in |
| Transient Aliasing | Disabled |
| MRT / Multi-Attachment | Out of scope for v1 |
| ExecuteParallel | Not in production path |
| T-5 DebugDraw API | Not modified |
| P4-B Camera ABI | Not modified |
| EditorBridge ABI | Not modified |
| P4-C Jolt DebugRenderer Contract | Not modified |

---

## 3. FBO State Contract (Frozen)

RenderGraph Pass execution shall observe:

1. Before each Pass: RenderGraph binds the write-target's `renderTargetFbo` via `BindNativeFramebuffer`.
2. Inside the Pass: Clear and Draw operate on the bound target FBO.
3. After Execute: `GL46CommandList::End` restores default FBO (binding 0).
4. After Execute returns: caller must not assume target FBO remains bound.
5. To present or read back output, caller explicitly binds the corresponding resource.
6. FBO Binding state is not confused with Attachment data validity.

The verified `renderTargetFbo` mechanism (M2) continues to be used, not generalized into a multi-backend resource architecture.

---

## 4. Graph Pass Contract

Minimum Production Pass sequence:

1. Viewport Target Preparation
2. Color/Depth Clear
3. Static Mesh Draw
4. Output Presentation

Resource access declarations must fully reflect actual operations:
- Render Target Write
- Mesh Buffer Read
- Other actual Resource Access

Dependency graph must guarantee Clear executes before Mesh Draw.

Pass declaration order must not be used as a substitute for resource access dependencies.

---

## 5. Production Viewport Integration (B1 Entry Criteria)

Renderer integration shall:

- Reuse existing EditorCamera parameters.
- Reuse existing MeshRenderer.
- Reuse existing Shader.
- Reuse existing RHI Resources (Vertex/Index Buffers).
- Reuse existing Production Render Target.
- Execute selected Static Mesh Draw via RenderGraph.
- After Graph Execute, explicitly complete output binding or Presentation.
- Propagate Execute's `bool` return result to the caller.

No new MeshRenderer. No rewritten Shader Pipeline. No bypass of EngineHost OpenGL Context Owner.

---

## 6. Feature Flag (Renderer Path Selection)

A controlled Renderer Path Selection shall exist:

- `Direct Renderer`
- `RenderGraph Renderer`

Initial default value: `Direct Renderer`.

RenderGraph is an explicitly-enabled optional path only.

Switching between paths must not require production data modification.

---

## 7. DebugDraw Compatibility

T-5 DebugDraw retains its existing direct drawing mechanism.

If DebugDraw must draw after RenderGraph:
- Must explicitly bind its target FBO.
- Must not rely on residual GL Binding after RenderGraph Execute.
- T-5 API is not modified.
- DebugDraw remains at the correct render stage.

---

## 8. Lifecycle & Failure Handling (B2 Entry Criteria)

### 8.1 Resize
- Viewport Resize
- Render Target Recreate
- FBO Attachment Recreate
- Resource Handle Update
- Graph Invalidation
- Next frame correctly Compile/Execute

RenderGraph must not retain a released Render Target Handle.

### 8.2 Scene/Project Lifecycle
- Scene switch
- Project switch
- Viewport Dispose
- Renderer Shutdown
- Editor restart
- OpenGL Context Shutdown

Imported Resources must not be destroyed by RenderGraph.

### 8.3 Failure Handling

**Compile Failure**:
- Do not execute an uncompiled Graph.
- Record failure state.
- Fall back to Direct Renderer if production state is safe.

**Execute Failure**:
- Stop current Graph.
- Passes depending on failed results must not continue.
- Do not present state-indeterminate output.
- Restore known-safe GL/Resource state first.
- Only then allow Direct Renderer.
- If unrecoverable, disable RenderGraph path and report error.

### 8.4 Execute Bool Contract

Since `RenderGraph::Execute` / `ExecuteParallel` now return `bool`:
- All call sites must handle `false`.
- No production call site may ignore the return value.
- No failure-continues-execution logic.
- ExecuteParallel is not in production path, but API change impact on existing callers must be checked.

---

## 9. Visual & Runtime Verification (B3 Entry Criteria)

RenderGraph Production path shall be verified in a real Editor/Viewport:

- Static Mesh visible.
- Mesh Transform correct.
- Camera View/Projection correct.
- Color Attachment output correct.
- Depth Test normal.
- Multi-frame sustained rendering.
- Resize normal.
- DPI Scaling normal.
- No black frames.
- No flickering.
- No abnormal Attachment.
- No persistent GL Error.

Direct vs Graph comparison using same Scene/Camera/Mesh/Shader/Render Target/Viewport Size. Record actual pixel difference. Tolerance based on real test conditions.

---

## 10. Regression & Performance Baseline (B4 Entry Criteria)

| Test Target | Reference |
|-------------|-----------|
| test_renderer | 22/22 |
| test_core | 71/71 |
| test_debugdraw | 24/24 |
| test_bridge | 113/113 |
| test_pick | 10/10 |

Additional: EngineCore Debug/Release, EditorBridge Debug/Release, editor_avalonia Debug/Release, related UIA/Viewport tests.

Performance comparison (Direct vs RenderGraph Production):
- CPU Frame Time
- RenderGraph Execute CPU Time
- GPU Timestamp (if applicable)
- P50/P95
- Per-frame Heap Allocation
- Pass count
- Resource count

Performance shortfalls must be recorded as Blocker or Accepted Exception, not ignored.

---

## 11. Gate B Closure Conditions

- Gate A contract honored.
- E1/E2/E3 verified capabilities preserved.
- Production Mesh Draw integrated successfully.
- Viewport output correct.
- Direct Renderer retained.
- Failure Handling normal.
- Debug/Release Regression PASS.
- Performance Evidence recorded.
- All exceptions explicitly recorded.

Suggested closure status: `P4-E GATE B — PASS — PRODUCTION PILOT VERIFIED`

---

## 12. Explicit Non-Goals

- Vulkan / D3D12 adoption.
- Transient Aliasing enabled.
- ExecuteParallel production use.
- Full Renderer migration.
- T-5 API modification.
- P4-C Jolt DebugRenderer contract changes.
- EditorBridge ABI modification.

---

## 13. Documents

Primary: this file (`docs/P4-E-GateB-Implementation-Contract.md`).

Supporting records (created per Slice):
- `docs/P4-E-B1-Production-Renderer-Integration.md`
- `docs/P4-E-B2-Lifecycle-and-Failure-Handling.md`
- `docs/P4-E-B3-Visual-and-Runtime-Verification.md`
- `docs/P4-E-B4-Regression-and-Performance.md`
- `docs/P4-E-GateB-Closure-Record.md`

---

## 14. Authorization

Git Push / Merge / Rebase / Reset / Clean: NOT authorized in this phase.  
Worktree dirty submodules and crashes/: preserved per Project-Delivery-Readiness.md.
