# P4-D RenderGraph SG6 Implementation Record

Status: IMPLEMENTED — AWAITING FINAL VERIFICATION
Date: 2026-11-02

## Changes
- RenderGraph dependency derivation now collects readers/writers globally and adds RAW dependencies for every reader-to-writer edge.
- WAW dependencies are added only for declaration-ordered writer pairs where neither side also reads the resource.
- Topological sort uses a priority queue for stable declaration-index tie-breaking.
- Execute now consumes m_ExecutionOrder instead of declaration order.
- ExecuteParallel non-scene path processes passes in execution order.
- AddPass/AddScenePass invalidate the compiled plan.
- Reset clears execution order.
- Barrier generation uses real RHI handles from transient allocations and imported resources.
- High-frequency Execute/ExecuteParallel info logs removed.
- Added resource handle fields, GetExecutionOrder, and GetCompileCount.

## Boundaries Preserved
- No T-5 API changes.
- No P4-B/P4-C behavior changes.
- Production OpenGL renderer remains independent.
- No Bridge ABI changes.
