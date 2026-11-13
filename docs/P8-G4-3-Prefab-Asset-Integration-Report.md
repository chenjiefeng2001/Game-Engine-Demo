# P8 G4-3 - Prefab Asset Integration Report

**Status**: IMPLEMENTED - PENDING ACCEPTANCE
**Contract**: `docs/P8-G4-Prefab-Format-Contract.md`
**Scope**: AssetType, manifest/content-addressed storage, RevisionStore publish,
committed-Scene boundary, reader/dependency integration, ABI v1.15.
**Excluded**: Create Prefab editor UI/UIA (G4-4), Instantiate (G5), Override
(G6).

## 1. Source changes

- `engine/include/Engine/Core/Content/ContentAsset.h`
  - adds `AssetType::Prefab = 4`, `"Prefab"` type serialization.
- `bridge/src/EditorSession.h/.cpp`
  - adds `CreatePrefabFromEntity`;
  - stages canonical Prefab bytes at
    `assets/prefabs/<sanitized-stem>.<digest16>.prefab.json`;
  - registers the new GUID in a candidate manifest;
  - publishes with committed Scene bytes;
  - emits `EV_ASSET_IMPORTED type=4`;
  - adds Prefab dependency/dependent branches.
- `bridge/include/editor_bridge/capi.h`
  - ABI v1.15, count 61, op 60, C export.
- `bridge/src/capi.cpp`
  - C export implementation.
- `bridge/src/EngineHost.cpp`
  - Dispatch op 60.
- Managed ABI:
  - version/op constants, P/Invoke, service method, Asset Browser type mapping
    and filter. No Create Prefab UI.
- Tests:
  - `tests/test_bridge/test_prefab_asset.cpp`
  - ABI version/count updates in existing tests.
  - CMake test registration only.

## 2. Publish semantics

- Asset GUID is generated before capture and included in canonical bytes/Digest.
- Capture uses the live selected subtree.
- The Publish Scene argument is read from the committed revision/legacy mirror,
  not from unsaved live Scene edits.
- The target file must not collide with different bytes.
- `RevisionStore::Publish` failure leaves old current/Manifest/Scene intact.
- A staged orphan is possible and intentionally not treated as a registered asset.
- Reopening reads the Prefab from the committed manifest path and verifies the
  expected GUID.

## 3. Asset behavior

- `GetAssetType` returns 4.
- `GetAssetPath`/`GetAssetGuid` use the existing append-only asset index order.
- `GetAssetStatus` keeps existing existence/non-empty semantics.
- `GetAssetDependencies`:
  - Prefab -> unique direct Mesh GUIDs;
  - Mesh queries include Prefab dependents;
  - missing Mesh dependencies stay reported with empty path/resolved=false.

## 4. ABI

```text
v1.15 / 61 ops
ENGINE_HOST_OP_CREATE_PREFAB_FROM_ENTITY = 60
EditorSession_CreatePrefabFromEntity(handle, entityId, assetName)
```

Existing op numbers/layouts are unchanged. `EV_COUNT` remains 14. No generic
Prefab API or Instantiate operation is introduced.

## 5. Known limits

- No Create Prefab UI; G4-4 owns the real editor workflow and UIA.
- No same-GUID Prefab update API; each Create call allocates a new asset GUID.
- Prefab file bytes remain outside `RevisionStore` current hashes.
- Legacy Sprite/Script bindings remain fail-closed.
