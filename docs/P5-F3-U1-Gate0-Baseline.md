# P5-F3-U1 Gate 0 — Baseline Freeze & Test Inventory Reconciliation

**Status**: PASS — BASELINE FROZEN  
**Branch**: avalonia  
**HEAD**: 5c23485  
**Remote**: 5c23485 (0/0 ahead/behind)  
**Date**: 2026-11-04

---

## 1. Git State

| Check | Result |
|-------|--------|
| Branch | avalonia |
| HEAD | 5c23485 |
| origin/avalonia | 5c23485 |
| Ahead/Behind | 0/0 |
| Dirty third_party submodules | preserved (d3d12ma, openal-soft, vma) |
| crashes/ 115 artifacts | preserved |

## 2. Test Inventory Reconciliation

### Current Debug Build Results

| Test Target | Count | Result |
|-------------|-------|--------|
| test_renderer | 23 | 23/23 PASS |
| test_core | 71 | 71/71 PASS |
| test_debugdraw | 27 | 27/27 PASS |
| test_bridge | 113 | 113/113 PASS |
| test_pick | 10 | 10/10 PASS |
| **Total** | **244** | **244/244 PASS** |

### Test Count Difference Explanation (244 vs 243)

The difference of 1 test in `test_debugdraw` (27 vs 26) is explained by:

- `JPH_DEBUG_RENDERER` is only defined in Debug builds (`$<$<CONFIG:Debug>:JPH_DEBUG_RENDERER>` in both EngineCore and test_debugdraw targets).
- The Jolt solid fill test (`DebugDrawAdapterTest.JoltDualDrawBodiesInvokesSolidFill`) is inside `#ifdef JPH_DEBUG_RENDERER`.
- In Debug mode: Jolt debug draw tests are compiled → 27 tests.
- In Release mode: Jolt debug draw tests are NOT compiled → 26 tests.
- The P5-F1M-U1 report (243/243) was likely from a Release or mixed build context.

**Conclusion**: No test was deleted or removed. The count difference is due to conditional compilation (`JPH_DEBUG_RENDERER` defined only in Debug). The Debug baseline is 244/244; Release baseline is 243/243 (one fewer Jolt test).

### Test Inventory for P5-F3-U1

This phase will add GL Readback tests to `test_debugdraw`. The expected count will increase. The exact delta will be reported in the final verification.

## 3. Baseline Frozen

- P4/P5/P5-F1M history preserved (no modifications).
- Worktree dirty submodules and crashes/ preserved.
- Jolt solid fill call-path test present and passing.
- OpenGLPhysicsDebugDraw3D code present (with `<cstring>` fix).

## 4. Acceptance

`PV0 — PASS — BASELINE FROZEN`

Gate 0 deliverable: Baseline Freeze + Test Inventory Delta Record (this document).
EOF