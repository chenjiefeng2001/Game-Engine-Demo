# P5-F3-U1 — Source Alignment Follow-up

**Status**: PARTIAL — SOURCE ALIGNMENT RESTORED; ADAPTER SEH PENDING  
**Branch**: avalonia  
**HEAD**: cd33c77  
**Date**: 2026-11-05
**Precondition**: Gate 0 partial (Git baseline frozen at 711381e; remote 0/0)

---

## 1. Purpose

Recover the test/source/build/recording chain that exposed unresolved uncommitted diagnostic drafts after Gate 0:

```
Committed Test Sources -> CMake Registration -> Debug/Release Build -> Test Enumeration -> Recorded Evidence
```

---

## 2. Changes

| Item | Change |
|------|--------|
| `OpenGLPhysicsDebugDrawSolidFillReadback` | Kept as draft under `#if 0`, marked `PENDING — NOT QUALIFIED`; excluded from regression count |
| `OpenGLPhysicsDebugDrawWireframeNoFill` | Same treatment; negative control not claimed PASS |
| `P5F3_Sanity` | Removed from test registration; no diagnostic draft counted |
| Second duplicate `JoltDualDrawBodiesInvokesSolidFill` | Removed; canonical retained: `DebugDrawAdapterTest.JoltDualDrawBodiesInvokesSolidFill` |
| `PV2_FixtureClearReadbackDiagnostic` | Removed from test registration; fixture-only diagnostics retained as docs/P5-PV2 record only |
| `tests/CMakeLists.txt` | Deleted duplicate `engine/src` include and duplicate `JPH_DEBUG_RENDERER` definition |
| `tests/test_debugdraw/test_debugdraw.cpp` | Removed duplicate `#include "Engine/Core/Physics/IPhysicsDebugDraw3D.h"` |

Draft/unqualified sources remain in the working tree; they are not part of the formal test binary or expected pass count.

---

## 3. Restored Test Chain

| Target | Debug | Release |
|--------|-------|---------|
| `test_debugdraw` | 25/25 PASS | 25/25 PASS |
| `test_renderer` | 23/23 PASS | 23/23 PASS |
| `test_core` | 71/71 PASS | 71/71 PASS |
| `test_bridge` | 113/113 PASS | 113/113 PASS |
| `test_pick` | 10/10 PASS | 10/10 PASS |
| **Total** | **245/245** | **245/245** |

Build: EngineCore/EditorBridge/editor_avalonia Debug/Release — not re-verified in this slice.

---

## 4. Accepted Remaining Exceptions

| Exception | Status |
|-----------|--------|
| Jolt Solid Fill GL Pixel Readback | **PARTIAL — CALL-PATH VERIFIED; GL READBACK PENDING** |
| Adapter SEH (0xC0000005) at C0 | **PENDING — SEH INVESTIGATION** |
| Gate A | PENDING — ADAPTER SEH BLOCKER |
| Gate B | PARTIAL — Jolt DrawTriangle call path verified; pixel readback not completed |

---

## 5. Next Steps

1. Verify `test_debugdraw`/`test_renderer` Debug/Release builds are clean.
2. Capture C0 SEH call stack from Release/Debug diagnostic run.
3. Isolate Adapter/C0 fault; apply minimal fix only if fault is in production Adapter path.
4. Re-run Solid Fill pixel proof; do not tag Gate A/B PASS on callback evidence alone.
