# P8 G4-0 - Prefab Asset Integration Map

**Status**: ACCEPTED - READ-ONLY COMPLETE
**Date**: 2026-11-10
**Baseline**: G3 CORE ACCEPTED; ABI v1.14 / 60 ops
**Scope**: Map the existing asset, revision, Scene, component, ABI, and UI paths
for a committed Prefab Asset. No production code was changed for G4-0.

## 1. Executive conclusion

Prefab can reuse the existing Content v2 path without a second asset system:

```text
Live Scene subtree
  -> PrefabModel / Local IDs / component allowlist
  -> canonical Prefab bytes + semantic digest
  -> content-addressed file under assets/
  -> candidate ContentRegistry manifest (AssetType::Prefab)
  -> RevisionStore::Publish(manifest, committed-or-current Scene bytes)
  -> current revision
  -> later reader resolves GUID -> path -> strict Prefab Reader
```

This is the same pattern already used by Material edits and Mesh import:
asset bytes are staged first, the registry is rebound in a candidate, and
`RevisionStore::Publish` commits manifest + Scene. Prefab file bytes are
therefore outside the revision's manifest/scene hash, exactly like existing
Material/Mesh files. G4-A must freeze the Prefab digest/path policy and G4-3
must test the resulting write/Publish failure boundary.

Two contract additions are required before G4-1:

1. **Entity name must be added to the Prefab entity record.** Scene v2 persists
   `name`, but the C7 minimum entity shape does not. Dropping it would be silent
   data loss.
2. **Prefab component payload validation must be schema-strict.** Existing
   component `Deserialize()` methods ignore unknown keys. The Prefab Reader
   cannot rely on them to reject unknown Entity-reference fields.

Neither is a reason to reopen G1-G3. Both are G4-A implementation-level
amendments to the C7 minimum.

## 2. Current Content Asset model

| Fact | Evidence |
|---|---|
| Content asset types are `Texture=0, Script=1, Mesh=2, Material=3` | `engine/include/Engine/Core/Content/ContentAsset.h:33` |
| Type strings are strict; unknown strings reject the manifest | `ContentAsset.h:46`, `ContentAsset.cpp:190` |
| Manifest is `version: 1`, entries are `guid/path/type` | `ContentAsset.cpp:90` |
| GUID/path uniqueness and canonical path validation are strict | `ContentAsset.cpp:165-243` |
| `ContentRegistry::ResolvePath` is GUID -> project-relative path | `ContentAsset.h:83` |
| Asset indices are append-only `m_AssetOrder`, not map order | `EditorSession.h:305`, `EditorSession.cpp:2980` |
| Asset status is currently existence + non-empty size only | `EditorSession.cpp:2529` |

The next additive Content type value is **4**. G4-A should freeze
`AssetType::Prefab = 4` and update `ToString`/`TryFromString` and all explicit
0..3 maps. Existing numeric values must not change.

There is a separate legacy `Engine::AssetType::Prefab` and `.prefab` extension
in `engine/include/Engine/Editor/AssetTypes.h:66,115`. That is not the
Content v2 registry and must not be used as the committed Prefab store.
G4 must add `Content::AssetType::Prefab`, not reuse the legacy enum.

## 3. RevisionStore and asset bytes

`RevisionStore` owns `<root>/.engine-content/current` and immutable
`revisions/<N>/{manifest.json,scene.scene}`. `Publish()` writes a candidate,
strictly reloads the manifest and Scene, then atomically replaces `current`
(`RevisionStore.cpp:321-553`). Its `current` hash covers manifest and Scene
bytes only (`RevisionStore.cpp:475-476`). It does **not** hash arbitrary asset
files.

Material edits establish the reusable asset pattern:

1. validate the edited material payload;
2. write a new content-addressed `.material.json` file;
3. rebind the same Material GUID to the new path in a candidate registry;
4. read the committed Scene;
5. `Publish(candidateManifest, committedScene)`.

Evidence: `EditorSession.cpp:2595-2739` (material), `:1957-1977` (mesh group),
`RevisionStore.cpp:321-553`.

Consequences for Prefab:

- Stage Prefab bytes before `Publish`; a failed publish must leave old
  `current`, old Manifest bytes, and old Scene bytes intact.
- A failed publish can leave an orphan Prefab candidate file. This is accepted
  by the existing Material/Mesh pattern and should be recorded as the same
  class of cleanup debt, not called atomic asset+Scene commit.
- Prefab file bytes are not covered by `current` hashes. G4-A must freeze a
  Prefab semantic digest and whether the file name is content-addressed. G4-3
  must test corruption/missing bytes independently of revision validation.
- Asset creation should publish the **committed Scene bytes**, not live unsaved
  Scene edits, unless G4-A explicitly decides otherwise. This preserves the
  existing Material/Rename boundary and keeps Prefab creation from committing
  unrelated Scene changes.

