# P8 G4-A - Prefab Format Contract

**Status**: ACCEPTED - FROZEN - IMPLEMENTED THROUGH G4-4
**Based on**: `docs/P8-G4-G0-Prefab-Asset-Map.md` (accepted)
**Parent contract**: `docs/P8-GateA-Scene-Composition-Contract.md` C7/C10
**Scope**: Committed Prefab Asset model, format, validation, storage, publishing,
minimal editor creation, and the required C7 implementation amendments.
**Not scope**: Prefab Instance, Local ID -> Scene ID instantiate mapping,
Overrides, Apply/Revert, source reconciliation, Nested Prefab, Variant.

No production source, test, CMake, or ABI file is changed by G4-A.

## 1. C7 implementation amendments

### Amendment A - Entity name is part of the Prefab contract

Scene v2 persists entity `name`. The C7 minimum entity shape did not carry it.
G4 adds it:

```text
PrefabEntity
  localId
  name
  parentLocalId
  localTransform
  components[]
```

- `name` is required.
- It is a JSON string and must be valid UTF-8.
- It may be empty. Capture and write preserve it exactly; no trim, case fold,
  path sanitization, or duplicate-name resolution is applied to the entity name.
- If a UI asset name is needed for the file stem, that is a separate sanitized
  asset name, never a replacement for `PrefabEntity.name`.
- `name` participates in canonical serialization and Digest.

### Amendment B - Strict component schema validation

The existing component `Deserialize()` implementations ignore unknown fields.
They cannot be used as the Prefab validation boundary.

The Prefab Reader must implement an independent exact schema for each allowlisted
component:

- unknown Component type rejects;
- unknown component field rejects;
- duplicate Component type rejects;
- wrong JSON types/ranges reject;
- malformed nested data rejects;
- failure returns no partial `PrefabModel`.

The current allowlist has no Scene Entity reference. Skinning bone `parent` is a
bone index, not a Scene Entity ID. A future component with an Entity reference
may not join the allowlist until an explicit Local ID mapping rule is frozen.

## 2. Frozen decisions A1-A16

| ID | Decision |
|---|---|
| A1 | `Content::AssetType::Prefab = 4`; existing values 0-3 unchanged. |
| A2 | `format = "engine.prefab"`, `formatVersion = 1`, extension `.prefab.json`. |
| A3 | Files are stored under `assets/prefabs/<stem>.<digest16>.prefab.json`. |
| A4 | Entity `name` is required and included in Digest. |
| A5 | Local IDs are nonzero `uint64`, encoded as JSON unsigned integers, unique in the Prefab. |
| A6 | Initial Capture assigns `1..N` by deterministic DFS preorder; published IDs are persistent; future updates retain matched IDs and never reuse deleted IDs. |
| A7 | Root `parentLocalId` is JSON `null`; non-root must reference a valid Local ID; `0` is invalid. |
| A8 | Local Transform uses Scene v2 position[3], quaternion[4] `(x,y,z,w)`, scale[3]; all values must be finite. |
| A9 | Canonical writer orders entities by ascending `localId`; reader accepts any order but validates the graph. |
| A10 | Canonical writer orders components by stable type string; reader accepts any order but rejects duplicate types. |
| A11 | Strict component schemas below; unknown field/type/duplicate fails closed. |
| A12 | Digest covers the complete canonical file bytes, including format, GUID, names, IDs, parents, transforms, components, and asset GUIDs. |
| A13 | `prefabGuid` is included in the Digest and therefore in file addresses; file Digest and bytes cannot disagree. |
| A14 | Capture reads the live selected subtree; `RevisionStore::Publish` receives committed Scene bytes, never unsaved live Scene edits. |
| A15 | Reader validates GUID syntax but does not require dependencies to exist; availability is reported separately and resolved by G5. |
| A16 | Additive ABI v1.15 / 61 ops with op `CREATE_PREFAB_FROM_ENTITY = 60`; minimal Managed API and Hierarchy UI. |

The four earlier undecided groups A5-A13 are now frozen by this table and the
sections below.

## 3. Prefab file schema (formatVersion 1)

### 3.1 Top-level object

```json
{
  "format": "engine.prefab",
  "formatVersion": 1,
  "prefabGuid": "0123456789abcdef0123456789abcdef",
  "rootLocalId": 1,
  "entities": [
    {
      "localId": 1,
      "name": "Root",
      "parentLocalId": null,
      "localTransform": {
        "position": [0.0, 0.0, 0.0],
        "rotation": [0.0, 0.0, 0.0, 1.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "components": [
        {
          "type": "Mesh",
          "data": {
            "meshGuid": "0123456789abcdef0123456789abcdef"
          }
        }
      ]
    }
  ]
}
```

