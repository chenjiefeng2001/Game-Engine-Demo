# P8 G7-A - Source Reconciliation Contract

**Status**: ACCEPTED - CONTRACT FROZEN
**Based on**: `docs/P8-G7-0-Source-Reconciliation-Map.md`
**Baseline**: ABI v1.19 / 66 ops
**No production code is changed by G7-A.**

## 1. Three-way model

```text
Base   = exact instance source (sourceDigest + sourcePath)
Latest = current manifest/registry source for the same prefabGuid
Local  = current Scene Instance state + persisted overrides
```

Base is never inferred from latest or sourceRevision. Source lookup:

- Base: `sourcePath` + `sourceDigest` + Prefab GUID; missing/corrupt/digest
  mismatch => SourceUnavailable.
- Latest: current registry path for the GUID; missing/corrupt/GUID mismatch =>
  SourceUnavailable.
- Local: Scene anchor, mappings, entities, overrides, and actual values; mapping
  must equal the actual instance subtree.

## 2. Status model

Persist only G6-compatible `syncState` values: `clean`, `overrides`,
`apply_pending`, `recovery_required`, `conflict`.

Derived, not persisted:

- `out_of_date`: instance source digest/path differ from latest.
- `source_unavailable`: Base or Latest cannot be read/validated.
- `conflict_count`: unresolved persisted conflict records.

`out_of_date`/`source_unavailable` must be recomputed on load; they cannot be
used as a substitute for source validation.

## 3. Field-level merge

Only the G6-A/G6-1 field paths may merge. Comparison is per scalar field.

| Base vs Latest | Base vs Local | Result |
|---|---|---|
| same | same | keep Base/Local |
| changed | same | take Latest |
| same | changed | keep Local; preserve Override |
| changed | changed, Local==Latest | take Latest; remove redundant Override |
| changed | changed, different | Conflict (field) |

Transform comparison uses Local quaternion components, not Euler/World values.
Mesh/Camera/Collider/RigidBody use G6 schemas. Skinning, Mesh `visible`,
Material/Texture and unknown fields remain unsupported and are structural
conflicts, never silently adopted or discarded.

### Override rebasing

After successful reconciliation, every retained Override's base becomes Latest.
If Local value equals Latest value, remove the Override. No extra `baseValue`
field is stored; Base is derived from the new Source Identity.

## 4. Structural merge

Structure identities use Scene anchor + Prefab Local ID.

| Situation | Result |
|---|---|
| Local ID exists in all three, fields unchanged locally | update from Latest |
| Local ID exists in Latest, missing in Local | Entity Missing Conflict |
| Local ID exists in Base/Local, absent in Latest | Delete Conflict if Local has any override/structural edit; otherwise delete only that Instance entity |
| New Latest Local ID | allocate new Scene Entity ID, map it, create entity, attach allowlisted components, set parent in second pass |
| Source Parent changed, Local parent unchanged | adopt Latest parent |
| Source and Local parent both changed, same | adopt; else Parent Conflict |
| Source name changed, Local name unchanged | adopt Latest name |
| Source and Local name changed, same | adopt; else Name Conflict |
| Component added in Latest, Local unchanged | add allowlisted component |
| Component removed in Latest, Local has edits | Component Conflict |
| Component set differs from Base/Local | Component/Unsupported Edit Conflict |
| Extra Scene subtree entity not in mapping | Unsupported Local Edit Conflict; never delete |

The anchor and existing Scene Entity IDs are preserved wherever the Local ID
still exists. Reconciliation never rebuilds the entire Instance, never reuses
another entity's Scene ID, and never deletes a Scene Entity outside the instance
mapping.

Structural conflicts are separate from field conflicts and require a structural
resolution action; field `Keep Local`/`Take Source` must not be applied to them.

## 5. Conflict persistence

