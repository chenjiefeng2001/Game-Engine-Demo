# P8 G6-4-3 - Phase 1 Prefab Revision Publish Closure

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Report**: `docs/P8-G6-4-3-Phase1-Prefab-Publish-Report.md`

## 1. Delivered

- Prepared Recovery Record persisted before Publish.
- Same-GUID candidate Prefab file in content-addressed storage.
- Phase 1 Publish with candidate Manifest and old committed Scene.
- Actual Phase 1 revision recorded in `PrefabPublished` record.
- Old current/revision/Scene/old Prefab bytes retained on safe failure.
- RecoveryRequired handling for uncertain current/readback.
- Duplicate Apply blocked while a record is present.
- Live Scene metadata and overrides deliberately unchanged pending Phase 2.

## 2. Evidence

```text
Native Debug/Release : 1277 executed, 1275 PASS, 1 PICK-01, 1 SDK skip
test_content         : 176 tests, 175 PASS + 1 SDK skip
test_bridge          : 137/137 Debug and Release
UIA                  : not rerun; no UI/runtime behavior changed
```

## 3. Limits

- Phase 2, startup recovery, Apply ABI/UI remain G6-4-4/G6-4-5/G6-4-6.
- Marker-write fault points 3/4 are not exercised on an existing versioned
  baseline; G4-3 retains their first-publish evidence.
- No automatic recovery yet.

## 4. Git

Uncommitted; no commit/push/reset/clean/stash.
