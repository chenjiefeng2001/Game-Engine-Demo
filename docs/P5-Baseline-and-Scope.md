# P5 Baseline and Scope

**Status**: FROZEN  
**Branch**: avalonia  
**HEAD**: ef9e227  
**Date**: 2026-11-03  
**Precondition**: P4 — CLOSED; P4 Post-Completion — REVIEW COMPLETE

---

## 1. P4 Baseline (Immutable)

| Workstream | Status |
|------------|--------|
| T-6 Console | CLOSED |
| P4-B Production 3D Viewport | CLOSED — SCOPE-BOUND |
| P4-C Original Scope | CLOSED |
| P4-D RenderGraph SG6 | CLOSED — ACCEPTED EXCEPTIONS |
| P4-E Gate B | PASS — PRODUCTION PILOT VERIFIED |
| P4-CX | CLOSED — SCOPE-BOUND |
| P4 Overall | CLOSED |
| P4 Post-Completion | REVIEW COMPLETE |
| Remote Publication | SYNCHRONIZED at 79efaa2 (pre-P5) |

## 2. Three Remaining Exceptions (P5 Scope)

| ID | Exception | Status |
|----|-----------|--------|
| F1-M | Managed Feature Flag + Real Editor/UIA | PARTIAL |
| F2-B | Performance Benchmark Harness | PENDING |
| F3-G | Solid Fill GL Readback Verification | PARTIAL (call-path verified) |

## 3. Regression Baseline

| Test Target | Baseline |
|-------------|----------|
| test_renderer | 22/22 |
| test_core | 71/71 |
| test_debugdraw | 26/26 (was 24, +2 Jolt tests) |
| test_bridge | 113/113 |
| test_pick | 10/10 |
| **Total** | **242/242** |

Builds: EngineCore Debug/Release, EditorBridge Debug, editor_avalonia Debug/Release — 0 errors.

## 4. Git State

- Branch: avalonia
- Local/Remote: synchronized (push after each commit)
- Dirty third_party submodules: preserved
- crashes/ 115 artifacts: preserved

## 5. P5 Work Boundaries

- Does NOT reopen P4-B, P4-C, P4-D, P4-E, or P4-CX.
- Does NOT modify P4 historical closure records.
- Does NOT enable Transient Aliasing, ExecuteParallel, Vulkan/D3D12.
- Does NOT modify T-5 DebugDraw API (unless separately authorized).
- New results recorded as independent P5 documents.
EOF