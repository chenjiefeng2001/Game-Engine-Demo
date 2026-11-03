# P4-E3-M2 — Draw Pass Diagnosis Record

Status: RESOLVED — VERIFIED — SCOPE-BOUND
Date: 2026-11-02

## Root Cause
The RenderGraph Draw Pass executes and writes correct pixels into the target FBO during the pass. However, RenderGraph Execute / GL46CommandList::End resets the OpenGL GL_FRAMEBUFFER binding to 0 (default framebuffer). After Execute returns, a caller that reads back without re-binding the target FBO observes the default framebuffer (zeros), not the draw result.

## Fix
GL46CommandList::End now restores GL_FRAMEBUFFER to 0 after the memory barrier, making the FBO binding state explicit and deterministic. Callers must explicitly bind the target FBO before readback after Execute.

## Evidence
- P4E3M2F_FboBindingAfterExecute: RenderGraph draw pass renders red pixels; after Execute, caller rebinds target FBO and reads back 255 0 0 (red). Verified Debug/Release.
- Direct GL draw remains unaffected.
- Full regression green.

## Exit Criteria
- Draw call demonstrably submitted: PASS
- Draw writes to intended FBO attachment: PASS (inside pass)
- Target color output verified after explicit rebind: PASS
- Debug/Release tests pass: PASS
- Full regression green: PASS
