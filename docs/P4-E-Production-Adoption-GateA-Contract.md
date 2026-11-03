# P4-E Gate A — Production Adoption Contract

Status: FROZEN — ARCHITECTURE CONTRACT
Date: 2026-11-02
Source: P4-E proposed adoption plan

## Scope
P4-E evaluates RenderGraph as a production execution infrastructure without migrating the production renderer by default.

## Frozen Boundaries
- Backend: OpenGL 4.6 only.
- Context: existing EngineHost OpenGL context.
- Thread: existing runtime owner thread.
- Execution: serial only.
- First pilot: offscreen production 3D viewport subpath.
- Direct Renderer remains default and fallback.
- No Vulkan or D3D12 adoption.
- No production viewport default change in first pilot.
- No DebugDraw migration.
- No Jolt DebugRenderer contract changes.
- No T-5 API changes.

## Resource Contract
- Imported and transient resources are distinct.
- Resource IDs must be stable.
- Destroyed or invalid handles cannot execute.
- First pilot disables transient aliasing.
- FBO color/depth attachments require explicit mapping.

## Execution Contract
- Compile and Execute are separated.
- Structural or access changes invalidate the compiled plan.
- Execute consumes topological order.
- OpenGL commands remain on the owner thread.
- Compile and Execute failures must be observable.
- GPUProfiler compatibility is preserved.

## Non-Goals
- No full renderer migration.
- No ScenePass slicing.
- No ExecuteParallel production use.
- No multi-backend support.
- No RenderGraph DSL rewrite.
- No shader system rewrite.

## Authorization Boundary
Gate A architecture direction approved.
Gate B source implementation not authorized.
P4-E1 requires separate authorization after Gate A.