Exact top-level keys:

- `format`: string exactly `"engine.prefab"`;
- `formatVersion`: integer exactly `1`;
- `prefabGuid`: string of exactly 32 lowercase hexadecimal characters, nonzero;
- `rootLocalId`: nonzero unsigned integer;
- `entities`: non-empty array of entity objects.

Unknown top-level fields reject. Missing required fields reject. Unsupported
future `formatVersion` rejects; there is no best-effort old-format parse.

### 3.2 Entity object

Exact keys:

- `localId`: nonzero unsigned integer, unique within the Prefab;
- `name`: required string, valid UTF-8, may be empty;
- `parentLocalId`: `null` for the root, otherwise a nonzero unsigned integer;
- `localTransform`: exact object described below;
- `components`: array (may be empty).

Unknown entity fields reject. Duplicate Local IDs reject. `0` is never a valid
Local ID or Parent Local ID.

Hierarchy validation:

- exactly one entity has `parentLocalId == null`;
- that entity's `localId` equals `rootLocalId`;
- every other entity has an existing parent;
- self-parent rejects;
- cycles reject;
- every entity is reachable from the root;
- additional roots/orphans reject;
- root must not have a parent.

### 3.3 Local Transform

`localTransform` has exactly these keys:

```json
{
  "position": [x, y, z],
  "rotation": [x, y, z, w],
  "scale": [sx, sy, sz]
}
```

- arrays have exactly 3, 4, and 3 numbers;
- all values are finite floats representable by the engine;
- rotation uses the Scene v2 quaternion order `(x, y, z, w)`;
- no Euler or World Matrix is stored;
- no normalization or repair is applied by the Reader.

### 3.4 Canonical serialization

The canonical writer:

1. validates the complete `PrefabModel`;
2. sorts entities by ascending `localId`;
3. sorts components within each entity by stable type string;
4. emits the JSON with `nlohmann::json::dump(2)`;
5. writes no trailing byte after the closing brace;
6. computes `digest16 = RevisionStore::HashBytes(canonicalBytes)`;
7. never writes a partial file.

Every canonical byte contributes to Digest, including `prefabGuid`, entity names,
order of entity and component arrays, nested Skinning arrays, and asset GUID
strings. The Reader accepts any valid JSON whitespace/key order, but normalizes
to the model; only the Writer's canonical bytes are used for path Digest.

### 3.5 Path and name stem

```text
assets/prefabs/<stem>.<digest16>.prefab.json
```

- `digest16` is the full 16-character lowercase FNV-1a 64-bit hash returned by
  `RevisionStore::HashBytes`.
- `prefabGuid` is included in the digest. Different GUIDs cannot claim the same
  content-addressed file path through an equal semantic digest.
- `<stem>` is the sanitized asset name: ASCII `[A-Za-z0-9_-]`, maximum 48
  characters, fallback `prefab` when empty.
- The file must resolve under `ProjectRoot`; path escape rejects.
- If the target path already exists with different bytes, publish fails closed;
  it must not overwrite.
- Old Prefab paths are never modified or deleted by G4.

## 4. Component schema

The `components` array contains objects with exact keys `type` and `data`.
`type` must be one of the frozen names below. `data` is an object with the exact
fields below; unknown fields reject. Duplicate `type` in one entity rejects.

### 4.1 Mesh

```json
{ "meshGuid": "0123456789abcdef0123456789abcdef" }
```

- `meshGuid` is optional; when absent the component has no mesh GUID.
- When present it is a nonzero 32-character lowercase hex GUID.
- No material slot, visible/color, runtime status, GPU handle, or path field is
  stored.
- Asset availability is not checked by the Reader.

### 4.2 Camera

```json
{
  "enabled": true,
  "zoom": 1.0,
  "viewport": "",
  "bounds": ""
}
```

- all four fields are required;
- `enabled` is bool;
- `zoom` is finite and greater than zero;
- `viewport` and `bounds` are valid UTF-8 strings;
- these strings are not Entity IDs and are not remapped.

### 4.3 Collider

```json
{
  "enabled": true,
  "shape": "Circle",
  "radius": 0.5,
  "halfX": 0.5,
  "halfY": 0.5,
  "isSensor": false,
  "category": 1,
  "mask": 65535
}
```

- all fields required;
- `shape` is `Circle` or `Box`;
- `radius`, `halfX`, `halfY` are finite and non-negative;
- `category` and `mask` are integers in `[0, 65535]`;
- `isSensor` is bool.

