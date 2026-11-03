# P4-B C3 - Implementation Record

Status: IMPLEMENTED - VERIFIED
Date: 2026-11-02
Branch: avalonia
HEAD: b10e0cf84442602932d365c2445ce49bcaf8324a

## Scope
- Long-lived native EditorCamera instance in production 3D path.
- View/Projection/Aspect updates per frame via SetViewportSize + OnUpdate.
- Managed DPI conversion via TopLevel.RenderScaling in MainWindow.
- FBO resize handled by EngineHost ApplyPendingViewportSize (fail-closed).
- Dispose stops pump and disposes EngineHost.

## Verification
- EngineCore Debug/Release: PASS
- EditorBridge Debug/Release: PASS
- test_bridge: 112/112
- test_pick: 9/9 (includes P4B3DProof)
- test_viewport_resize: PASS
- AvaloniaEditor Debug: 0 errors

## Unverified
- 125/150/200% DPI visual proof (requires UI runtime)
- Dock Hide/Show/Float/Redock runtime proof
- Camera controls deferred to C4
