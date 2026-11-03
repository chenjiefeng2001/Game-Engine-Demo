# P4-E Post-Closure UIA Verification

**Status**: PARTIAL — OFFSCREEN VERIFIED; INTERACTIVE WINDOW EVIDENCE PENDING  
**Branch**: avalonia  
**HEAD**: cf42133  
**Date**: 2026-11-03  
**Precondition**: F0 PASS (remote synchronized)

---

## 1. Offscreen Pixel Evidence (VERIFIED)

`RenderGraphSG6.P4E3M2_OffscreenMeshDrawPilot` (test_renderer Debug/Release):
- Creates 8×8 offscreen Color Texture + Depth Renderbuffer + FBO.
- Imports texture into RenderGraph with `renderTargetFbo = fbo`.
- RenderGraph Draw Pass clears (0,0,0,1) and draws a red triangle (255,0,0,1).
- After `Execute`, caller rebinds target FBO and reads back:
  `pilotMesh postExecute = 255, 0, 0` — **PASS**.
- `GL_FRAMEBUFFER_BINDING` after Execute = 0 — **PASS** (state restoration contract).

`P4E3M2F_FboBindingAfterExecute` (FBO binding state contract): **PASS**.

This establishes that RenderGraph produces real, correct FBO pixel output and restores GL state after Execute.

---

## 2. Real Editor Window Verification (PENDING — Interactive Desktop Required)

The following require a real Avalonia Editor window + interactive desktop and were **not** run in this verification cycle:

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
- Tolerance determined from real test conditions.

### Blocker for Real Window Verification
The RenderGraph production path feature flag (`m_ProductionUseRenderGraph`) is not exposed through the EngineHost C ABI or managed Avalonia side. A managed-side toggle or debug menu entry is required to switch paths at runtime in the Editor. This is an additive ABI change (allowed per Gate A contract: "优先复用，必要时 additive").

Until the feature flag is exposed, real-window verification must enable the flag via a native test harness or temporary EngineHost modification.

---

## 3. UIA Evidence (PENDING)

Existing UIA tests (T6ConsoleUITests) pass (6/6) in this environment, confirming interactive desktop is available. However, no UIA test currently exercises the Production Viewport RenderGraph path. A new UIA test is required.

---

## 4. Conclusion

`F1 — PARTIAL — OFFSCREEN VERIFIED; INTERACTIVE WINDOW PENDING`.

Offscreen pixel evidence (255,0,0) and FBO state restoration (binding 0) establish the core RenderGraph visual-rendering contract. Real Editor window verification requires:
1. Exposing `m_ProductionUseRenderGraph` through EngineHost C ABI + managed side (additive).
2. A new UIA test or human-run gate exercising the RenderGraph path.

These are independent follow-up tasks that do not affect P4-E Gate B closure status.
EOF