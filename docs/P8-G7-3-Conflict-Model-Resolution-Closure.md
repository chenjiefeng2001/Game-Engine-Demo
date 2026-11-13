# P8 G7-3 - Conflict Model & Resolution Engine Closure

**Status**: SUBMITTED - PENDING ACCEPTANCE
**Gate**: P8-G7-3 Conflict Model & Resolution Engine
**Baseline**: G7-2 CLOSED / G7-A FROZEN
**Scope**: conflict persistence and resolution behavior; no ABI/UI changes, no G7-4 executor.

## 1. Persistent conflict model

`PrefabReconciliationState` stores the latest source identity and the unresolved
conflict records:

```json
{
  "latestSourceDigest": "<16-hex>",
  "latestSourcePath": "assets/prefabs/a.<digest>.prefab.json",
  "conflicts": [
    {
      "id": "field:1:Transform:position.x",
      "type": "field",
      "localId": 1,
      "component": "Transform",
      "field": "position.x",
      "base": 1.0,
      "latest": 1.5,
      "local": 2.0,
      "detail": "both source and local changed"
    }
  ]
}
```

The state is optional in `PrefabInstanceMetadata` and
`SerializedPrefabInstance`. It is serialized into
`prefabInstance.reconciliation`, loaded with strict validation, and requires
`syncState=conflict` when present. Legacy instances, malformed records, unknown
conflict types and unsupported field paths fail closed.

Conflict type strings:

- `field`
- `entity_missing`
- `entity_delete`
- `entity_name`
- `entity_parent`
- `component_set`
- `unsupported_local_edit`
- `unsupported_component_change`

## 2. Persistence API

`EditorSession::PersistPrefabReconciliationConflicts(root, plan)`:

- validates the plan and source identity;
- writes latest digest/path and deterministic conflict records;
- sets `syncState=conflict`;
- clears the reconciliation object when the plan has no conflicts and
  recomputes `clean`/`overrides` from retained overrides.

`EditorSession::PreviewPrefabReconciliation(root, plan)` is pure and does not
persist or mutate Scene/source; `PersistPrefabReconciliationConflicts` is the
explicit persistence boundary.

## 3. Resolution behavior

`EditorSession::ResolvePrefabReconciliationConflict(root, id, action, value)`
accepts `take_source`, `keep_local` and `manual_edit`.

Field conflicts:

- `take_source`: write Latest value to Scene, remove matching Override, remove
  conflict;
- `keep_local`: retain Local value, normalize/add Override against Latest
  source, remove conflict;
- `manual_edit`: validate and write supplied value; if it equals Latest remove
  the Override, otherwise normalize it against Latest.

Structural conflicts:

- `entity_name` / `entity_parent`: Take Source and Manual Edit write the target
  value; Keep Local leaves Scene unchanged.
- `entity_delete`: Take Source deletes only the mapped entity when it has no
  local children, updates the mapping and removes its Overrides; Keep Local retains
  the entity.
- `component_set`: Take Source adds/applies or removes the component and clears
  its Overrides; Keep Local retains the local component.
- `unsupported_*`: only Keep Local is accepted.
- `entity_missing`: deferred to G7-4 because it requires Scene entity creation.

Every successful resolution:

- removes one stored conflict;
- if conflicts remain, keeps `syncState=conflict`;
- if none remain, clears the reconciliation object and sets `syncState`
  to `overrides`/`clean`;
- revalidates Local mapping/subtree and Scene composition;
- commits metadata and Scene changes in one P7-B transaction;
- leaves Source Identity update and automatic G7-4 merge execution to G7-4.

`keep_local` overrides are normalized against the Latest source model, so G7-4
can commit Latest as the new Base without an extra baseValue field.

## 4. No-mutation / unresolved boundary

- Preview does not persist conflicts.
- Persist stores conflict state only; it does not execute G7-4 changes.
- Resolution changes only the selected entity/component/field and conflict
  metadata; it does not publish a Prefab revision or update Source Identity.
- Unresolved conflicts remain `syncState=conflict` and cannot be consumed by a
  future G7-4 executor.

## 5. Save, cold start and history

Scene serialization includes reconciliation state, so Save/reopen preserves
conflict records and overrides. Resolution runs through `BeginSceneEdit` /
`CommitSceneEdit`, so Undo/Redo restore both Scene values and conflict metadata.

Focused bridge tests cover:

- `ConflictStatePersistsColdStartAndResolutionUndoRedo`
- `KeepLocalConflictRebasesOverrideAgainstLatest`
- `ManualEditConflictResolvesToExplicitValue`

Focused content test covers conflict JSON round-trip and unknown-type rejection.

## 6. Evidence

- Native Debug/Release: `1297 executed / 1295 PASS`; 1 known PICK-01 and 1 SDK
  skip.
- `test_content`: `183 tests / 182 PASS + 1 skip`.
- `test_bridge`: `150/150`.
- Managed static: `385`; CTest entries: `22`.
- UIA not rerun; no UI changes.
- ABI unchanged: v1.19 / 66 ops.

Existing PICK-01 and SDK skip are historical registrations.

## 7. Scope limits / next gate

G7-3 does not allocate new Scene entities, apply new Latest subtree entities,
publish Prefab changes, update Source Identity, or expose ABI/UI conflict
operations. Those remain G7-4 and G7-5. Structural `entity_missing` resolution
is intentionally deferred to the executor.

**Next**: P8-G7-4 Reconciliation Executor.
