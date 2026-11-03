# P4-B C5-0 - DebugDraw Production Integration Gate 0

Status: PASS - INTEGRATION READY
Date: 2026-11-02
Branch: avalonia
Mode: READ-ONLY AUDIT (no source changes)

## 1. T-5 API and Owner Thread
- DebugDraw lives in Engine::Rendering::DebugDraw.
- API: Initialize(IRenderer), BeginFrame(View, dt), Line/WireBox/WireSphere/Axis/Frustum, EndFrame, Shutdown.
- OpenGL46DebugDrawRenderer requires GladGLContext and owner thread = current GL thread.
- All GL calls occur inside Render (owner thread). No UI-thread GL.

## 2. Production Render Path
- EngineHost binds Presentation FBO, clears color+depth, then calls RenderProductionFrame then RenderProduction3DFrame.
- RenderProduction3DFrame enables GL_DEPTH_TEST/GL_LESS and renders MeshRenderer into the same FBO.
- FBO retains GL_DEPTH24_STENCIL8 depth attachment and is not re-cleared by 3D path.

## 3. Camera and Matrices
- Production 3D camera is Engine::EditorCamera (long-lived).
- View/Projection can be supplied to DebugDraw::View via Mat4 viewProjection = projection * view.
- EditorCamera exposes GetViewMatrixPtr/GetProjectionMatrixPtr/GetPosition.

## 4. DebugDraw Insertion Point
- Initialize renderer in Application::InitProduction3DRenderResources (runtime thread, context current).
- BeginFrame after camera matrices are finalized, before scene geometry or after? Plan: collect primitives before EndFrame; EndFrame after MeshRenderer::Render.
- EndFrame submits depth-tested and X-Ray batches; renderer restores GL state.
- Shutdown in Application teardown before GL context destruction.

## 5. Depth and X-Ray
- Renderer Render() snapshots/restores GL state.
- Depth batch: GL_DEPTH_TEST enabled, depthMask false, depthFunc LEQUAL.
- X-Ray batch: depth test disabled, depthMask false.
- Does not clear color or depth; restores depthMask/depthFunc from snapshot.

## 6. GL State Isolation
- GLStateSnapshot captures blend/depth/program/VAO/VBO/lineWidth and restores them.
- Safe to run after MeshRenderer::Render.

## 7. Risks / Open Items
- Grid/Axis plane (XY vs XZ) depends on engine up-axis; must be verified with T-5 reference, not assumed.
- Empty-frame handling: BeginFrame/EndFrame must be skipped or EndFrame with zero vertices returns Ok.
- Suspend/resume must not leave stale primitives; DebugDraw persistent commands age by dt; zero-dt frames may retain.
- No ABI changes required for C5.

## 8. Exit Criteria
PASS - INTEGRATION READY
