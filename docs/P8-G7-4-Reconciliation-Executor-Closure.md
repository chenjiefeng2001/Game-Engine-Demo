# P8 G7-4 - Reconciliation Executor Closure

**Status**: SUBMITTED - PENDING ACCEPTANCE
**Gate**: P8-G7-4 Reconciliation Executor
**Baseline**: G7-3 CLOSED / G7-A FROZEN
**Scope**: native executor; no ABI/UI or Prefab asset publish.

## 1. API and transaction boundary

`EditorSession::ExecutePrefabReconciliation(rootEntityId)`:

- re-resolves Base/Latest/Local and rebuilds the deterministic plan;
- rejects any persisted `prefabInstance.reconciliation` state (unresolved
  conflicts cannot enter the executor);
- executes additions, deletions, structural updates and automatic field merges;
- updates Local ID Mapping, Source Identity and Overrides;
- commits Scene + metadata in one P7-B transaction;
- never publishes a Prefab Asset revision or creates an Apply/Recovery record.

Failure before commit restores the complete pre-execution Scene and metadata
snapshot.

## 2. Executed plan actions

- Field `TakeLatest`: apply Latest value to the mapped Scene entity/component;
  redundant Override is removed during normalization.
- Field `KeepLocal`: retain the local value and normalize the Override against
  Latest Source.
- New Latest entity: create the GameObject with source name, Transform and
  allowlisted components; set parent in a second pass; allocate fresh Scene
  Entity IDs; add every Local ID to the mapping.
- New Latest entity whose Local entity was missing (`entity_missing`): created
  from Latest Source when no stale Overrides remain for that Local ID.
- Removed Latest entity: delete mapped Scene subtree in depth-descending order,
  remove mapping entries and remove affected Overrides.
- Name/Parent: adopt Latest automatically when the plan marks them automatic.
- Component add/remove: add deserialized allowlist component or remove the
  component; component-scoped Overrides are removed on removal.
- Existing auto actions are no-ops when already applied, so G7-3 committed
  structural resolutions are not repeated.

Structural conflicts that remain in a fresh post-resolution plan (Name, Parent,
Delete, Component Set, Unsupported) are treated as previously resolved and are
not executed again. Field conflicts must still be provably resolved: Local equals
Latest (Take Source) or a matching Override exists (Keep Local/Manual).

## 3. Source identity and Overrides

On successful execution:

- `sourceRevision` is the current committed project revision at executor commit;
- `sourceDigest/sourcePath` become Latest Source;
- `legacySourceIdentity=false`;
- `reconciliation` is cleared;
- retained Overrides are validated against Latest Source and redundant
  value==Base records are removed;
- `syncState` becomes `overrides` or `clean`.

## 4. Local ID Mapping

- Added entities receive fresh Scene Entity IDs from Scene registration and are
  inserted into `localToScene`.
- Deleted entities and their removed descendants are erased from
  `localToScene`.
- Deleted mapping entries and their Overrides are removed together.
- Final mapping must equal the actual instance subtree; executor fails closed
  otherwise.

## 5. Focused tests

`tests/test_bridge/test_prefab_instance.cpp`:

- `ExecutorAppliesFieldMergeAndCommitsSourceIdentity` (Undo/Redo/source identity)
- `ExecutorAddsLatestEntityAndUpdatesMapping` (add/parent/mapping/Save/reopen)
- `ExecutorDeletesLatestRemovedEntityAndMapping` (delete/mapping)
- `ExecutorPreservesResolvedKeepLocalConflict` (no repeat of G7-3 resolution)
- `ExecutorRejectsUnresolvedConflicts` (persisted conflict boundary)

## 6. Evidence

- Native Debug/Release: `1302 executed / 1300 PASS`; 1 known PICK-01 and 1 SDK
  skip.
- `test_content`: `183 tests / 182 PASS + 1 skip`.
- `test_bridge`: `155/155`.
- Managed static: `385`; CTest entries: `22`.
- UIA not rerun; no UI changes.
- ABI unchanged: v1.19 / 66 ops.

Existing PICK-01 and SDK skip are historical registrations.

## 7. Scope limits / next gate

G7-4 does not expose ABI/UI preview/apply/conflict controls, auto-update all
instances, or publish Prefab assets. Structural `unsupported_*` states remain
Keep-Local by policy and can be surfaced in G7-5. G7-5 owns the ABI v1.20 /
70-op surface and Managed/Avalonia workflow.

**Next**: P8-G7-5 Reconciliation ABI / Managed / UI.