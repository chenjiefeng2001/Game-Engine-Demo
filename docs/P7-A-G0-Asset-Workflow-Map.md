# P7-A G0 — Asset Workflow Map (Read-only)

**Status**: COMPLETE — MINIMAL MAP
**Baseline**: P6-1B CLOSED; P7-C CLOSED; HEAD `5de8f1a`; P7-C changes uncommitted

## 1. Material data / runtime capability

Material asset v1 fields: `slot`, `name`, `baseColorFactor[4]`, `metallicFactor`, `roughnessFactor`, `emissiveFactor[3]`, `alphaCutoff`, `alphaMode`, five texture refs (`baseColor/normal/occlusion/emissive/metallicRoughness`).

Production `3d_lit` shader / `ApplyMeshMaterialBinding` currently consume:
- Base Color factor (only when no base texture) and baseColor texture;
- normal texture + strength.

Stored-only / not rendered by current shader: metallicFactor, roughnessFactor, emissiveFactor, emissive/occlusion/metallicRoughness textures, alphaMode/cutoff/blend. P7-A must not present these as effective.

## 2. Material ownership / reimport

- Imported Material GUIDs are preserved by B3 stem+slot prefix rebind; material files are content-addressed.
- No source provenance is stored inside Material JSON; Mesh JSON stores only `sourceFile`.
- `ImportModelAsset` has an exact-path idempotency fast path; forced reimport must bypass it to reset source material.
- Decision: material edits are recorded in `.engine-content/material-edits.json`; normal source reimport is blocked when a referenced material is edited; explicit discard reimport bypasses the fast path, resets source Material, and clears the marker. No silent overwrite.

## 3. Dependency data

- Mesh -> Material mapping from committed Mesh JSON `materialNames` + committed registry prefix matching (`ResolveMaterialAssetPaths`).
- Material -> Texture mapping from committed Material JSON refs + registry path lookup.
- Source -> Mesh from Mesh JSON `sourceFile`; MTL/external buffer dependency files are parsed during import but not persisted in v1 asset data. Dependency view therefore exposes source file only for source provenance; this is a known limitation.
- No filesystem scans, no name guessing beyond the frozen writer path convention.

## 4. Reimport data flow

`sourceFile` -> `ImportModelAsset` parser -> digest v2 -> content-addressed Mesh/Material/Texture staging -> one `RevisionStore::Publish` -> current -> `MeshRuntimeScene` revision refresh.

`ReimportAsset` resolves source, checks edited-material guard, calls the existing import pipeline, and never partially publishes.

## 5. Existing APIs reused / added

Reused: ContentRegistry, RevisionStore, MaterialAssetReader, Mesh/Material paths, AssetBrowser, Inspector property channel, MeshRuntimeScene.
Added (minimal ABI v1.11): `EditorSession_GetMaterialJson/UpdateMaterial/GetAssetDependencies/ReimportAsset` + EngineHost ops 33–36. No Scene/asset format change.

## 6. Worktree boundary

P7-C changes remain uncommitted; P7-A edits do not revert or rewrite them. Dirty submodules ×3 and `crashes/` remain untouched.
