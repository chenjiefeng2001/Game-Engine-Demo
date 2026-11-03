# P4-E Post-Closure Performance Record

**Status**: PARTIAL — REGRESSION PASS; BENCHMARK HARNESS PENDING  
**Branch**: avalonia  
**HEAD**: cf42133  
**Date**: 2026-11-03  
**Precondition**: F0 PASS

---

## 1. Regression Baseline (VERIFIED)

| Test Target | Result |
|-------------|--------|
| test_renderer | 22/22 PASS (Debug/Release) |
| test_core | 71/71 PASS (Debug) |
| test_debugdraw | 24/24 PASS (Debug) |
| test_bridge | 113/113 PASS (Debug) |
| test_pick | 10/10 PASS (Debug) |
| **Total** | **240/240 PASS** |

Builds (0 errors): EngineCore Debug/Release, EditorBridge Debug, editor_avalonia Debug/Release.

No test-count change vs pre-P4 baseline (240 → 240). No regression from P4-E-B1 RenderGraph integration.

---

## 2. Performance Benchmark (PENDING)

### Benchmark Harness Requirements

A dedicated benchmark harness is required to measure Direct Renderer vs RenderGraph Production Renderer:

| Metric | Direct Renderer | RenderGraph Production | Delta |
|--------|----------------|------------------------|-------|
| CPU Frame Time (ms) | — | — | PENDING |
| Graph Compile Time (ms) | N/A | — | PENDING |
| Graph Execute Time (ms) | N/A | — | PENDING |
| GPU Timestamp (ms) | — | — | PENDING |
| Pass Count | 1 | 1 | 0 |
| Resource Count | N/A | 1 | +1 |
| Heap Allocations/frame | — | — | PENDING |
| P50 (ms) | — | — | PENDING |
| P95 (ms) | — | — | PENDING |
| P99 (ms) | — | — | PENDING |

### Sampling Rules (to be frozen before execution)
- Warm-up: N frames discarded
- Minimum 1000 effective samples per path
- 3 independent runs
- Release build as primary baseline
- Debug build for diagnostic overhead
- GPU timestamp via existing T-4 GPUProfiler (not CPU submit time)

### Acceptance Thresholds (to be frozen before execution)
- CPU Overhead tolerance: TBD
- GPU Overhead tolerance: TBD
- P95 difference standard: TBD
- Memory allocation standard: TBD
- Compile frequency standard: TBD

### Blocker
No benchmark harness exists yet. The harness requires:
1. A test scene with known Static Mesh content.
2. Both Direct and RenderGraph paths enabled (feature flag toggle).
3. Frame-time instrumentation around `RenderProduction3DFrame`.
4. Statistical aggregation (P50/P95/P99).

---

## 3. Conclusion

`F2 — PARTIAL — REGRESSION PASS; BENCHMARK HARNESS PENDING`.

Regression baseline is stable (240/240, 0 build errors). Performance measurement
requires a benchmark harness that does not yet exist. This is an independent
follow-up task that does not affect P4-E Gate B closure status.

Performance shortfalls, if found, must be recorded as Blocker or Accepted
Exception — not ignored.
EOF