### 4.4 RigidBody

```json
{
  "motionType": "Dynamic",
  "linearDamping": 0.0,
  "angularDamping": 0.0
}
```

- all fields required;
- `motionType` is `Static`, `Kinematic`, or `Dynamic`;
- damping values are finite and non-negative.

### 4.5 Skinning

```json
{
  "visible": true,
  "skeleton": [
    {
      "name": "root",
      "parent": -1,
      "bind": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    }
  ],
  "skinnedMesh": {
    "boneMapping": [0],
    "vertices": [
      {
        "pos": [0, 0, 0],
        "nrm": [0, 0, 1],
        "uv": [0, 0],
        "tan": [1, 0, 0],
        "indices": [0, 0, 0, 0],
        "weights": [1, 0, 0, 0]
      }
    ],
    "indices": []
  },
  "animation": {
    "duration": 0.0,
    "localTime": 0.0,
    "playing": false,
    "timeScale": 1.0,
    "tracks": []
  }
}
```

- `visible` is required and bool;
- `skeleton`, `skinnedMesh`, and `animation` are independently optional objects;
- no other fields are allowed at any level;
- skeleton bone entries require `name` string, `parent` integer, and `bind`
  exactly 16 finite numbers;
- bone `parent` is `-1` for a root bone or a valid earlier bone index; it is a
  bone index, never a Scene Entity ID;
- `skinnedMesh.boneMapping` is an integer array;
- each vertex requires `pos`[3], `nrm`[3], `uv`[2], `tan`[3], `indices`[4]
  unsigned integers, and `weights`[4] finite numbers;
- `skinnedMesh.indices` is an unsigned-integer array;
- animation requires finite `duration` >= 0, finite `localTime`, bool `playing`,
  finite non-negative `timeScale`, and a `tracks` array;
- each track requires `name` string, integer `type`, and a `keys` array;
- each key is `[finite time, number-or-array]`, where the value array has 2, 3,
  or 4 finite numbers;
- `localTime` is not required to be within `duration`; runtime playback clamps as
  existing code does.

## 5. Local ID and identity rules

### 5.1 Initial allocation

For a new Prefab capture:

1. start at the selected Scene subtree root;
2. walk depth-first preorder using the existing `GetChildren()` order;
3. assign Local IDs `1..N`.

Initial `rootLocalId` is therefore `1`.

### 5.2 Persistence

Once a Prefab revision is published, its Local IDs are persistent identities of
that Prefab asset.

- Future Prefab update must retain IDs for matched entities.
- New entities receive unused IDs.
- Deleted IDs are never reused.
- G4 does not expose a Prefab update/reconciliation API. The rule is frozen for
  G7 and for future asset updates; G4 only creates new Prefab assets.

### 5.3 Scene identity

- Scene Entity IDs never appear in Prefab bytes.
- Runtime GameObject IDs never appear in Prefab bytes.
- `prefabGuid` is the Asset GUID and is stable across Prefab revisions.
- Local IDs are scoped to one Prefab asset/revision and are not Asset GUIDs.

## 6. Digest and content addressing

Frozen rule:

```text
canonicalBytes = Writer(PrefabModel)
digest16      = RevisionStore::HashBytes(canonicalBytes)   // 16 lowercase hex
path          = assets/prefabs/<sanitized-stem>.<digest16>.prefab.json
```

Properties:

- every semantic field is covered;
- `prefabGuid` is covered;
- equal canonical bytes always produce the same digest and path;
- different Prefab GUIDs cannot collide by excluding identity from the digest;
- the file's canonical bytes and its address digest are consistent;
- the Reader does not trust the path digest as content proof; the G4-3 asset
  resolver verifies that the file's `prefabGuid` equals the manifest Asset GUID.

No separate "semantic digest excluding GUID" is introduced in G4.

## 7. Capture and publish semantics

### 7.1 Capture

`CapturePrefabFromScene`:

- accepts a Scene and a root Scene Entity ID;
- captures the complete subtree (the selected root plus all descendants);
- rejects zero/unknown entity IDs;
- rejects legacy Sprite/Script bindings;
- rejects non-allowlisted components;
- uses the strict component schemas;
- maps Scene Entity IDs to new Local IDs only in the capture map; no Scene IDs
  are written;
- copies entity names and Local Transforms;
- sorts components before model construction;
- validates the final PrefabModel;
- returns no partial model on failure.

Capture is independent of RevisionStore and can be tested as a value operation.

### 7.2 Publish

The G4-3 integration must follow this order:

```text
Capture + Validate
  -> canonical bytes + digest
  -> write content-addressed file (no overwrite)
  -> candidate ContentRegistry copy (Prefab GUID -> path, AssetType::Prefab)
  -> read committed Scene bytes
  -> RevisionStore::Publish(candidateManifest, committedSceneBytes)
  -> success: update in-memory registry, append asset order, emit EV_ASSET_IMPORTED
  -> failure: keep old current, manifest, Scene, registry; no success event
```

Rules:

- Live unsaved Scene edits are used for capture, but are not committed by the
  Prefab Publish. `Publish` receives the committed Scene bytes.
- The selected Scene entity/subtree is not deleted, replaced, or modified.
- No Prefab Instance metadata is created.
- A failed publish may leave an unreferenced Prefab candidate file. This is an
  accepted staging artifact and must not be reported as a successful asset.
- The old committed Prefab file and old manifest revision remain intact.
- No cross-file atomic claim is made.

### 7.3 Idempotency and revisions

- A new Create call allocates a new nonzero `prefabGuid`.
- Publishing the same GUID with identical canonical bytes and the same path is
  a no-op for content; it must not create a duplicate manifest entry.
- Publishing the same GUID with changed canonical bytes writes a new path and a
  new Manifest revision; the GUID remains stable, old paths remain.
- G4 does not expose a generic "update existing Prefab" UI or ABI. The lower
  layer may support stable-GUID publication for G4-3 tests and future G7.

## 8. Reader rules

`ReadPrefabJson(bytes, optional expectedGuid)`:

1. reject empty/invalid JSON;
2. reject non-object roots;
3. reject unknown top-level fields;
4. require exact `format` and `formatVersion = 1`;
5. require canonical 32-hex nonzero `prefabGuid`;
6. if an expected GUID is supplied and differs, reject;
7. validate `rootLocalId`, entity array, names, IDs, parents, cycles,
   reachability, and single-root identity;
8. validate Transform shapes and finite values;
9. validate components with the exact frozen schemas;
10. reject unknown fields at every level;
11. return either a complete valid model or `ok = false` with a diagnostic;
12. never repair, skip, or retain partial data.

Reader errors are diagnostics only; no partial `PrefabModel` is returned.

## 9. Asset type, dependencies, status, and ABI

### 9.1 Asset type

- `Content::AssetType::Prefab = 4`.
- `ToString` returns `"Prefab"`.
- `TryFromString("Prefab")` succeeds.
- Manifest type `"Prefab"`.
- `EditorSession_GetAssetType` returns `4` for Prefab.
- Managed Asset Browser maps `4` to `"Prefab"` and adds it to the type filter.
- Existing Texture/Script/Mesh/Material values and behavior do not change.
- The legacy `Engine::AssetType::Prefab` / `.prefab` system is not reused.

### 9.2 Dependencies

The Prefab direct-dependency query reads the Prefab's Mesh component GUIDs.

- A GUID found in the current registry reports `type="Mesh"`, `path`, `guid`,
  and `resolved` based on file availability.
- A valid GUID absent from the registry remains a reported dependency with an
  empty path and `resolved=false`.
- Material/Texture references are transitive through Mesh assets and are not
  duplicated as direct Prefab dependencies.
- Missing dependencies do not fail the Prefab Reader; G5 handles runtime
  availability.

### 9.3 Status

G4 keeps the existing `GetAssetStatus` semantics for Prefab: path resolves,
file exists, and file is non-empty. Strict schema validity is established by the
Prefab Reader/G4-3 read path, not by inventing a new status enum.

### 9.4 ABI

Frozen additive ABI:

```text
Version: v1.15
Op count: 61
New op: ENGINE_HOST_OP_CREATE_PREFAB_FROM_ENTITY = 60
```

Dispatch contract:

- `strArg` = source Scene Entity ID, decimal string;
- `strArg2` = requested asset name, UTF-8 (may be empty);
- `arg0` = 0/unused;
- return >= 0 = new asset index;
- return < 0 = failure and diagnostic.

Native export:

```c
int32_t EditorSession_CreatePrefabFromEntity(
    EditorSessionHandle h, uint64_t entityId, const char* assetName);
```

Managed surface:

```csharp
int CreatePrefabFromEntity(ulong entityId, string? assetName);
```

No new event type is added. Success emits the existing `EV_ASSET_IMPORTED` with
`type=4`, the new asset index, GUID, and path. `EV_COUNT` remains 14.
No Prefab Instance/Get/Apply operation is added in G4.

## 10. Minimal editor workflow

Hierarchy adds:

- a Prefab asset name text box (`PrefabNameBox`);
- a `Create Prefab` button (`CreatePrefabBtn`).

