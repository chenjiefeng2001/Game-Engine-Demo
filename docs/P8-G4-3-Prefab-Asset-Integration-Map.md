# P8 G4-3 - Prefab Asset Integration Map

**Status**: IMPLEMENTED - VERIFICATION RECORD
**Contract**: `docs/P8-G4-Prefab-Format-Contract.md`
**Baseline**: G4-2 accepted; ABI v1.14 / 60 before this gate
**Result target**: ABI v1.15 / 61 ops

## 1. Integration points

| Area | Change |
|---|---|
| Content type | `Content::AssetType::Prefab = 4`; type string `"Prefab"` |
| Manifest | Prefab GUID/path/type registered in the existing `ContentRegistry` manifest |
| File | `assets/prefabs/<stem>.<digest16>.prefab.json` canonical G4-2 bytes |
| Publish | Existing `RevisionStore::Publish`; committed Scene bytes are the Scene input |
| Reader | `ReadPrefab(bytes, expectedGuid)` from the committed asset path |
| ABI | v1.15 / 61 ops; `CREATE_PREFAB_FROM_ENTITY = 60` |
| Event | Existing `EV_ASSET_IMPORTED`, `type=4` |
| Dependencies | Prefab -> direct Mesh GUIDs; Prefab listed as Mesh dependent |
| Editor API | Native/Managed `CreatePrefabFromEntity`; no Create Prefab UI yet |

## 2. Native publish path

`EditorSession::CreatePrefabFromEntity`:

1. resolves the live source Scene entity;
2. converts current legacy Sprite/Script bindings to the fail-closed capture map;
3. allocates a new Prefab GUID and captures the live subtree;
4. writes canonical Prefab bytes and computes the G4-2 digest;
5. checks the content-addressed target path and writes the file atomically;
6. copies the current registry and registers the Prefab GUID/path/type;
7. reads committed Scene bytes;
8. calls `RevisionStore::Publish(candidateManifest, committedScene)`;
9. on success updates registry/asset order and emits `EV_ASSET_IMPORTED`.

The source Scene, hierarchy, IDs, selection, and dirty state are not modified.
The Prefab file is staged before Publish. A publish failure leaves the old
current manifest/Scene and current bytes intact; a new Prefab content file can
remain as an orphan candidate. This is the frozen non-atomic cross-file boundary.

## 3. Reader and dependency integration

- Prefab assets are read by resolving the manifest path through `ProjectRoot` and
  passing bytes plus the expected Asset GUID to `ReadPrefab`.
- Missing/corrupt Prefab bytes fail Reader and dependency queries; `GetAssetStatus`
  remains the existing file availability check.
- Direct Prefab dependency = unique Mesh GUIDs found in `Mesh` component payloads.
  Missing registry entries are reported with empty path and `resolved=false`.
- Mesh dependency queries include Prefab assets that reference the Mesh.
- Material/Texture dependencies remain transitive through Mesh assets.

## 4. ABI boundary

- Existing G3 ABI v1.14 / 60 operations preserved.
- New additive operation `CREATE_PREFAB_FROM_ENTITY = 60`; count 61; version 1.15.
- Dispatch arguments: entity ID in `strArg`, asset name in `strArg2`; returns asset
  index or failure.
- C export: `EditorSession_CreatePrefabFromEntity`.
- Managed service method exists for G4-4; Create Prefab UI/UIA remains G4-4.
- No event enum change; success reuses `EV_ASSET_IMPORTED type=4`.

## 5. Tests added

`tests/test_bridge/test_prefab_asset.cpp`:

- publish + content-addressed path + stable GUID + cold reopen;
- live subtree capture while committed Scene remains unchanged;
- Prefab Reader with expected GUID;
- Mesh dependency query;
- corrupt/missing file behavior;
- all eight RevisionFaultPoint timings (baseline and first-publish marker cases);
- orphan candidate remains after failed publish but is absent from the manifest;
- C ABI Create Prefab registration.

No production path outside the Prefab integration was modified for G4-3.
