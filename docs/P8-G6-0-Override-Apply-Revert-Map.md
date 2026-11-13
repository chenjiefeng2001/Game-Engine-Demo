# P8 G6-0 - Override / Apply / Revert Integration Map

**Status**: READ-ONLY INVESTIGATION COMPLETE - PENDING G6-A
**Contract**: `docs/P8-GateA-Scene-Composition-Contract.md` C8/C9/C10
**Baseline**: G5 CORE ACCEPTED; ABI v1.16 / 63 ops
**Source changes in G6-0**: NONE
**Test runs in G6-0**: NONE

## 1. Current state

### Prefab Instance metadata

`Engine::PrefabInstanceMetadata` currently contains only:

- `prefabGuid`;
- `sourceRevision` (project revision at instantiate);
- `localToScene`.

It has no Override records and no synchronization/conflict status. Scene v2
persists this metadata on the instance root and validates its Local ID mapping.

### Inspector write path

```
Avalonia Inspector
  -> MainViewModel.ApplyInspectorPosition / ApplyCamera / ApplyRigidBody
  -> EditorHostService
  -> EngineHost Dispatch
  -> EditorSession::SetEntityTransformById / SetComponentProperty
  -> GameObject Transform / Component
```

- Transform uses `SetEntityTransformById`, with an automatic Scene transaction.
- Camera/RigidBody apply through `SetComponentProperty`, with a ViewModel
  `BeginSceneEdit`/`CommitSceneEdit` transaction.
- Mesh GUID is assigned by `Add Mesh to Scene`, not by an editable Mesh Inspector
  field.
- Component property writes have no Prefab-instance lookup and no Override
  recording.

### Undo / persistence

P7-B Snapshot History captures the complete Scene JSON, including G5 instance
metadata. G6 Override data stored in `PrefabInstanceMetadata` and Scene v2 will
participate in Undo/Redo only after serializer support is added.

### Asset update path

G4-3 has `CreatePrefabFromEntity` (always a new Prefab GUID). Material editing
shows the existing same-GUID update pattern:

- read current committed bytes;
- validate a new payload;
- content-address a new file;
- rebind the same GUID in a candidate registry;
- `RevisionStore::Publish(candidateManifest, committedScene)`.

There is no Prefab same-GUID update service, no override-to-candidate builder,
and no Apply Recovery Record.

## 2. Override identity and field paths

C8 identity is frozen:

```text
Prefab Local ID + Component Type + Field Path
```

No runtime GameObject ID, root index, entity name, array position, or temporary
component pointer may become identity.

### Transform

The runtime Transform is built-in, not a Component. G6-A must freeze a synthetic
component/path representation, recommended:

```text
Component Type = "Transform"
Field Path = "position.x" | "position.y" | "position.z"
           | "rotation.x" | ... | "scale.z"
```

Values are Local Transform values for the entity at that Local ID. World
transform must never be the override value.

### Component fields

Current declarative fields available through `ComponentPropertyDesc`:

| Type | Existing editable properties | Persisted by Prefab? |
|---|---|---|
| Mesh | `meshGuid`, `visible` | `meshGuid` only |
| Camera | `enabled`, `zoom`, `viewport`, `bounds` | yes |
| Collider | `enabled`, `shape`, `radius`, `halfX`, `halfY`, `isSensor`, `category`, `mask` | yes |
| RigidBody | `motionType`, `linearDamping`, `angularDamping` | yes |
| Skinning | no `GetPropertyDesc` editable contract | serialized bulk data only |

Mesh `visible` is editable through the generic property channel but is not
persisted by `MeshComponent::Serialize`; it cannot be an Apply override without a
component-contract decision.

Skinning has serialized nested data but no field-path/property-descriptor
contract. G6-A must either exclude Skinning overrides initially or freeze an
explicit nested field-path allowlist. It must not serialize arbitrary JSON paths
and call that an Override.

Material/Texture references are currently asset-level, not per-instance
Component fields. `MeshComponent` exposes only Mesh GUID. G6-A must state whether
the first Apply scope includes Material GUID references; it must not silently
claim them from the current Mesh contract.