Behavior:

1. require a selected hierarchy entity;
2. default a blank name to the selected root entity name;
3. call the additive Create Prefab operation with the selected Scene Entity ID;
4. do not alter the Scene, its hierarchy, IDs, or selection;
5. reload the Asset Browser;
6. show a Prefab asset with type `Prefab`, stable GUID, path, and status;
7. log/display a clear failure; no event or successful asset on failure.

No generic asset path chooser, Prefab Inspector, Instantiate, Override, Apply, or
Revert is part of G4.

## 11. Required implementation boundaries

G4-1 may add:

```text
engine/include/Engine/Core/Content/PrefabAsset.h
engine/src/Core/Content/PrefabAsset.cpp
```

covering `PrefabModel`, `PrefabEntity`, `PrefabComponentData`,
`PrefabDiagnostic`, `PrefabValidationResult`, and `PrefabLocalIdMap`.

G4-2 may add:

```text
engine/include/Engine/Core/Content/PrefabSerializer.h
engine/src/Core/Content/PrefabSerializer.cpp
```

covering canonical writer, strict reader, and Digest helpers.

G4-3 may touch:

```text
engine/include/Engine/Core/Content/ContentAsset.h
engine/src/Core/Content/ContentAsset.cpp
bridge/src/EditorSession.h/.cpp
bridge/include/editor_bridge/capi.h
bridge/src/capi.cpp
bridge/src/EngineHost.cpp
editor_avalonia/...
tests/...
```

Only the frozen additions above; no second asset registry, no second revision
store, no Prefab Instance metadata.

## 12. Required verification

### G4-1

- single-entity and multi-level Capture;
- initial DFS Local IDs and nonzero uniqueness;
- root/parent mapping;
- name/Transform preservation;
- Mesh GUID preservation;
- allowlist acceptance and unknown component rejection;
- fail-closed capture with no partial model.

### G4-2

- Write -> Read model equivalence;
- canonical determinism and Digest stability;
- all hierarchy negative cases;
- Transform type/range/finite negatives;
- each component positive/negative schema case;
- unknown top-level/entity/component fields;
- duplicate component type;
- unsupported formatVersion;
- truncated/corrupt JSON;
- expected-GUID mismatch.

### G4-3

- first Prefab Publish;
- same GUID/same bytes idempotency;
- same GUID/changed bytes new revision/path;
- GUID stability;
- manifest current and cold-start read;
- Prefab Reader from committed bytes;
- direct Mesh dependency query with missing dependency;
- all eight `RevisionFaultPoint` failures with old current intact;
- missing/corrupt Prefab bytes;
- orphan candidate file visibility/behavior;
- `EV_ASSET_IMPORTED type=4` only on success.

### G4-4

- real Hierarchy Create Prefab Debug/Release UIA;
- Asset Browser type/filter/GUID/path/status;
- failure leaves Scene unchanged and shows error;
- no Instantiate/Override behavior.

### G4-5

- G1-G3 regression;
- full Native Debug/Release;
- `test_bridge`, `test_content`;
- UIA Debug/Release;
- historical PICK-01 and SDK SKIP kept separate.

## 13. Exit criteria

| ID | Requirement | Evidence |
|---|---|---|
| G4-01 | Prefab AssetType 4 registered | Native + Manifest tests |
| G4-02 | Prefab Model + Local IDs | Native model tests |
| G4-03 | Scene subtree capture | ID/name/parent tests |
| G4-04 | Strict component schemas | Positive/negative tests |
| G4-05 | Reader/Writer round-trip | Deterministic round-trip |
| G4-06 | Stable Digest | Repeated-byte digest tests |
| G4-07 | RevisionStore Publish | Eight fault points |
| G4-08 | Cold-start committed read | Restart/resolver test |
| G4-09 | Asset Browser type/status | Managed/UIA |
| G4-10 | Create Prefab UI | Debug/Release UIA |
| G4-11 | G1-G3 regression | Full inventory |
| G4-12 | Closure docs | Implementation/Test/Closure |

Final status must not claim editor workflow verification if G4-4 UIA is absent.

## 14. Frozen non-goals

G4 does not implement:

- Prefab Instance or Anchor;
- Instance metadata in Scene;
- Local ID -> Scene Entity ID instantiation mapping;
- Overrides, Apply, Revert, recovery records;
- source update/reconciliation/conflict resolution;
- Nested Prefab or Variant;
- Prefab-specific pixel rendering.

## 15. Git and gate state

G4-A adds only this contract document. No production code, tests, CMake, or ABI
files are changed. G4-1 must not start until this contract is accepted.
