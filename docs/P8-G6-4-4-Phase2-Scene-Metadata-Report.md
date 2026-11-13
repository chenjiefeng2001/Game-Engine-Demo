# P8 G6-4-4 - Phase 2 Scene Metadata Publish Report

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Scope**: Phase 2 of Apply. No startup recovery, Apply ABI, or UI.

## 1. Delivered

`EditorSession::PublishPrefabApplyPhase2(rootEntityId)`:

1. requires a `PrefabPublished` Recovery Record;
2. verifies the actual current revision equals `record.phase1Revision`;
3. verifies the live registry resolves the Prefab GUID to `record.newPath`;
4. reads the Phase 1 candidate bytes, recomputes its digest, and strictly reads
   the Prefab with the expected GUID;
5. reads committed Scene bytes (not unsaved live Scene edits);
6. finds the unique Instance anchor and verifies its old source
   revision/digest/path;
7. updates only that Instance's `sourceRevision`, `sourceDigest`,
   `sourcePath`, `overrides`, and `syncState`;
8. keeps Scene Entity IDs, hierarchy, and other Instance metadata unchanged;
9. publishes candidate Manifest + updated Scene through RevisionStore;
10. records `ScenePublished`/`phase2Revision`, updates live metadata, verifies
    dirty state, and removes the Recovery Record only after successful
    completion.

Failure behavior:

- safe Phase 2 failure leaves Phase 1 revision/record/overrides intact;
- recoveryRequired keeps the record and marks the session RecoveryRequired;
- record-update or live-metadata failure after Phase 2 Publish marks
  RecoveryRequired, never reports Apply success.

`sourceRevision` remains the Phase 1 revision; Phase 2 revision is only the
Scene commit revision.

## 2. Tests

`PrefabInstanceTest.Phase2UpdatesInstanceMetadataAndClearsOverrides`:

- Phase 1 then Phase 2; Phase 2 revision advances;
- target Instance source revision/digest/path updated; overrides cleared;
- other simultaneous Instance keeps old source identity and empty overrides;
- Recovery Record removed; Save/cold reopen preserves updated metadata.

`PrefabInstanceTest.Phase2FailureKeepsPhase1RecordAndOverrides`:

- candidate Manifest write fault;
- Phase 1 revision current remains;
- Recovery Record remains `PrefabPublished`;
- live source/overrides not cleared.

Focused/full regression:

```text
Native Debug/Release : 1279 executed, 1277 PASS, 1 PICK-01, 1 SDK skip
test_content         : 176 tests, 175 PASS + 1 SDK skip
test_bridge          : 139/139 Debug and Release
UIA                  : not rerun; no UI/runtime path change
Managed static       : 385
```

## 3. Known limits

- Startup Recovery remains G6-4-5.
- Apply ABI/Inspector UI remains G6-4-6.
- Phase 2 failure after Publish but before record/live update is marked
  RecoveryRequired, not auto-recovered yet.
- No Override/source reconciliation (G7).
- No claim that Apply is ordinary Scene Undo.

## 4. Git

Uncommitted; no commit/push/reset/clean/stash.
