# P5-F1M — Closure Record

**Status**: CLOSED — SCOPE-BOUND — ABI IMPLEMENTED, REAL UIA PENDING  
**Branch**: avalonia  
**HEAD**: 700dd34  
**Date**: 2026-11-03  
**Precondition**: P5-F2 PASS

---

## 1. Summary

P5-F1M delivered the Managed Renderer Selection feature end-to-end:

1. **Native Additive ABI**: `EngineHost_SetProductionUseRenderGraph` / `EngineHost_GetProductionUseRenderGraph` (C ABI) forwarding to `Application::SetProductionUseRenderGraph` / `ProductionUseRenderGraph`. No existing struct/enum/function contract modified.

2. **Managed Binding**: `EditorBridgeApi` P/Invoke + `EngineHostChannel` methods + `EditorHostService` wrapper + `MainViewModel.SetRendererPath` / `UseRenderGraph`.

3. **UI Toggle**: `ComboBox` in `ToolbarEditGroup` with Direct/RenderGraph options, `AutomationId="ViewportRendererPathSelector"`, status text display.

4. **Default**: Direct Renderer (flag defaults to false, unchanged behavior).

## 2. Builds & Regression

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

## 3. Real UIA Verification (PENDING)

Requires interactive desktop + Editor launch with loaded project. The offscreen pixel evidence (255,0,0) from P4-E3-M2 and production integration (P4-E-B1) establish rendering correctness. The remaining gap is the interactive UIA evidence for the toggle control.

## 4. Accepted Exceptions

| Exception | Rationale |
|-----------|-----------|
| Real Editor window/UIA verification | Requires interactive desktop; offscreen pixel evidence exists |
| Managed-side UI toggle not exercised by automated test | No existing UIA test for Production Viewport; new test needed |

## 5. Conclusion

`P5-F1M — CLOSED — SCOPE-BOUND — ABI IMPLEMENTED, REAL UIA PENDING`.

The additive ABI and UI toggle are implemented and build cleanly. Real Editor window/UIA verification is an independent follow-up task requiring interactive desktop.

P4/P5 original closure records remain immutable.
EOF