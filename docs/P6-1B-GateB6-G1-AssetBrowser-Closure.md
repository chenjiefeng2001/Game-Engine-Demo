# P6-1B Gate B6-G1 — AssetBrowser Resource Discovery

**Status**: PASS — SCOPE-BOUND

## 1. Native type/availability contract

- `EditorSession::GetAssetType`: `0=Texture, 1=Script, 2=Mesh, 3=Material` (`bridge/src/EditorSession.cpp`).
- New `EditorSession_GetAssetStatus` / EngineHost op 32:
  `0=available, 1=missing/unreadable, 2=path rejected, -1=bad index`.
- ABI increment v1.10 (additive op/export; `RuntimeResult` remains 76B, existing op numbers unchanged).
- Unknown types remain impossible in committed manifests (`ContentRegistry::LoadManifest` rejects them); no extension-based guessing.

## 2. Managed Asset Browser

- `MainViewModel` type mapping now includes `2 => "Mesh"`, `3 => "Material"`; filter options are `All/Texture/Script/Mesh/Material`.
- Each row displays **Name**, Type, Path, **Availability**, GUID.
- Name is the committed path basename; no manifest schema change.
- Refresh sources unchanged: open project, `EV_ASSET_IMPORTED`, `EV_ASSET_RENAMED`, explicit import/reload.
- Only committed assets are enumerated (`m_AssetOrder` built from committed current); staging is not visible.

## 3. Tests

- `ImportCommitTest.AssetTypeAndAvailabilityContract`:
  - Mesh type 2, Material type 3, Texture type 0;
  - availability 0 while committed file exists; becomes 1 after removing the committed mesh file.
- `ImportCommitTest.MeshComponentGuidRoundTripThroughCAbi` also enumerates imported assets.
- B6 real UIA test (`B6MeshEditorUITests`) prepares an OBJ+MTL import and verifies AssetBrowser contains **both** a `.mesh` and a `.material.json` row before selecting the Mesh.
- Inventory: test counts included; no staging/unknown-type paths exposed.

## 4. Limitations

- No asset delete/rename/import UI changes beyond existing controls.
- AssetBrowser shows availability, not content preview/dependency details.