## 4. Scene v2 facts relevant to Prefab capture

`SceneSerializerV1` version 2:

- `SerializedEntity` has name, position, rotation quaternion, scale, legacy
  sprite/script GUIDs, component list, `entityId`, `parentId`
  (`SceneSerializerV1.h:45-59`).
- IDs and parent IDs are serialized as unsigned JSON integers.
- `CaptureSceneById` captures all entities via `GetAllEntities()`, names,
  Local Transform, and contract components (`SceneSerializerV1.cpp:199-242`).
- Component type is `GetComponentTypeName()`; payload is `Component::Serialize`.
- `InstantiateScene` recreates entities first and applies Parent References in
  a second pass (`SceneSerializerV1.cpp:248-355`).
- v2 validation rejects zero/duplicate IDs, missing/self/cyclic parents
  (`SceneSerializerV1.cpp:164-196`).

Important capture observations:

- `GameObject::ForEachComponent()` iterates an unordered map. The current Scene
  serializer therefore does not guarantee component order. A canonical Prefab
  writer must sort components by a frozen key (recommended: stable type string)
  before hashing/serialization.
- `GetAllEntities()` is deterministic depth-first order over root/child vectors.
  A Prefab writer may use this order, but G4-A should freeze either DFS order or
  ascending Local ID order, not rely on incidental container order.
- Scene v2 saves entity names. C7 does not list `name` in the minimum Prefab
  entity shape. **G4-A must add `name`** (and include it in the digest) or
  explicitly accept name loss. This map recommends adding it.
- Scene v2 does not persist `active`, `layer`, or `layerMask`; no committed
  Scene semantics are lost by omitting them from Prefab. They may be copied as
  runtime-only state only if G4-A chooses.
- Scene v2 has legacy `sprite` and `script` bindings. G3 already rejects those
  for Duplicate. G4 should keep Prefab capture fail-closed for legacy
  Sprite/Script bindings rather than inventing a second binding representation.

## 5. Component allowlist and references

G3's frozen allowlist is the starting point. None of these components currently
contains a Scene Entity ID reference.

| Type | Serialized fields | Asset refs | Entity refs | Notes |
|---|---|---|---|---|
| `Mesh` | `meshGuid` | Mesh GUID | none | `MeshComponent.h:132-150`; color/visible are not serialized |
| `Camera` | `enabled`, `zoom`, `viewport`, `bounds` | none | none | `CameraComponent.cpp:61-78`; viewport/bounds are strings, not entity IDs |
| `Collider` | `enabled`, `shape`, `radius`, `halfX`, `halfY`, `isSensor`, `category`, `mask` | none | none | `ColliderComponent.cpp:123-157` |
| `RigidBody` | `motionType`, `linearDamping`, `angularDamping` | none | none | `RigidBodyComponent.cpp:154-184` |
| `Skinning` | `visible`, skeleton bones + bind matrices, skinned mesh, animation tracks | none | none | `SkinningComponent.cpp:125-190`; bone `parent` is a bone index, not Scene ID |

There are no allowlisted external Entity references today. The Prefab policy
should therefore say:

- internal hierarchy uses `parentLocalId`;
- current allowlisted components have no entity-reference fields;
- a future component with an entity reference must add an explicit Prefab
  mapping rule before being allowlisted;
- legacy Sprite/Script bindings reject Prefab capture.

The strict reader must not merely call the existing `Deserialize()`:
`Mesh`, `Camera`, `Collider`, `RigidBody`, and `Skinning` ignore unknown keys.
G4-A must freeze an exact field schema and reject unexpected fields (or at
minimum reject any field that could represent an unrecognized entity/asset
reference). Duplicate component types must reject; `GameObject` itself supports
only one component per type.

## 6. Asset Browser, status, dependencies, and ABI

- Managed type display is hardcoded `0..3` with `Unknown` fallback
  (`MainViewModel.cs:2204-2211`).
- Type filter is `All/Texture/Script/Mesh/Material`
  (`MainViewModel.cs:269`).
- `EditorSession::GetAssetType` returns `0..3` and currently falls back to 2
  (`EditorSession.cpp:2516-2527`).
- C ABI documentation says `GetAssetType` is `0=Texture 1=Script 2=Mesh
  3=Material` (`capi.h:240`); the value is an int, not a struct field.
- `GetAssetStatus` is type-agnostic existence/non-empty check
  (`EditorSession.cpp:2529-2540`).
- `GetAssetDependencies` has explicit Mesh and Material branches only
  (`EditorSession.cpp:2742-2902`). Prefab needs a direct-dependency branch.
- Current ABI is v1.14 / 60 ops with no Prefab operation.

