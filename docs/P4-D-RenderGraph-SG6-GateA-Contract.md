# P4-D Gate A — RenderGraph SG6 Recovery Contract

Status: FROZEN — ARCHITECTURE CONTRACT
Date: 2026-11-02
Source: user-provided Gate A decision (D1–D8, performance gates, D0–D6 slices)

## Scope
P4-D is defined as RenderGraph SG6 Correctness & Execution Recovery.
It is not a full RenderGraph rewrite and does not migrate the production renderer.

## Frozen Decisions
- D1 SG6 recovery covers Q1–Q5 core correctness and performance.
- D2 Preserve the main API; add explicit resource access semantics (Read, Write, ReadWrite).
- D3 Dependencies must form a real DAG with RAW / WAR / WAW handling.
- D4 Execute must consume topologicalOrder; stable tie-break by declaration index.
- D5 Barriers must use real RHI resource handles and legal state transitions.
- D6 ExecuteParallel must respect the DAG and OpenGL single-context constraints.
- D7 GPUProfiler T-4 contract remains unchanged.
- D8 Production Renderer remains isolated during recovery.

## Non-Goals
- No new RenderGraph DSL.
- No full shader pipeline refactor.
- No ScenePass production integration.
- No multi-backend migration.
- No production renderer migration.
- No DebugDraw full integration.
- No changes to T-5 API or P4-B/P4-C delivered behavior.

## Performance Contract
- Compile and Execute are separated.
- Execute does not compile.
- Graph mutation invalidates the compiled plan.
- P50/P95 and allocation counts are recorded in B0 baseline before implementation.
- High-frequency per-pass logging is prohibited in Execute.

## Implementation Slices
- D1 Dependency model and topological execution.
- D2 Resource registry and barrier recovery.
- D3 Build once / execute many.
- D4 ExecuteParallel DAG recovery.
- D5 SG6 full verification.
- D6 Closure and documentation.

## Closure Target
P4-D — PASS — SG6 RECOVERED — SCOPE-BOUND

Note: SG6 recovery closure does not imply production renderer migration.

## Authorization Boundary
Gate A architecture direction approved.
Gate B source implementation not yet authorized.