Add an optional `prefabInstance.reconciliation` object only when structural or
field conflicts remain:

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
      "local": 2.0
    }
  ]
}
```

Structural conflicts use exact keys `id`, `type`, `localId`, `component?`,
`field?`, `detail`, and stored base/latest/local snapshots sufficient to
resolve after restart. IDs are deterministic identity strings, not random or
pointer values. Unknown conflict types/fields fail closed.

Unresolved conflict state is saved with `syncState=conflict`, old Source
Identity, old Overrides, and no partial entity changes. No Reconciliation
transaction is committed until all conflicts have a resolution.

## 6. Resolution and transaction

Resolution actions:

- `take_source`: adopt Latest value, remove matching Override/conflict.
- `keep_local`: retain Local value, rebase Override against Latest, remove
  conflict.
- Structural: explicit Keep Local/Delete/Source action per conflict type; unknown
  structural conflict cannot be resolved by a field action.
- Manual Edit: normal Inspector write after conflict resolution; must re-run
  validation and must not delete conflict state until the value matches the
  selected resolution policy.

Successful reconciliation is one P7-B Snapshot transaction:

- no Prefab Asset publish/revision change;
- preserve anchor and valid Scene IDs;
- update Source Identity only on commit success;
- `sourceRevision` = current committed project revision at reconciliation commit
  (advisory label; digest/path are authoritative);
- `sourceDigest/sourcePath` = Latest Source;
- `syncState=clean`, conflicts cleared, retained Overrides rebased;
- failure leaves old Scene, overrides, Source Identity and conflicts unchanged;
- Undo/Redo restore complete pre/post state.

Apply (G6) and Reconciliation (G7) are separate operations. Reconciliation does
not modify Prefab Asset revisions or Recovery Records.

## 7. ABI / Managed / UI (frozen)

Additive ABI v1.20 / 70 ops:

| ID | Operation | Shape |
|---|---|---|
| 66 | `GET_PREFAB_RECONCILIATION_STATUS` | Fetch, root entity ID string, JSON |
| 67 | `PREVIEW_PREFAB_RECONCILIATION` | Fetch, root entity ID string, JSON diff; no mutation |
| 68 | `APPLY_PREFAB_RECONCILIATION` | Dispatch, root string; no conflicts only |
| 69 | `RESOLVE_RECONCILIATION_CONFLICT` | Dispatch, `root;conflictId;action`, optional value JSON |
| 70 | `ENGINE_HOST_OP_COUNT` | count |

Existing operations 0-65 remain unchanged. UI must show source revision,
out-of-date/source-unavailable/conflict states, preview, Apply, and conflict
resolution. UI must not bypass Native validation or auto-update all Instances.

## 8. Per-gate acceptance and failure thresholds

| Gate | Required inputs | Required outputs | Exit evidence | Failure threshold |
|---|---|---|---|---|
| G7-0 | accepted G6 baseline + current code | map | source identity/mapping/structural blockers identified | any unresolved Base/Latest semantics or missing transaction path |
| G7-A | G7-0 map | this contract | decisions frozen | any undefined structural/conflict/ABI rule |
| G7-1 | contract + project + instance | Base/Latest/Local source snapshots | exact source reads, GUID/digest/path validation | latest substitution, missing/corrupt source accepted, mapping mismatch |
| G7-2 | three validated sources | deterministic reconciliation diff | stable output for identical inputs; no mutation | nondeterministic output, source/Scene mutation, missing field category |
| G7-3 | diff result | conflict records + resolution behavior | stable conflict IDs, persistence/restart, all types handled | unknown conflict treated clean, unresolved conflict committed, lost state |
| G7-4 | resolved plan | atomic Scene reconciliation | anchor/ID preservation, add/delete/parent rules, one transaction | partial entity state, rebuild of whole instance, unrelated deletion, Prefab publish |
| G7-5 | executor + ABI contract | Managed service/UI workflow | status/preview/apply/conflict feedback | ABI renumbering, UI bypass, no conflict feedback |
| G7-6 | successful executor | Save/load/Undo/Redo state | mapping/source/overrides recover after cold restart | lost mapping/conflict/override, non-idempotent state, cross-instance leakage |
| G7-7 | all prior gates | integrated report/closure | focused regression, evidence, docs | new stable failure, unverified exit condition, count/ABI mismatch |

## 9. Explicit out of scope

Nested Prefab, Prefab Variant, multiplayer/merge, streaming/World Partition,
ECS redesign, Material graph, Material/Texture Override, Skinning Override, and
automatic all-instance update. G7 does not modify G6 Apply/Recovery protocols.