G4-A/G4-3 must add `Prefab=4` to the Content enum/type strings, Managed display,
filter, C ABI documentation, and any tests asserting the type domain. A new
Create Prefab operation, if required by the Managed UI, should be additive
(op 60 or later) with a new ABI version; it must not renumber existing ops.
`GetAssetType` returning 4 is an additive value, but the ABI record/version
decision belongs to G4-A/G4-3.

## 7. Recommended storage shape (not frozen)

The existing pattern supports:

```text
assets/prefabs/<name>.<semantic-digest8>.prefab.json
```

with:

- ContentRegistry entry: stable Prefab GUID, canonical project-relative path,
  type `Prefab`;
- Prefab file: `formatVersion: 1`, `prefabGuid`, `rootLocalId`, `entities`;
- candidate file staged before revision Publish;
- old Prefab paths retained for old revisions;
- no mutation of old committed bytes.

G4-A must still freeze:

- extension and directory convention (`.prefab.json` recommended to avoid the
  legacy `.prefab` type);
- whether `prefabGuid` participates in the digest/file name;
- read validation and missing-dependency behavior;
- whether a Prefab GUID is created via `Import` or explicit GUID registration.

## 8. Format and identity decisions required for G4-A

The following are not resolved by this read-only map:

1. **Entity name**: add `name` to the Prefab entity schema; required or
   optional; digest participation.
2. **Local ID encoding/allocation**: unsigned JSON integer, DFS assignment,
   stability across recapture/revisions, and whether a Local ID may be reused
   after deletion.
3. **Transform encoding**: position[3], quaternion[4], scale[3] and defaults;
   reject non-finite values.
4. **Root/parent representation**: omit root parent vs explicit `0`; reject
   multiple roots, missing root, unreachable nodes.
5. **Component schema**: exact strict field sets; duplicate type rejection;
   unknown field rejection; unknown component rejection.
6. **Legacy Sprite/Script**: continue fail-closed.
7. **Digest**: canonical ordering, component ordering, inclusion of
   `formatVersion`, names, `prefabGuid`, and source revision-independent bytes.
8. **Publish scene input**: committed Scene bytes, not live unsaved edits.
9. **Dependency policy**: missing Mesh/Material GUIDs reject at read, or remain
   GUID references resolved later by G5.
10. **ABI/UI**: minimal Create Prefab op, AssetType value 4, Asset Browser
    filter/display, and whether ABI version increments.

## 9. Existing test facilities to reuse

- `tests/test_assetimport_commit/TestImportCommit.cpp`: all eight
  `RevisionFaultPoint` timings and old-current integrity.
- `tests/test_bridge/test_revision_commit.cpp`: revision/current, crash/restart,
  stale baseline, candidate validation.
- `tests/test_bridge/test_m1f_fault_matrix.cpp` and `M1FCrashHarness.cpp`:
  crash/restart recovery.
- `tests/test_content/ContentPipelineTest.cpp`,
  `ManifestStrictLoadTest.cpp`, `RevisionStoreTest.cpp`: manifest/GUID/path and
  revision rules.
- `tests/test_content/SceneCompositionTest.cpp`: Scene v2 hierarchy/IDs.
- `tests/EditorAvalonia.IntegrationTests/B6MeshEditorUITests.cs`: real editor,
  AssetBrowser, Save/restart, hierarchy UIA.
- `tools/test_inventory.ps1`: Debug/Release inventory.

No Prefab-specific fixture exists. G4-1/2/3 must add model, Reader/Writer, and
Publish tests; G4-4 must add real editor UI creation tests before claiming
Editor Workflow Verified.

## 10. Baseline and non-goals

Current accepted baseline:

```text
Native Debug/Release: 1224 executed, 1222 PASS, 1 PICK-01, 1 SDK skip
test_bridge: 120/120
test_content: 139 PASS + 1 SDK skip
UIA Debug/Release: 10/10 each
Managed static: 381
ABI: v1.14 / 60 ops
```

G4-0 does not add Prefab Instance, local-ID-to-Scene-ID instantiation, Override,
Apply/Revert, source reconciliation, Nested Prefab, or Variant. No production
source was changed by this map.

## 11. G4-0 exit checklist

| Item | Result |
|---|---|
| AssetType current values and next additive value identified | PASS (4) |
| Content Registry/Manifest registration path identified | PASS |
| RevisionStore candidate/publish/fault points identified | PASS |
| Material asset staging pattern identified | PASS |
| Scene v2 identity/parent/transform/component capture identified | PASS |
| Component allowlist fields and entity-reference audit | PASS (none today) |
| AssetBrowser/status/dependency/ABI exposure identified | PASS |
| Test/fault/cold-start facilities identified | PASS |
| Required C7 amendment (entity name) identified | PASS - pending G4-A |
| Strict component schema requirement identified | PASS - pending G4-A |
| Production code changed by G4-0 | NONE |

**G4-0 conclusion**: READY for G4-A contract freeze after the two C7
implementation-level amendments above are recorded.
