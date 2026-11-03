# P4-E3-M2 Offscreen Static Mesh Draw Pilot Record

Status: VERIFIED — SCOPE-BOUND
Date: 2026-11-03

## Root Cause

RenderGraph::Execute never bound a framebuffer before executing passes. Draw passes that relied on the caller-bound FBO drew to the default framebuffer (binding 0), so readback after Execute observed zeros — "RenderGraph draw pass executed but did not affect the FBO."

## Fix

1. `engine/include/Engine/Rendering/RenderGraph.h` — Added `uintptr_t renderTargetFbo = 0` field to `RGResource`; added `ImportTexture(StringID, IRHITexture*, uintptr_t renderTargetFbo)` overload to `RenderPassBuilder`.

2. `engine/include/Engine/Core/RHI/IRHICommandList.h` — Added default no-op `BindNativeFramebuffer(uintptr_t nativeHandle)` virtual method.

3. `engine/include/Engine/Core/RHI/GL46AZDODevice.h` — Declared `BindNativeFramebuffer(uintptr_t)` override on `GL46CommandList`.

4. `engine/src/OpenGL/GL46CommandList.cpp` — Implemented `BindNativeFramebuffer` to call `glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(nativeHandle))`.

5. `engine/src/Rendering/RenderGraph.cpp` —
   - `Execute`/`ExecuteParallel` now return `bool` (were `void`).
   - Before each pass, RenderGraph binds the write target's `renderTargetFbo` via `BindNativeFramebuffer`.
   - `Execute` restores default FBO (binding 0) via `GL46CommandList::End` after all passes complete.
   - Added topological execution order (`m_ExecutionOrder`) and compile count (`m_CompileCount`).

## Verification

New test `RenderGraphSG6.P4E3M2_OffscreenMeshDrawPilot`:
- Creates offscreen Color Texture + Depth Renderbuffer + FBO (8x8).
- Imports the texture into RenderGraph with `renderTargetFbo = fbo`.
- RenderGraph Draw Pass clears and draws a red triangle (255, 0, 0, 1).
- After `Execute`, caller rebinds target FBO and reads back `255 0 0` — PASS.
- After `Execute`, GL_FRAMEBUFFER_BINDING is 0 — state restoration verified — PASS.
- Debug/Release builds pass.

Cross-checks:
- `P4E3M2F_FboBindingAfterExecute` (FBO binding state contract) — PASS.
- `P4E3M2D_Diagnostic` (draw pass diagnostic) — PASS.
- Full `test_renderer` Debug/Release: 22/22 PASS.
- `test_core` 71/71, `test_debugdraw` 24/24, `test_bridge` 113/113, `test_pick` 10/10 — all PASS.

## Known Limits

- ExecuteParallel production use remains disabled (OpenGL serial only).
- Production MeshRenderer integration into RenderGraph path remains pending (P4-B scope).
- Depth attachment verified present but depth testing not asserted in this pilot.
