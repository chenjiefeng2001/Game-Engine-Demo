# P7-A G2 — Material Inspector Closure

**Status**: PASS — SCOPE-BOUND

- Asset Browser material selection opens a Material section in Inspector.
- Editable fields: Base Color RGBA, Metallic, Roughness, Emissive, Alpha Cutoff/Mode.
- Read-only: asset path, diagnostic.
- UI note explicitly states only Base Color currently affects the production shader; other
  fields are stored but not rendered.
- Save calls the native material edit transaction and reloads assets/diagnostics.
- UIA (`B6MeshEditorUITests`) selects Material, edits Base Color R, saves, and verifies the value
  survives editor close/relaunch/project reopen.
- Native pixel test `MaterialEditUpdatesProductionPixels` verifies the production FBO color
  changes after material save and remains correct in RenderGraph.
