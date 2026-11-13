# P8 G6-4-3 - Phase 1 Prefab Revision Publish Report

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Scope**: Durable Prepared record -> staged candidate -> Phase 1 RevisionStore
Publish -> PrefabPublished record. No Phase 2, startup recovery, ABI, or UI.

## 1. Delivered

`EditorSession::PublishPrefabApplyPhase1`:

1. builds and validates the G6-4-1 Apply candidate;
2. rejects a second Apply when a Recovery Record already exists;
3. derives the new content-addressed Prefab path from the current source path;
4. stages candidate bytes without overwriting old Prefab bytes;
5. writes a `Prepared` Recovery Record before Publish;
6. rebinds the same Prefab GUID in a candidate Manifest;
7. reads committed Scene bytes and calls `RevisionStore::Publish(candidate
   Manifest, old committed Scene)`;
8. updates the Recovery Record to `PrefabPublished`, stores the actual Phase 1
   revision as `phase1Revision`/`newSourceRevision`, and updates the new metadata
   source revision;
9. leaves the live Scene metadata and overrides unchanged for Phase 2.

Failure handling:

- Publish failure with old current intact removes the Recovery Record and leaves
  old current/revision/Scene/Prefab bytes unchanged;
- Publish uncertainty (`recoveryRequired`) keeps the record, sets it to
  `RecoveryRequired`, and blocks further writes;
- record update failure after a successful Publish keeps the record and marks
  `RecoveryRequired` rather than claiming Apply success;
- the staged candidate file may remain as an allowed orphan.

The intermediate current after success is intentionally:

```text
new Prefab in Manifest + old committed Scene metadata
```

The old revision directory and old Prefab file remain on disk.

## 2. Tests

`PrefabInstanceTest.Phase1PublishesPrefabAndLeavesSceneMetadataOld`:

- candidate file/digest and same GUID;
- old and new Prefab files exist;
- Phase 1 revision advances; old revision immutable;
- live Scene metadata still points old source and retains overrides;
- Recovery Record state/phase revision/path;
- duplicate Apply is rejected.

`PrefabInstanceTest.Phase1FaultPointsKeepOldCurrentOrExposeRecovery`:

- applicable existing-baseline fault timings 0,1,2,5,6,7;
- safe failure keeps current bytes and revision, removes record;
- readback fault keeps a Recovery Record in `RecoveryRequired` and marks the
  session recovery-required;
- `BeforeCandidateManifestWrite`, `AfterManifestWrite`, `AfterSceneWrite`,
  `AfterMarkerWrite`, `BeforeCurrentReplace`, and
  `AfterCurrentReplaceBeforeReadback` covered.
- timings 3/4 are marker-write paths and do not fire after a normal G4-created
  baseline because the versioned marker already exists; G4-3 already covers
  first-publish marker injection. No claim is made that they fired in this test.

Focused/full regression:

```text
Native Debug/Release : 1277 executed, 1275 PASS, 1 PICK-01, 1 SDK skip
test_content         : 176 tests, 175 PASS + 1 SDK skip
test_bridge          : 137/137 Debug and Release
UIA                  : not rerun (no UI/runtime path change; previous 14/14)
Managed static       : 385
```

## 3. Known limits

- No Phase 2 Scene metadata Publish.
- No startup Recovery.
- No Apply ABI/UI.
- Recovery record update failure after Publish is marked RecoveryRequired, but
  automatic recovery is G6-4-5.
- `sourceRevision` is Phase 1's project revision; Phase 2 must not rederive it.
- Other Instances of the same Prefab GUID are not touched by Phase 1.

## 4. Git

Uncommitted; no commit/push/reset/clean/stash.
