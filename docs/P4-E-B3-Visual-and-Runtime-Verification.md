# P4-E-B3 — Real Viewport & Visual Verification

**Status**: PARTIAL — OFFSCREEN VERIFIED; INTERACTIVE WINDOW PENDING  
**Branch**: avalonia  
**HEAD**: 641ce27  
**Date**: 2026-11-03  
**Precondition**: P4-E-B2 PASS

---

## 1. Verified (Offscreen Pixel Evidence)

`RenderGraphSG6.P4E3M2_OffscreenMeshDrawPilot` (test_renderer Debug/Release):

- Creates 8×8 offscreen Color Texture + Depth Renderbuffer + FBO.
- Imports texture into RenderGraph with `renderTargetFbo = fbo`.
- RenderGraph Draw Pass clears (0,0,0,1) and draws a red triangle (255,0,0,1).
- After `Execute`, caller rebinds target FBO and reads back:
  `pilotMesh postExecute = 255, 0, 0` — **PASS** (red pixels land in target FBO).
- `GL_FRAMEBUFFER_BINDING` after Execute = 0 — **PASS** (state restoration contract).

`P4E3M2F_FboBindingAfterExecute` (FBO binding state contract): **PASS**.
`P4E3M2D_Diagnostic` (draw pass diagnostic): **PASS**.

These establish that RenderGraph produces real, correct FBO pixel output and restores GL state after Execute — the core visual-rendering contract for production adoption.

---

## 2. To Verify (Interactive Desktop Required)

The following require a real Avalonia Editor window + interactive desktop and were **not** run in this headless CI environment:

| Check | Description | Status |
|-------|-------------|--------|
| Static Mesh visible in real Viewport | RenderGraph path renders mesh into presentation FBO, readback to WriteableBitmap | PENDING |
| Mesh Transform correct | Model transform applied via EditorCamera raw matrices | PENDING |
| Camera View/Projection correct | EditorCamera orbit/fly matrices match Direct Renderer | PENDING |
| Color Attachment output correct | Presentation FBO pixels match expected clear+mesh | PENDING |
| Depth Test normal | Z-fighting/occlusion correct between multiple meshes | PENDING |
| Multi-frame sustained rendering | No degradation over N frames | PENDING |
| Resize normal | FBO recreate + RenderGraph Reset each frame | PENDING |
| DPI Scaling normal | Logical → physical pixel mapping correct | PENDING |
| No black frames / no flickering | Every published frame has non-clear content when mesh present | PENDING |
| No persistent GL Error | glGetError == GL_NO_ERROR after each frame | PENDING |

### Direct vs Graph Comparison
Using identical Scene/Camera/Mesh/Shader/Render Target/Viewport Size:
- Capture Direct Renderer image.
- Capture RenderGraph Renderer image.
- Record actual pixel difference.
- Tolerance determined from real test conditions (no preset fixed threshold).

---

## 3. Window Runtime Checks

| Scenario | Expected | Status |
|----------|----------|--------|
| Initialization | RenderGraph device/queue created on headless GL context | VERIFIED (B1) |
| Camera operation | EditorCamera input → view/proj matrices | CODE-REVIEWED |
| Resize | FBO recreate; RenderGraph Reset; next frame compiles | CODE-REVIEWED (B2) |
| Scene switch | meshObjects collected fresh each frame | CODE-REVIEWED |
| Project switch | Session reinit; RenderGraph device recreated | CODE-REVIEWED |
| Path switch (Direct ↔ Graph) | Feature flag toggles without data modification | CODE-REVIEWED |
| Editor Shutdown | Application::Shutdown cleans DebugDraw; device released | CODE-REVIEWED |

---

## 4. Acceptance Criteria

Acceptance requires **real picture or equivalent re-checkable evidence**, not merely `Execute == true`.

The offscreen pixel evidence (255,0,0) satisfies the "RenderGraph produces correct FBO output" sub-criterion. The remaining window-runtime sub-criteria require an interactive desktop and are deferred to a human-run gate.
