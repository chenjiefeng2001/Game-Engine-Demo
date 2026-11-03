# P4-B C5-1 - DebugDraw Lifecycle Record

Status: IMPLEMENTED - VERIFIED - SCOPE-BOUND
Date: 2026-11-02
Branch: avalonia

## Scope
- Initialized DebugDraw once in InitProduction3DRenderResources (runtime thread, GL current).
- BeginFrame/EndFrame integrated into RenderProduction3DFrame around scene geometry.
- Shutdown in Application::Shutdown before GL context teardown.
- Empty frames handled (EndFrame with zero vertices).
- No ABI changes, no API changes.

## Verification
- EngineCore Debug: PASS
- EditorBridge Debug: PASS
- test_pick: 9/9
- test_bridge: 112/112
- test_debugdraw: 22/22
- AvaloniaEditor Debug: 0 errors

## Unverified
- Five primitive production calls (C5-2)
- Depth/X-Ray pixel validation (C5-3)
- Real-window UIA lifecycle (C6)
