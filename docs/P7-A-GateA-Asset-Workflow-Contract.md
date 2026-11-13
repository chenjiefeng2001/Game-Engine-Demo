# P7-A Gate A — Asset Workflow Contract (Frozen)

**Status**: FROZEN
**ABI**: additive v1.11

## C1 Material edit

- Editable v1 fields: `baseColorFactor[4]`, `metallicFactor`, `roughnessFactor`,
  `emissiveFactor[3]`, `alphaCutoff`, `alphaMode`.
- Texture slots remain read-only in P7-A.
- Base Color is the only field documented as currently affecting production pixels;
  metallic/roughness/emissive/alpha are stored but not rendered by the current shader.
- Validation: finite numbers only; array sizes exact; alphaMode OPAQUE/MASK/BLEND;
  unknown patch fields reject; edited JSON must pass `MaterialAssetReader`.
- Save: same GUID, content-addressed new Material path, one new revision.
- No-op content: zero new revision.
- Invalid/unknown/unsafe data: no Publish; old current intact.
- Edits are recorded in `.engine-content/material-edits.json`.

## C2 Imported vs edited Material ownership

- A user-edited Material is **not silently overwritten**.
- Normal source Reimport checks all Mesh material slots; if any referenced Material GUID is
  marked edited, reimport fails closed with `reimport blocked: material <guid> has local edits`.
- Explicit **Reimport (Discard Edits)** bypasses the fast path, reimports source content,
  preserves the Material GUID, and clears the edit markers.
- No automatic merge; no scene format/GUID semantics change.

## C3 Dependency graph

- Authoritative sources: committed registry + committed Mesh/Material JSON.
- Edges: Mesh→Material (slot), Material→Texture (role), Material→Texture via Mesh depth-2,
  Source→Mesh (`sourceFile`), and reverse dependents.
- Missing targets remain listed with `resolved=false`; no synthetic replacement.
- MTL/external buffer dependency files are not persisted in v1; only source file provenance
  is exposed. This is a known limitation, not a guessed dependency.

## C4 Reimport

- Unchanged source: no revision (normal path).
- Changed source: same GUID, new current/revision.
- Missing source/dependency: fail closed, old current retained.
- Publish fault: no partial current.
- Edited Material: blocked unless explicit discard.
- Runtime: old resources remain until new revision is fully loaded; runtime refresh uses
  existing B5/G4 revision switch semantics.

## C5 ABI

- `ENGINE_HOST_ABI_VERSION = v1.11`; ops:
  `GET_MATERIAL_JSON=33`, `UPDATE_MATERIAL=34`, `GET_ASSET_DEPENDENCIES=35`, `REIMPORT_ASSET=36`;
  op count 37.
- New exports: `EditorSession_GetMaterialJson/UpdateMaterial/GetAssetDependencies/ReimportAsset`.
- `RuntimeResult` layout and existing op/status values unchanged.
- Material/Scene/RevisionStore formats unchanged.

## C6 Acceptance

Native material edit/persistence/reimport/dependency tests; production pixel after material
edit; UIA Material Inspector + dependency view + reimport + cold restart; Debug/Release
inventory with no new failure beyond PICK-01/SDK SKIP.
