# P8 G6-4-7 - Fault Matrix and G6-4 Closure

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Fault matrix**: `docs/P8-G6-4-7-Fault-Matrix.md`

## 1. Final G6-4 assessment

Apply is implemented as Phase 1 Prefab Publish plus Phase 2 Scene Metadata
Publish, backed by strict Recovery Records, actual-byte recovery decisions, and
RecoveryRequired fail-closed behavior.

Verified:

- Candidate/source/base/field validation.
- Durable Recovery Record strict read/write/integrity.
- Phase 1 same-GUID content-addressed Prefab revision.
- Phase 2 committed Scene metadata update and override clearing.
- Startup recovery before Manifest/Scene exposure.
- Fault injection for applicable Phase 1/Phase 2 Publish timings.
- Crash-state tests: Prepared cancellation, Phase 1 record-loss inference,
  Phase 2 retry, corrupt record, idempotent startup.
- ABI v1.19 / 66 ops and Inspector Apply feedback.
- Full Native Debug/Release and bridge/content regression.

## 2. Evidence

```text
Native Debug/Release : 1285 executed, 1283 PASS, 1 PICK-01, 1 SDK skip
test_content         : 176 tests, 175 PASS + 1 SDK skip
test_bridge          : 145/145 Debug and Release
UIA Debug/Release    : 14/14 each
Managed static       : 385
```

Historical PICK-01 and SDK skip remain separately registered. No new stable G6-4
failure is claimed.

## 3. Proposed closure status

`P8-G6-4 — PASS — CLOSED — TWO-PHASE PREFAB APPLY & RECOVERY VERIFIED — SCOPE-BOUND`

Retained limits:

- Fault points 3/4 are historical G4-3 evidence on first-publish marker paths;
  not re-fired on the existing versioned baseline.
- Source update/reconciliation and conflict resolution remain G7.
- No UI recovery action; RecoveryRequired is visible/blocked.
- Apply is not ordinary Scene Undo.
- Nested Prefab/Variant/Material-Texture/Skinning overrides remain unsupported.

## 4. Git

Uncommitted; no commit/push/reset/clean/stash.