## 3. Instance lookup gap

There is no helper to:

- find the owning Prefab Instance Anchor for a Scene Entity ID;
- resolve its Local ID from `localToScene`;
- determine whether a transform/component edit belongs to an instance;
- record an Override at the correct anchor.

G6-1 must add this lookup in the Scene/EditorSession boundary. It must traverse
the Scene parent chain, not use runtime GameObject IDs.

## 4. Write tracking gap

`SetEntityTransformById` and `SetComponentProperty` currently mutate directly.
G6 needs one semantic seam that:

1. resolves the owning instance/local ID;
2. reads the Prefab base value for that local ID/field;
3. compares the new value to the base value;
4. records or clears an Override only when the value differs;
5. leaves ordinary Scene edits unchanged outside instances;
6. fails closed for unsupported component/field paths.

The existing Snapshot History remains the only Scene undo stack. Override
recording is Scene metadata, not a second history mechanism.

## 5. Revert gap

C9 requires Revert to:

1. read the current committed Prefab Source;
2. restore the selected field from the corresponding Local ID;
3. remove that Override record;
4. update runtime/projection;
5. commit one P7-B Snapshot transaction;
6. not modify the Prefab asset revision.

No Revert API or UI exists. G6-A must freeze:

- field-level vs component-level vs all-overrides Revert;
- behavior when source bytes are missing/corrupt;
- whether reverting a non-overridden field is a no-op or error;
- how the selected Scene Entity maps to an instance Local ID.

## 6. Apply gap and recovery

C9 Apply is a cross-domain, recoverable operation:

```text
read Source + Overrides
  -> build/validate Prefab Candidate
  -> persist Recovery Record
  -> publish new Prefab Revision (same Prefab GUID)
  -> update Instance sourceRevision/Overrides
  -> mark Recovery Record complete
  -> remove record after durable completion
```

Current blockers:

- no same-GUID Prefab update API;
- no override-to-Prefab-model builder;
- no Apply Recovery Record schema/location;
- no startup Apply recovery hook;
- no source-revision identity beyond the G5 project revision;
- no Apply-specific fault injection;
- no decision for failed/unknown publish state.

G6-A must freeze a minimally reliable protocol. If reliable recovery persistence
cannot be achieved, Apply must be explicitly deferred; ordinary Scene Undo must
not substitute for cross-domain recovery.

## 7. Source revision semantics

G5 stores the project revision at instantiate time. It does not store a Prefab
Asset content digest or dedicated asset revision. If the Prefab asset was
published in the same project revision as unrelated Scene changes, the current
field cannot by itself identify exact source bytes.

G6-A must define one of:

- store the Prefab content digest/path identity alongside source revision;
- use a dedicated Prefab revision identity;
- explicitly constrain G6/G7 to a source revision resolvable from committed
  manifest history.

G7 must locate the instance's original source, not assume the latest Prefab file
is the source.

## 8. Required G6-A decisions

1. Override record schema and Scene v2 persistence.
2. Transform synthetic field paths.
3. Component field-path allowlist; Mesh `visible` and Skinning policy.
4. Material GUID override policy; fail-closed for unsupported fields.
5. Instance/local-ID lookup API.
6. Override recording seam for Inspector writes.
7. Revert granularity and failure behavior.
8. Apply candidate construction and same-GUID Publish.
9. Recovery Record schema/location and startup behavior.
10. Source revision/digest identity.
11. ABI/UI for override indication, Revert, and Apply.
12. G6 fault matrix and targeted regression set.

## 9. Existing evidence to reuse

- C8/C9/C10 contract text.
- G5 `PrefabInstanceTest` for anchoring/mapping/cold restart.
- G4-2 canonical Prefab Writer/Reader/Digest.
- G4-3 material same-GUID rebind and RevisionStore fault tests.
- P7-B Snapshot History and UIA Inspector tests.
- `test_revision_commit` and `test_m1f_fault_matrix` recovery precedents.

No G6 source implementation may start until G6-A freezes the above decisions.
