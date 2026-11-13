# P8 G5 - Prefab Instance Implementation Report

**Status**: CORE PASS - ACCEPTED - SCOPE-BOUND
**Contract**: P8 Gate A C8 / frozen G4 Prefab format
**Scope**: Persistent Prefab Instance root anchor, Local ID mapping, Scene v2
metadata, instantiate, Save/cold restart, independent instances, ABI v1.16.
**Excluded**: Overrides (G6), Apply/Revert (G6), source reconciliation (G7),
Nested Prefab.

## 1. Scene identity and metadata

`Engine::PrefabInstanceMetadata` records:

- `prefabGuid`;
- `sourceRevision` (current project revision at instantiate);
- `localToScene` (`Prefab Local ID -> Scene Entity ID`).

`Scene` owns metadata keyed by the instance root Scene Entity ID. Public
operations:

- `SetPrefabInstanceMetadata`;
- `GetPrefabInstanceMetadata`;
- `RemovePrefabInstanceMetadata`.

The setter validates nonzero GUID, nonnegative source revision, nonzero unique
local/scene IDs, root membership, and that every mapped entity belongs to the
anchor subtree. Removing an instance root/descendant removes the associated
metadata record.

## 2. Scene v2 persistence

`SerializedEntity` has an optional `prefabInstance`:

```json
{
  "guid": "<prefab-guid>",
  "sourceRevision": 1,
  "localToScene": {
    "1": 21,
    "2": 22
  }
}
```

- v1 scenes remain valid and have no Prefab metadata.
- v2 Capture writes metadata only for the anchor root.
- v2 deserialize validates GUID, revision, IDs, map uniqueness, root membership,
  complete subtree coverage, and rejects nested instance metadata.
- `InstantiateScene` reapplies metadata after hierarchy construction.
- Save/reopen/Undo/Redo preserve the mapping.

## 3. Instantiate

`EditorSession::InstantiatePrefab(assetIndex, parentEntityId, outRoot)`:

1. resolves and strictly reads the committed Prefab with expected GUID;
2. builds a detached object tree with strict component deserialization;
3. allocates fresh Scene IDs through normal Scene registration;
4. preserves names, Local Transforms, Mesh GUIDs, and component data;
5. remaps internal parents through the new objects;
6. records `prefabGuid`, project source revision, and Local ID map;
7. commits one existing Scene Snapshot transaction;
8. marks the Scene dirty and emits an entity-created event.

Failure leaves no partial instance. GPU handles/runtime state are never copied.
Each instantiate gets independent Scene Entity IDs.

## 4. ABI / UI surface

ABI v1.16 / 63 ops:

- `INSTANTIATE_PREFAB = 61` (Dispatch asset index, parent ID in string arg);
- `GET_LAST_INSTANTIATED_ENTITY_ID = 62` (Fetch, 8 bytes);
- C export `EditorSession_InstantiatePrefab`;
- Managed service method and Asset Browser `Instantiate Prefab` button.

The button is available when a Prefab asset is selected. G5 automated evidence is
Native/ABI; a dedicated instantiate UIA workflow is not claimed.

## 5. Tests

`test_bridge/PrefabInstanceTest` (3 tests):

- two independent instances have distinct root/child Scene IDs, guard mapping,
  shared Prefab GUID, and copied Mesh GUID;
- Save/cold reopen preserves instance metadata and Local ID map; Undo/Redo
  round-trip is checked before Save;
- missing Prefab bytes fail without a partial instance.

C ABI Create Prefab test also invokes `EditorSession_InstantiatePrefab`.
