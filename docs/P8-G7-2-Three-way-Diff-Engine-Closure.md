# P8 G7-2 - Three-way Diff Engine Closure

**Status**: SUBMITTED - PENDING ACCEPTANCE
**Gate**: P8-G7-2 Three-way Diff Engine
**Baseline**: G7-1 CLOSED / G7-A FROZEN
**Scope**: deterministic native diff only; no Scene, Prefab Source, RevisionStore, ABI or UI mutation.

## 1. Delivered API

`engine/include/Engine/Core/Content/PrefabReconciliation.h`

`BuildPrefabReconciliation(base, latest, local)` consumes validated
`PrefabSourceSnapshot` and `PrefabLocalInstanceSnapshot` inputs and returns a
`PrefabReconciliationPlan`:

- `outOfDate` from Base/Latest path or digest difference;
- `fields[]` with `TakeLatest`, `KeepLocal` or `Conflict` decisions;
- `structural[]` add/delete entity, name, parent and component actions;
- `overrides[]` with source rebasing decision and new Base value;
- `conflicts[]` with stable IDs and Base/Latest/Local snapshots;
- `validationErrors[]` for source identity/duplicate-ID/mapping failures.

The returned plan is deterministic. Inputs are read-only.

## 2. Merge rules

- G6-A allowlist only: Transform scalar/quaternion fields, Mesh `meshGuid`,
  Camera, Collider and RigidBody schemas.
- Same Base/Latest/Local value: no change.
- Base changed, Local unchanged: `TakeLatest`.
- Base unchanged, Local changed: `KeepLocal`; retained Override base is Latest.
- Base and Local changed to the same value: `TakeLatest`; redundant Override is
  removable.
- Base and Local changed differently: field Conflict.
- Transform compares Local quaternion components, not Euler/World values.
- Unknown supported-component fields and unsupported component types fail closed
  as structural conflicts; they are never silently adopted or discarded.
- Structural handling covers new Latest entities, missing Local entities,
  delete conflicts when Local has overrides/structural edits, Parent/Name
  changes, and component add/remove/conflict cases.
- New Latest entities carry their Local ID, parent, name, Transform and
  components into the plan; Scene Entity allocation remains G7-4 work.

## 3. Stable conflict identity

- Field: `field:<localId>:<component>:<field>`
- Name: `name:<localId>`
- Parent: `parent:<localId>`
- Entity missing: `entity:missing:<localId>`
- Entity delete: `entity:delete:<localId>`
- Local-only entity: `entity:extra:<localId>`
- Component: `component:<localId>:<component>`
- Unsupported local edit: `unsupported:<localId>:<component>[:<field>]`

Conflict IDs use Local ID and path identity only; no pointers, random values or
iteration addresses.

## 4. Override rebasing

`RebaseOverrides` emits one record per persisted Override:

- source Base unchanged: `KeepLocal`, `latestBase` equals Base;
- source changed and Local already equals Latest or Local equals old Base:
  `TakeLatest`, Override removal is indicated;
- source and Local diverge: `Conflict` with the field conflict ID;
- missing/unsupported field or source entity/component: fail closed.

G7-4 will apply `latestBase`, retain/remove Overrides, and commit the new Source
Identity in one transaction. G7-2 does not persist conflict records.

## 5. No-mutation boundary

- No Scene entity is created, deleted, reparented or renamed.
- No component or Transform is written.
- No Prefab Source, Prefab Model, digest, revision or Override storage is
  modified.
- No ABI, Managed service or Avalonia UI is changed.

## 6. Focused tests

`tests/test_content/PrefabReconciliationTest.cpp`:

- `FieldAutoMergeKeepLocalAndConflict`
- `EntityNameParentAndComponentStructure`
- `DeterministicAndNoMutation`
- `OverrideRebaseKeepsLocalAndRemovesMatchingLatest`
- `ComponentSetAndMissingEntityFailClosed`
- `EntityDeletionAndExtraLocalFailClosed`

Coverage includes take-latest/keep-local/conflicting field merges, name and
parent conflicts, component additions, source deletions, local override
conflicts, missing entities, redundant override removal, deterministic repeated
output and input immutability.

## 7. Evidence

- Native Debug/Release: `1293 executed / 1291 PASS`; 1 known PICK-01 and 1 SDK
  skip.
- `test_content`: `182 tests / 181 PASS + 1 skip`.
- `test_bridge`: `147/147` (unchanged; G7-2 adds no bridge surface).
- `test_content` focused reconciliation: `6/6`.
- Managed static: `385`; CTest entries: `22`.
- UIA not rerun; no UI changes.
- ABI unchanged: v1.19 / 66 ops.

Existing PICK-01 and SDK skip are historical registrations.

## 8. Scope limits / next gate

G7-2 produces the plan and diagnostics only. It does not persist conflicts,
allocate Scene Entity IDs, apply structural changes, publish sources, or expose
preview/apply through ABI/UI. Those remain G7-3, G7-4 and G7-5.

**Next**: P8-G7-3 Conflict Records & Resolution.
