# P4-E3-M2-F — FBO Binding State Contract Fix

Status: IMPLEMENTED — VERIFIED — SCOPE-BOUND
Date: 2026-11-02

## Change
GL46CommandList::End now restores the default framebuffer binding (GL_FRAMEBUFFER, 0) after the memory barrier.

## Reason
RenderGraph Execute draws into the caller-bound target FBO inside the pass, but did not restore the previous GL framebuffer binding on End. Callers that expected the previous FBO to remain bound after Execute returned would read back from the wrong (default) framebuffer and observe zero pixels.

## Verification
- P4E3M2F_FboBindingAfterExecute: RenderGraph draw pass renders red pixels into the target FBO; after Execute, the caller explicitly rebinds the target FBO and reads back 255 0 0 (red). Verified Debug/Release.
- test_renderer Debug/Release: 21/21 pass.
- Full regression (pick/bridge/core/debugdraw/physics/viewport_resize): Debug/Release pass.

## Known Limits
- Production MeshRenderer integration still pending.
- ExecuteParallel remains disabled.
- Readback after Execute requires the caller to explicitly rebind the target FBO.
