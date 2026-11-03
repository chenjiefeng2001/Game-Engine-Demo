# P4-E Gate 0 - Production Adoption Read-Only Scan

Status: PASS - READ-ONLY COMPLETE
Date: 2026-11-02
Mode: READ-ONLY (no source changes)

## 1. Production Render Path
- P4-B production 3D rendering uses Engine::Application::RenderProduction3DFrame.
- It renders MeshRenderer directly into the EngineHost presentation FBO.
- DebugDraw uses OpenGL46DebugDrawRenderer directly and is not a RenderGraph pass.
- RenderGraph is not used by the production 3D viewport path.

## 2. OpenGL Context Ownership
- EngineHost owns the production GL context.
- Runtime rendering occurs on the existing owner thread.
- No multi-context or multi-thread GL execution is available for production adoption.
- RenderGraph Execute must remain serial in this environment.

## 3. Resource Boundaries
- Production resources are owned by MeshRenderer / RHI device paths.
- RenderGraph currently tracks logical resources and transient allocations.
- Imported resource handles must be supplied explicitly; RenderGraph does not own production resources.
- Resize, recreate, and destruction boundaries must be defined before adoption.

## 4. Renderer Relationships
- MeshRenderer is the stable production renderer.
- DebugDraw is independent and must remain functional.
- GPUProfiler hooks are available but RenderGraph production integration is unproven.
- Jolt DebugRenderer remains independent.

## 5. Key Risks
- WAR dependency remains unverified.
- Transient alias safety remains unproven.
- ExecuteParallel must not be enabled for OpenGL production.
- Real pixel equivalence must be established before viewport opt-in.
- Resource lifetime and FBO attachment mapping need explicit contracts.

## 6. Recommended Pilot
- Offscreen production 3D viewport subpath only.
- Static mesh rendering with existing camera and shader.
- Direct Renderer retained as fallback.
- No production viewport default change in first pilot.

## 7. Boundary
- No source changes in Gate 0.
- No production renderer migration authorized.
- P4-D accepted exceptions remain in force.
