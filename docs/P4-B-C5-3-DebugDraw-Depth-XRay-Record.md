# P4-B C5-3 - DebugDraw Depth / X-Ray Record

Status: IMPLEMENTED - VERIFIED - SCOPE-BOUND
Date: 2026-11-02
Branch: avalonia

## Scope
- Production DebugDraw uses shared Presentation FBO depth buffer.
- DebugDraw renderer snapshots/restores GL state and does not clear depth.
- Depth-tested and X-Ray batches submitted through production path.
- Added production test P4BC53DepthXRay (X-Ray primitive visible).

## Verification
- EngineCore Debug: PASS
- EditorBridge Debug: PASS
- test_pick: 10/10
- test_debugdraw: 22/22
- AvaloniaEditor Debug: 0 errors

## Unverified
- Depth occlusion pixel assertion for specific primitives (covered by T-5 unit tests)
- Real-window UIA (C6)
