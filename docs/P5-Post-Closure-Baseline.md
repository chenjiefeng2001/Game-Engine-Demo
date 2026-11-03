# P5 Post-Closure Baseline

**Status**: FROZEN  
**Branch**: avalonia  
**HEAD**: 1e7fee9  
**Date**: 2026-11-04  
**Precondition**: P5-F1M CLOSED — SCOPE-BOUND

---

## 1. Current Baseline

| Component | Status |
|-----------|--------|
| P4 — Production Rendering Completion | CLOSED |
| P5 — Runtime Evidence & Performance Qualification | CLOSED |
| P5-F1M — Managed Renderer Selection | CLOSED — SCOPE-BOUND |
| P5-F2 — Performance Benchmark | PASS |
| P5-F3 — Jolt Solid Fill Call-Path | VERIFIED (call-path) |
| Remote (origin/avalonia) | Synchronized at 1e7fee9 |
| Ahead/Behind | 0/0 |

## 2. Remaining Exceptions

| ID | Exception | Status |
|----|-----------|--------|
| PV1 | Real Editor/UIA Verification | KNOWN UNVERIFIED |
| PV2 | Jolt Solid Fill GL Readback | KNOWN UNVERIFIED |

## 3. Regression Baseline

| Test Target | Baseline |
|-------------|----------|
| test_renderer | 23/23 |
| test_core | 71/71 |
| test_debugdraw | 26/26 |
| test_bridge | 113/113 |
| test_pick | 10/10 |
| **Total** | **243/243** |

Builds: EngineCore/EditorBridge/editor_avalonia Debug/Release — 0 errors.

## 4. Worktree State

- Dirty third_party submodules: preserved (d3d12ma, openal-soft, vma)
- crashes/ 115 artifacts: preserved
- No unexpected source changes

## 5. P5 Post-Closure Boundaries

- Does NOT reopen P4/P5.
- Does NOT modify historical closure records.
- Does NOT re-run P5-F2 benchmark.
- New results recorded as independent follow-up documents.
EOF