# P8 G6-4-7 - Apply / Recovery Fault Matrix

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Scope**: Apply Phase 1/2 fault injection, crash/restart recovery, idempotency,
and final G6-4 closure evidence.

## 1. Fault matrix

| Area | Covered |
|---|---|
| Phase 1 | RevisionFaultPoint 0,1,2,5,6,7 |
| Phase 2 | RevisionFaultPoint 0,1,2,5,6,7 |
| Phase 1 3/4 | Not fired on existing versioned baseline; historical G4-3 first-publish evidence retained |
| Phase 2 3/4 | Same marker timing limitation; no false claim |
| Phase 1 safe failure | old current/revision/Scene/Prefab unchanged; record removed |
| Phase 1 uncertainty | record `RecoveryRequired`; session blocked |
| Phase 2 safe failure | Phase 1 current/record/overrides retained |
| Phase 2 uncertainty | record `RecoveryRequired`; session blocked |
| Corrupt record | startup fails closed with RecoveryRequired |
| Prepared old current | startup cancels record, old source retained |
| Phase 1 committed but record update lost | startup infers Phase 1 from actual Manifest/Scene and completes Phase 2 |
| Phase 1 committed -> restart | startup completes Phase 2, removes record |
| Phase 2 publish failure -> restart | record retained, startup retry completes |
| Repeated startup | no duplicate revision, no repeated metadata mutation |
| Other instances | target Apply does not alter their source identity |

## 2. Tests

- `Phase1FaultPointsKeepOldCurrentOrExposeRecovery`
- `Phase2FaultPointsKeepPhase1OrRecovery`
- `StartupRecoveryCancelsPreparedBeforePhase1`
- `StartupRecoveryInfersPhase1WhenRecordUpdateWasLost`
- `StartupRecoveryCompletesPhase2AndIsIdempotent`
- `StartupRecoveryRetriesAfterPhase2PublishFailure`
- `StartupRecoveryFailsClosedOnCorruptRecord`
- `Phase2UpdatesInstanceMetadataAndClearsOverrides`
- `Phase2FailureKeepsPhase1RecordAndOverrides`
- C ABI Apply flow and real UIA Apply flow from G6-4-6

## 3. Evidence

```text
Native Debug/Release : 1285 executed, 1283 PASS, 1 PICK-01, 1 SDK skip
test_content         : 176 tests, 175 PASS + 1 SDK skip
test_bridge          : 145/145 Debug and Release
UIA Debug/Release    : 14/14 each (G6-4-6 run; final delta is Native-only)
Managed static       : 385
```

## 4. Remaining boundaries

- Marker fault points 3/4 are not re-fired on an existing versioned baseline;
  G4-3 first-publish marker evidence is retained. This is an evidence reuse,
  not a G6-4-7 execution claim.
- No source reconciliation/conflict resolution (G7).
- No dedicated UI recovery control; RecoveryRequired is reported and native
  operations block.
- Apply is not ordinary Scene Undo.

## 5. Git

Uncommitted; no commit/push/reset/clean/stash.
