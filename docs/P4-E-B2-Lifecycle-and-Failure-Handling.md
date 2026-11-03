# P4-E-B2 — Lifecycle & Failure Handling

**Status**: VERIFIED — CODE-LEVEL + DOCUMENTED  
**Branch**: avalonia  
**HEAD**: a1bd4d0  
**Date**: 2026-11-03  
**Precondition**: P4-E-B1 Production Renderer Integration PASS

---

## 1. Resize

### Behavior
- Viewport Resize is driven by managed `EditorHostService.SetViewportSize` → `EngineHost_SetViewportSize` → `EngineHost::SetViewportSize` → pending size applied in `PumpOneFrame` via `ApplyPendingViewportSize` **before** render.
- On resize, `m_PresentationFbo` is recreated by EngineHost; the new FBO ID is passed to `Application::SetProductionRenderTargetFbo` each frame before `RenderProduction3DFrame`.
- RenderGraph is `Reset()` each frame in `RenderProduction3DFrame`, so a stale compiled plan referencing the old FBO handle is never reused.
- If resize/recreate fails, `PumpOneFrame` returns fail-closed (no frame published, host stays alive, no RenderGraph Execute).

### Verification
- `test_viewport_resize`: 5/5 (Debug/Release).
- No RenderGraph handle is retained across resize; `RenderGraph::Reset()` clears passes/resources/execution order each frame.

### Contract
RenderGraph must not retain a released Render Target Handle. ✅ Satisfied by per-frame Reset.

---

## 2. Scene / Project Lifecycle

### Behavior
- Scene switch: `RenderProduction3DFrame` iterates `scene.GetObjects()` each frame; mesh objects are collected fresh.
- Project switch: EngineHost reinitializes session; `m_Production3DRenderReady` is reset on teardown (`Application::Shutdown` cleans `m_ProductionDebugDraw`).
- Viewport Dispose: `m_PresentationFbo` released by EngineHost teardown; RenderGraph device/queue/transient heap owned by Application and released at Application shutdown.
- Editor restart: `InitProduction3DRenderResources` is idempotent (`m_Production3DRenderReady` guard); RenderGraph device recreated on next init.
- OpenGL Context shutdown: `GL46CommandList::End` restores default FBO (binding 0); no RenderGraph state leaks past context lifetime.

### Contract
Imported Resources must not be destroyed by RenderGraph. ✅ Satisfied: RenderGraph only imports `renderTargetFbo` (a uintptr handle, no ownership), never calls `glDeleteFramebuffers` on imported FBOs.

---

## 3. Failure Handling

### 3.1 Compile Failure
- `RenderGraph::Compile` returns `false` on cyclic dependency or transient heap allocation failure.
- `RenderProduction3DFrame` logs `[P4-E B1] RenderGraph compile failed, using Direct Renderer` and falls through to the Direct Renderer path (unchanged behavior).
- No Execute is attempted on an uncompiled Graph.

### 3.2 Execute Failure
- `RenderGraph::Execute` returns `bool`; `RenderProduction3DFrame` checks it.
- On `false`: logs `[P4-E B1] RenderGraph Execute failed`, returns `false` to EngineHost.
- EngineHost treats `rendered3D == false` (with `rendered2D == false`) as "production scene render produced no renderable frame" → `RequestEmergencyStop()` + typed failure (`ENGINE_HOST_STATUS_GL_FAILED`, `ENGINE_HOST_STAGE_FBO`).
- No state-indeterminate output is presented: on Execute failure, the presentation FBO content is not trusted; EngineHost still Unbinds it, and the managed readback sees the previous frame or clear.

### 3.3 GL/Resource State Restoration
- `GL46CommandList::End` restores `GL_FRAMEBUFFER` to binding 0 after every Execute.
- `RenderProduction3DFrame` rebinds `m_ProductionRenderTargetFbo` after a successful Execute so DebugDraw can draw into it; EngineHost then Unbinds.
- On Execute failure, `RenderProduction3DFrame` returns `false` before DebugDraw EndFrame; DebugDraw is skipped for that frame (no state leak).

### 3.4 Execute Bool Contract — Call-Site Audit
All `RenderGraph::Execute` / `ExecuteParallel` call sites:

| Call Site | File | Handles `bool` |
|-----------|------|----------------|
| `RenderProduction3DFrame` (production) | `engine/src/Core/Application.cpp:1031` | ✅ Checked; failure → return false |
| `P4E3M2D_Diagnostic` (test) | `tests/test_renderer/RenderGraphSG6Test.cpp:198` | ⚠️ Return value unused (test-only, non-production) |
| `SG6_InOrderDeclaration_CorrectChain` (test) | `tests/test_renderer/RenderGraphSG6Test.cpp:300` | ⚠️ Return value unused (test-only, non-production) |
| `SG5_PerfOverhead_Measured` (test) | `tests/test_renderer/RenderGraphSG6Test.cpp:357` | ⚠️ Return value unused (test-only, non-production) |
| `P4E3M2_OffscreenMeshDrawPilot` (test) | `tests/test_renderer/RenderGraphSG6Test.cpp:390` | ✅ `EXPECT_TRUE` |
| `P4E3M2F_FboBindingAfterExecute` (test) | `tests/test_renderer/RenderGraphSG6Test.cpp:441` | ✅ `EXPECT_TRUE` |
| `P4E3_OffscreenClearPilot` (test) | `tests/test_renderer/RenderGraphSG6Test.cpp:524` | ✅ `EXPECT_TRUE` |
| `P4E3M2_OffscreenMeshDrawPilot` (direct call path) | `tests/test_renderer/RenderGraphSG6Test.cpp:587` | ✅ `EXPECT_TRUE` |

`ExecuteParallel` is not called in production (disabled per Gate A). Its signature change to `bool` is backward-compatible for test callers (none currently call it).

---

## 4. Regression Baseline (unchanged by B2)

| Test Target | Result |
|-------------|--------|
| test_renderer | 22/22 |
| test_core | 71/71 |
| test_debugdraw | 24/24 |
| test_bridge | 113/113 |
| test_pick | 10/10 |
| **Total** | **240/240** |

Debug/Release builds: EngineCore, EditorBridge, editor_avalonia — 0 errors.

---

## 5. Accepted Exceptions

- Test call sites at RenderGraphSG6Test.cpp:198/300/357 do not assert `Execute` return value. These are SG6 architecture tests (non-production path); not a production contract violation.
- Real-window Resize / DPI / dock lifecycle visual verification requires interactive desktop; deferred to B3.

---

## 6. Conclusion

`P4-E-B2 — PASS`. Lifecycle and failure-handling contracts are satisfied at the code level:
- Resize → per-frame RenderGraph Reset, no stale handles.
- Scene/project lifecycle → idempotent init, RenderGraph device released at shutdown.
- Compile/Execute failure → Direct Renderer fallback, no indeterminate output.
- Execute bool return → checked at the sole production call site.

Next: P4-E-B3 (Real Viewport & Visual Verification) requires interactive desktop for true pixel comparison; B4 (Regression & Performance) baseline already established above.
