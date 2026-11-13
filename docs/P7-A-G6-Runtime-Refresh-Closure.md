# P7-A G6 — Atomic Runtime Refresh Closure

**Status**: PASS — SCOPE-BOUND

- Material edit publishes a new revision; `MeshRuntimeScene` detects the revision change and
  rebuilds Material/Texture binding before replacing the active component state.
- Production pixel test `MaterialEditUpdatesProductionPixels`:
  white factor material → edit to red → Direct FBO probes change to red ambient; RenderGraph
  matches.
- Texture-only/geometry reimport behavior remains covered by B5 `test_mesh_pixel` (reimport
  identity/revision/pixel tests).
- Old runtime resources remain until new load succeeds; existing G4 revision-switch tests cover
  failure retention.
- Known limitation: no dedicated material-edit failure fault injection beyond existing Publish
  fault matrix; Publish failure leaves old current and runtime untouched.
