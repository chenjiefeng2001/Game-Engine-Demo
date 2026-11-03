# P4-D Gate 0 - RenderGraph SG6 Recovery Scan

Status: PASS - READ-ONLY AUDIT
Date: 2026-11-02
Mode: READ-ONLY (no source changes, no build/test execution beyond existing evidence)

## 1. SG6 Historical Root Causes
Source: docs/GPU-Physics-v3.0-Simulation-Domain-Architecture.md SG6 section.

Documented failures:
1. Q1 API coupling: AddPass/setup/execute style is verbose and raw GL kernels are bound manually.
2. Q2 ordering correctness: Execute does not use topologicalOrder; DeriveDependencies only scans resource names and cannot detect read-after-write hazards reliably.
3. Q3 resource lifetime/transient allocation: firstUse/lastUse exist, but generated barriers use reinterpret_cast placeholders instead of real resource handles.
4. Q4 ExecuteParallel is layer-based and not dependency-driven; ScenePass slicing is not production-integrated.
5. Q5 performance: 47.5x overhead for 500 iterations due to per-frame Compile and lambda/map overhead.

## 2. Current Code State
- RenderGraph is implemented in engine/include/Engine/Rendering/RenderGraph.h and engine/src/Rendering/RenderGraph.cpp.
- Compile derives dependencies, topologically sorts, computes lifetimes, allocates transient requests, and generates barriers.
- Execute walks pass indices in declaration order, not topological order.
- ExecuteParallel uses layer grouping and does not consume topological ordering.
- Barrier generation stores IRHIBuffer*/IRHITexture* placeholders by casting resource name values.
- SG6 tests exist in tests/test_renderer/RenderGraphSG6Test.cpp and currently pass in isolation (3/3).

## 3. Production Renderer Relationship
- P4-B production 3D rendering uses OpenGL direct path (MeshRenderer into presentation FBO), not RenderGraph.
- DebugDraw uses OpenGL46DebugDrawRenderer directly and is not a RenderGraph pass.
- Therefore SG6 recovery can be staged without changing the stable OpenGL production path.

## 4. GPUProfiler / DebugDraw / Multi-Pass Impact
- RenderGraph has optional GPU timestamp profiler hooks.
- DebugDraw is currently outside RenderGraph and must remain functional if RenderGraph is reintroduced.
- Multi-pass requirements should be validated through isolated RenderGraph tests before production integration.

## 5. Minimum Recovery Direction (Proposed, Not Implemented)
1. Make Execute consume topologicalOrder.
2. Replace barrier placeholder handles with real resource references or a validated resource registry.
3. Support build-once / execute-many to remove per-frame Compile overhead.
4. Reduce logging and per-frame allocations in Execute.
5. Keep recovery isolated until parity tests pass.

## 6. Acceptance Matrix (Proposed)
- SG6 ordering test: PASS
- SG6 dependency/resource test: PASS
- Barrier transition test: real handles verified
- Performance test: overhead below agreed threshold
- Existing test_renderer: no regression
- Production OpenGL path: unchanged

## 7. Boundary
- No source modifications in Gate 0.
- No changes to T-5 API.
- No changes to P4-B/P4-C delivered behavior.
- RenderGraph SG6 remains isolated until recovery is explicitly authorized.
