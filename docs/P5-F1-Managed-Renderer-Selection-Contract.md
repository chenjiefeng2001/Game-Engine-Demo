# P5-F1 — Managed Renderer Selection Contract & Verification

**Status**: PARTIAL — ABI IMPLEMENTED; REAL UIA PENDING  
**Branch**: avalonia  
**HEAD**: 3cf4766  
**Date**: 2026-11-03  
**Precondition**: P5-F2 PASS

---

## 1. ABI Contract (IMPLEMENTED)

### Additive ABI Changes

| Component | Change | ABI Impact |
|-----------|--------|------------|
| `bridge/include/editor_bridge/capi.h` | Added `EngineHost_SetProductionUseRenderGraph` / `EngineHost_GetProductionUseRenderGraph` declarations | Additive (new functions only) |
| `bridge/src/EngineHost.h` | Added `SetProductionUseRenderGraph(bool)` / `ProductionUseRenderGraph()` forwarding to Application | Internal |
| `bridge/src/capi.cpp` | C ABI implementations (thin forwarders) | Additive |
| `editor_avalonia/Native/EditorBridgeApi.cs` | P/Invoke declarations | Managed mirror |
| `editor_avalonia/Services/EditorHostService.cs` | `EngineHostChannel` methods + `EditorHostService` wrapper | Managed API |

### ABI Compatibility
- No existing struct layout modified.
- No enum numeric values changed.
- No existing function contract changed.
- `RuntimeResult` (76B), `EV_COUNT` (14), `EngineLogRecordV1` (2392B) unchanged.
- Default remains Direct Renderer (flag defaults to false).

### ABI Verification
- Native build: EngineCore Debug, EditorBridge Debug — 0 errors.
- Managed build: editor_avalonia Debug/Release — 0 errors.
- Existing bridge tests: 113/113 PASS (no regression from additive exports).

## 2. Real Editor/UIA Verification (PENDING)

### Blocker
The managed-side feature flag API (`EditorHostService.SetProductionUseRenderGraph`) is now available, but:
1. No UI control (checkbox/toggle) exists in MainWindow.axaml to let the user switch paths at runtime.
2. No UIA test exercises the Production Viewport RenderGraph path.
3. Real Editor window pixel comparison (Direct vs Graph) requires interactive desktop.

### Required Follow-up
1. Add a UI control (e.g., checkbox or menu item) in MainWindow.axaml bound to `SetProductionUseRenderGraph`.
2. Create a UIA test that launches the Editor, toggles the flag, and verifies Viewport output.
3. Capture Direct vs Graph screenshots and compare pixels.

These are independent follow-up tasks that do not affect the ABI implementation.

## 3. Conclusion

`P5-F1 — PARTIAL — ABI IMPLEMENTED; REAL UIA PENDING`.

The additive ABI for Renderer path selection is implemented and builds cleanly (0 errors, 0 regression). Real Editor window/UIA verification requires a UI toggle control and interactive desktop testing.

Accepted exception: Real UIA verification pending UI control implementation.
EOF