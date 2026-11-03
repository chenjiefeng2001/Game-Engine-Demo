# P5-F1M — Managed Renderer Selection & Real Editor/UIA Verification

**Status**: PARTIAL — ABI IMPLEMENTED; REAL UIA PENDING  
**Branch**: avalonia  
**HEAD**: (pending commit)  
**Date**: 2026-11-03  
**Precondition**: P5-F2 PASS

---

## 1. ABI Contract Verification (PASS)

### Existing Additive ABI

| Component | Status |
|-----------|--------|
| `bridge/include/editor_bridge/capi.h` — `EngineHost_SetProductionUseRenderGraph` / `EngineHost_GetProductionUseRenderGraph` | ✅ Declared (additive) |
| `bridge/src/capi.cpp` — C ABI implementations (thin forwarders) | ✅ Implemented |
| `bridge/src/EngineHost.h` — `SetProductionUseRenderGraph(bool)` / `ProductionUseRenderGraph()` | ✅ Implemented (forwarding to Application) |
| `editor_avalonia/Native/EditorBridgeApi.cs` — P/Invoke declarations | ✅ Implemented |
| `editor_avalonia/Services/EditorHostService.cs` — `EngineHostChannel` methods + `EditorHostService` wrapper | ✅ Implemented |

### ABI Compatibility
- No existing struct layout modified.
- No enum numeric values changed.
- No existing function contract changed.
- `RuntimeResult` (76B), `EV_COUNT` (14), `EngineLogRecordV1` (2392B) unchanged.
- Default remains Direct Renderer (flag defaults to false).

### Threading Contract
- Managed UI thread calls `SetProductionUseRenderGraph` which delegates to `EngineHostChannel` → C ABI → `EngineHost` → `Application::SetProductionUseRenderGraph`.
- Native `Application::RenderProduction3DFrame` reads `m_ProductionUseRenderGraph` on the Runtime Owner Thread during `PumpOneFrame`.
- Frame Boundary: the flag is read at the start of each `RenderProduction3DFrame` call, ensuring consistent behavior within a frame.

## 2. Managed Binding & ViewModel (IMPLEMENTED)

- `MainViewModel.SetRendererPath(bool useRenderGraph)` calls `_host.SetProductionUseRenderGraph()`.
- `MainViewModel.UseRenderGraph` property reads `_host.ProductionUseRenderGraph`.
- `Raise(nameof(UseRenderGraph))` notifies UI of state change.

## 3. UI Toggle Control (IMPLEMENTED)

- `MainWindow.axaml`: Added `RendererPathSelector` ComboBox (Direct/RenderGraph) in ToolbarEditGroup.
- `MainWindow.axaml.cs`: Added `OnRendererPathChanged` handler calling `Vm.SetRendererPath()`.
- AutomationId: `ViewportRendererPathSelector`.
- Default selection: Direct (SelectedIndex="0").

## 4. Real Editor/UIA Verification (PENDING)

### Blocker
Real Editor window/UIA verification requires:
1. Interactive desktop environment.
2. Launching the Editor with a loaded project containing 3D mesh entities.
3. Toggling the Renderer Path selector and verifying Viewport output.
4. Capturing Direct vs RenderGraph screenshots and comparing pixels.

### Verification Matrix (to be completed in interactive environment)
| Check | Status |
|-------|--------|
| Editor startup with Viewport | PENDING |
| Renderer Path Selector visible | PENDING |
| Default shows Direct | PENDING |
| Toggle to RenderGraph | PENDING |
| RenderGraph Mesh visible in Viewport | PENDING |
| Toggle back to Direct | PENDING |
| Camera operation (both paths) | PENDING |
| Resize (both paths) | PENDING |
| DPI scaling | PENDING |
| Debug/Release UIA | PENDING |

## 5. Builds & Regression

| Target | Result |
|--------|--------|
| EngineCore Debug | 0 errors |
| EditorBridge Debug | 0 errors |
| editor_avalonia Debug | 0 errors |
| editor_avalonia Release | 0 errors |
| test_renderer | 23/23 PASS |
| test_core | 71/71 PASS |
| test_debugdraw | 26/26 PASS |
| test_bridge | 113/113 PASS |
| test_pick | 10/10 PASS |
| **Total** | **243/243 PASS** |

## 6. Conclusion

`P5-F1 — PARTIAL — ABI IMPLEMENTED; REAL UIA PENDING`.

The additive ABI for Renderer path selection is implemented end-to-end (Native C ABI → Managed P/Invoke → EditorHostService → MainViewModel → MainWindow UI toggle). Builds pass with 0 errors. Native regression: 243/243 PASS.

Real Editor window/UIA verification requires an interactive desktop environment and is documented as a follow-up task. The offscreen pixel evidence (255,0,0) from P4-E3-M2 and the production integration (P4-E-B1) establish the rendering correctness; the remaining gap is the interactive UIA evidence.
EOF