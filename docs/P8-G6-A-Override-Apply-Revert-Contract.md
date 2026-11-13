# P8 G6-A - Prefab Override / Apply / Revert Contract

**Status**: ACCEPTED - FROZEN
**Based on**: `docs/P8-G6-0-Override-Apply-Revert-Map.md` (accepted)
**Parent contract**: `docs/P8-GateA-Scene-Composition-Contract.md` C8/C9/C10
**Implementation**: G6-1/G6-2 delivered; G6-3/G6-4 follow this record.
**ABI staging note**: each implementation gate adds only its frozen operation(s);
the gate report records the exact version/count. The final G6 count is 66.

## 1. Scope

- Per-field Overrides on Prefab Instances.
- Field-level and whole-instance Revert.
- Recoverable two-phase Apply that publishes a Prefab Revision and updates
  committed Scene Instance metadata.
- Inspector tracking and minimal UI.
- Fault and restart verification.

Out of scope: source update/reconciliation (G7), nested Prefabs, arbitrary
component JSON overrides, runtime GameObject IDs, GPU handles, or pointers as
identity, and Apply rollback via ordinary Scene Undo.

## 2. Override identity

```text
Prefab Local ID + Component Type + Field Path
```

Never uses runtime GameObject ID, root index, entity name, array position,
component pointer, or typeid hash. Unknown component/field/path rejects.

## 3. Field allowlist

Transform (synthetic `Transform` component): `position.x/y/z`,
`rotation.x/y/z/w`, `scale.x/y/z`. Values are Local quaternion components.

Mesh: `meshGuid` only. Mesh `visible` is not override-capable.

Camera: `enabled`, `zoom`, `viewport`, `bounds`.

Collider: `enabled`, `shape`, `radius`, `halfX`, `halfY`, `isSensor`,
`category`, `mask`.

RigidBody: `motionType`, `linearDamping`, `angularDamping`.

Skinning, Material/Texture references, arrays, objects, and runtime-derived
fields reject. Values are scalar bool/int/finite-number/string only.

## 4. Metadata

Scene v2 `prefabInstance` extends with:

- `sourceRevision`, `sourceDigest` (16-hex G4-2 digest), `sourcePath`;
- `overrides[]` with `localId/component/field/value`;
- `syncState`: `clean`, `overrides`, `apply_pending`, `recovery_required`,
  `conflict`.

Legacy G5 metadata is marked `legacySourceIdentity`; it must not bind to latest
source automatically. Apply/Revert fail closed until explicit migration.

## 5. Recording and Revert

Inspector writes through `SetEntityTransformById` and `SetComponentProperty`
resolve owning instance/local ID, load exact source, compare base value, then
add/update/delete the matching override. Unsupported fields fail before Scene
mutation. P7-B Snapshot History remains the only Scene undo stack.

Field Revert reads exact source base, restores that field, removes the matching
override, updates sync state, and commits one Snapshot transaction. Instance
Revert restores source names, Local Transforms and component data for mapped
entities, removes all overrides, keeps anchor/Scene IDs, and commits one
transaction. Mapping/component-set mismatch fails closed; it does not silently
delete Scene entities. Revert does not modify the Prefab revision.

## 6. Apply

Phase 0: read exact source and overrides, build and validate candidate, stage
content-addressed bytes and candidate manifest with the same Prefab GUID, write
an Apply Recovery Record.

Phase 1: `RevisionStore::Publish(candidateManifest, old committed Scene)`.
- old current intact: remove record, keep old metadata, leave orphan allowed;
- unknown commit: keep record, mark recovery_required, block writes;
- success: record new project revision R1.

Phase 2: build updated committed Scene metadata (new source revision/digest/path,
empty overrides, clean), publish candidate manifest + updated Scene.
- success: remove record and update live metadata;
- failure: keep record, mark recovery_required; startup must complete or fail
  closed.

Apply is not globally atomic and is not rolled back by Scene Undo after a
successful Prefab Publish.

## 7. Recovery record

Location: `<ProjectRoot>/.engine-content/prefab-apply/<prefabGuid>.<rootSceneEntityId>.json`.
Schema: `engine.prefab-apply`, formatVersion 1, old/new source, instance root,
old Scene hash, candidate Manifest/Scene hashes. Atomic write; one record per
root/GUID; removed only after durable Phase 2 completion. Unknown/corrupt state
=> `RecoveryRequired`, writes blocked.

Startup: current old => remove record/keep old; current candidate + old Scene =>
complete Phase 2; candidate + candidate Scene => cleanup record; neither =>
RecoveryRequired.

## 8. ABI / UI

Frozen operations:

```text
GET_PREFAB_INSTANCE_JSON = 63  (Fetch; source, overrides, sync state)
REVERT_OVERRIDE = 64          (Dispatch; field, or instance via "*")
APPLY_PREFAB_OVERRIDES = 65   (Dispatch; instance root)
```

Final G6 `ENGINE_HOST_OP_COUNT = 66`; existing operations are never renumbered.
UI: instance/override status, field and instance Revert, Apply, recovery/conflict
visibility. No new setter ABI.

## 9. Verification

Fault matrix: candidate/recovery write, Phase 1 candidate/current/readback,
between phases, Phase 2 candidate/current, post-Phase-2 record removal, and
restart at each. Required outcomes: old state intact when Publish not observed;
RecoveryRequired when commit state unknown; no partial overrides; no success
before Phase 2; orphan candidate allowed and unregistered.

Tests: identity/field/value negative cases, tracking/base restore, Revert field/
instance with one transaction, Save/restart, source digest/path validation,
legacy rejection, Apply fault/restart, unsupported fields, and UI markers.
If reliable Apply recovery cannot be delivered, Apply must be explicitly
deferred rather than given weaker semantics.