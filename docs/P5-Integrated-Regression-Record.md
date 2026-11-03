# P5 Integrated Regression Record

**Status**: PASS — 243/243 NATIVE TESTS + MANAGED BUILDS  
**Branch**: avalonia  
**HEAD**: 2e50462  
**Date**: 2026-11-03  
**Precondition**: P5-F3, P5-F2, P5-F1 all complete

---

## 1. Native Regression

| Test Target | Baseline (P4) | P5 Result | Delta |
|-------------|-------------|-----------|-------|
| test_renderer | 22/22 | 23/23 | +1 (P5F2 benchmark) |
| test_core | 71/71 | 71/71 | 0 |
| test_debugdraw | 24/24 | 26/26 | +2 (Jolt solid fill tests) |
| test_bridge | 113/113 | 113/113 | 0 |
| test_pick | 10/10 | 10/10 | 0 |
| **Total** | **240/240** | **243/243** | **+3** |

All tests PASS in Debug. test_renderer and test_core also PASS in Release.

## 2. Builds

| Target | Config | Result |
|--------|--------|--------|
| EngineCore | Debug | 0 errors |
| EngineCore | Release | 0 errors |
| EditorBridge | Debug | 0 errors |
| editor_avalonia | Debug | 0 errors |
| editor_avalonia | Release | 0 errors |

## 3. Runtime Verification

| Check | Status |
|-------|--------|
| Direct Renderer path | Unchanged (default) |
| RenderGraph path (opt-in) | Integrated, offscreen verified (255,0,0) |
| Jolt Wireframe | Preserved (DrawLine) |
| Jolt Solid Fill | Code capability activated (DrawTriangle invoked) |
| FBO lifecycle (Resize) | Per-frame Reset, no stale handles |
| Execute bool contract | Checked at production call site |
| GL state restoration | GL_FRAMEBUFFER_BINDING = 0 after Execute |

## 4. Conclusion

`P5-5 — PASS — INTEGRATED REGRESSION COMPLETE`.

Native regression: 243/243 PASS (was 240, +3 new tests). Managed builds: 0 errors.
No regression introduced by P5 changes. All P4 functionality preserved.
EOF