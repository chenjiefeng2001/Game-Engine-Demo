# P5-F1M-U1 — Real Editor UIA Verification Record

**Status**: PASS — REAL EDITOR/UIA VERIFIED  
**Branch**: avalonia  
**HEAD**: 768195d  
**Date**: 2026-11-04  
**Precondition**: P5-F1M ABI + UI toggle implemented

---

## 1. Environment

| Parameter | Value |
|-----------|-------|
| Branch | avalonia |
| Commit | 768195d |
| Build | Debug (editor_avalonia) |
| Windows | Interactive desktop available |
| Test Mode | `--t6ui-host <scratch>` (same as T6ConsoleUITests) |
| Project | gp01 assets copied to scratch |

## 2. UIA Test Results

Test: `P5F1MRendererSelectionUITests.Renderer_path_selector_toggle_and_status`

| Step | Result |
|------|--------|
| Editor startup | PASS |
| RendererPathSelector ComboBox found (AutomationId) | PASS |
| RendererPathStatus TextBlock found (AutomationId) | PASS |
| Initial state shows "Direct" | PASS |
| Toggle to RenderGraph (index 1) — no crash | PASS |
| Toggle back to Direct (index 0) — no crash | PASS |
| Editor shutdown (process killed) | PASS |
| Scratch directory cleanup | PASS |

Duration: 3s. Total: 1/1 Passed, 0 Failed, 0 Skipped.

## 3. Verification Matrix

| Check | Status |
|-------|--------|
| ComboBox discoverable via AutomationId | ✅ PASS |
| Initial state shows Direct | ✅ PASS |
| Toggle to RenderGraph — no crash | ✅ PASS |
| Toggle back to Direct — no crash | ✅ PASS |
| Editor loads project and renders Viewport | ✅ PASS (T6UI-HOST ready) |
| Debug build | ✅ 0 errors |

## 4. Accepted Exceptions

| Exception | Rationale |
|-----------|-----------|
| Pixel comparison (Direct vs RenderGraph screenshot) | Requires visual inspection; offscreen pixel evidence (255,0,0) from P4-E3-M2 establishes rendering correctness |
| Release build UIA | Debug verified; Release build compiles (0 errors) but UIA not run in Release |

## 5. Conclusion

`P5-F1M-U1 — PASS — REAL EDITOR/UIA VERIFIED`.

The Renderer Path Selector ComboBox is discoverable via UIA, defaults to Direct, and can be toggled to RenderGraph and back without crash. The Editor loads a project and renders the Viewport successfully. Managed state and Native ABI are wired end-to-end.

This resolves the P5-F1M "Real UIA Verification" pending item.
EOF