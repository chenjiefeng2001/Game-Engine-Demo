# P8 G6-4-5 - Startup Recovery Report

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Scope**: Startup recovery for Phase 1/Phase 2 Apply records before project
Manifest/Scene are exposed for editing.

## 1. Delivered

Hooked `EditorSession::OpenProjectInternal` after `RevisionStore::Open` and
before Manifest/Scene loading:

```text
RevisionStore.Open
  -> RecoverPrefabApplyOnStartup
  -> load recovered current Manifest/Scene
  -> InstantiateScene / open editing
```

Recovery enumerates `.engine-content/prefab-apply/*.json`, strictly parses
records, and resolves each record from actual committed evidence:

- current revision and current Manifest/Scene bytes;
- Prefab GUID -> path in committed Manifest;
- actual candidate/old Prefab path and digest;
- committed Instance source identity/overrides;
- record phase revision facts.

### Decisions

| Evidence | Action |
|---|---|
| current old source + old Scene, record Prepared | cancel record, keep old state |
| current Phase 1 candidate Manifest + old Scene | write/update `PrefabPublished`, complete Phase 2 |
| current new Manifest + new Scene, any usable record state | verify and clear record |
| Prepared record after Phase 1 but before record update | infer Phase 1 revision from actual current, continue Phase 2 |
| unknown/mismatched current, corrupt/unknown record, Scene/Manifest inconsistency | fail startup with `RecoveryRequired` |
| Phase 2 Publish failure | leave record, fail startup; retry later |
| Phase 2 Publish uncertainty | record `RecoveryRequired`; fail startup |

Recovery is idempotent: a successful Phase 2 completion advances current and
removes the record; a second startup loads the same current without creating
another revision. Other Instance metadata is not modified.

After any recovery Publish, the loader uses the RevisionStore's updated
`CommittedManifestPath`/`CommittedScenePath`, not the pre-recovery
`RevisionOpenResult` paths.

## 2. Tests

`PrefabInstanceTest.StartupRecoveryCompletesPhase2AndIsIdempotent`:

- Phase 1 record left after an instance override;
- reopen completes Phase 2 automatically;
- live metadata is clean with Phase 1 source revision;
- Recovery Record is removed;
- a second reopen is a no-op and does not add a revision.

`PrefabInstanceTest.StartupRecoveryRetriesAfterPhase2PublishFailure`:

- injected Phase 2 Manifest write failure during startup;
- open fails with RecoveryRequired;
- record remains `PrefabPublished`, current remains Phase 1;
- retry completes recovery.

`PrefabInstanceTest.StartupRecoveryFailsClosedOnCorruptRecord`:

- corrupt record fails startup with explicit RecoveryRequired;
- no automatic overwrite.

## 3. Evidence

```text
Native Debug/Release : 1282 executed, 1280 PASS, 1 PICK-01, 1 SDK skip
test_content         : 176 tests, 175 PASS + 1 SDK skip
test_bridge          : 142/142 Debug and Release
UIA                  : not rerun; no UI change
Managed static       : 385
```

## 4. Known limits

- Apply ABI/UI remains G6-4-6.
- Recovery retry is process/manual-open based; no UI recovery directive yet.
- Current Phase 2 failure can leave the record in `PrefabPublished` (safe
  retry) or `RecoveryRequired` (unknown commit), as recorded.
- No source reconciliation/conflict handling (G7).

## 5. Git

Uncommitted; no commit/push/reset/clean/stash.
