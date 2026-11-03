# P4-E-B4 — Regression & Performance

**Status**: REGRESSION PASS — PERFORMANCE PLANNED  
**Branch**: avalonia  
**HEAD**: 641ce27  
**Date**: 2026-11-03  
**Precondition**: P4-E-B3 PARTIAL

---

## 1. Regression Baseline

| Test Target | Reference | Result (this cycle) |
|-------------|-----------|---------------------|
| test_renderer | 22/22 | 22/22 PASS (Debug/Release) |
| test_core | 71/71 | 71/71 PASS (Debug) |
| test_debugdraw | 24/24 | 24/24 PASS (Debug) |
| test_bridge | 113/113 | 113/113 PASS (Debug) |
| test_pick | 10/10 | 10/10 PASS (Debug) |
| **Total** | — | **240/240 PASS** |

Builds (0 errors):
- EngineCore Debug / Release
- EditorBridge Debug
- editor_avalonia Debug / Release

Test-count change vs prior baseline: **none** (240 → 240). No regression introduced by P4-E-B1 RenderGraph integration.

---

## 2. Performance Verification Plan

Compare Direct Renderer vs RenderGraph Production Renderer:

| Metric | Direct Renderer | RenderGraph Production | Delta |
|--------|----------------|------------------------|-------|
| CPU Frame Time (ms) | — | — | PENDING |
| RenderGraph Execute CPU Time (ms/frame) | N/A | — | PENDING |
| GPU Timestamp (if available) | — | — | PENDING |
| P50 / P95 frame time | — | — | PENDING |
| Per-frame Heap Allocation | — | — | PENDING |
| Pass count per frame | 1 (Direct) | 1 (RenderGraph DrawMesh) | 0 |
| Resource count per frame | N/A | 1 imported texture | +1 |

**Pass/fail rule**: Any performance regression beyond an acceptable threshold (to be determined from real measurements) must be recorded as a Blocker or Accepted Exception — not ignored.

**Status**: Not yet measured. Requires a benchmark harness rendering N frames on both paths. Deferred to a dedicated performance gate.

---

## 3. Performance-Accepted Exceptions (none yet)

No performance data has been collected. If RenderGraph introduces per-frame overhead, it must be documented here before Gate B Closure.

---

## 4. Conclusion

`P4-E-B4 — REGRESSION PASS`. Regression baseline is unchanged (240/240, 0 build errors). Performance measurement is planned but not yet executed; must be completed before P4-E-B5 Gate B Closure or recorded as an Accepted Exception.
