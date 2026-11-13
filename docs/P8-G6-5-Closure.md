# P8 G6-5 - Integrated Verification & G6 Closure

**Status**: PASS - SCOPE-BOUND - PENDING ACCEPTANCE
**Report**: `docs/P8-G6-5-Integrated-Verification-Report.md`

## 1. Final G6 assessment

G6 delivers persistent Prefab overrides, field/instance Revert, and a recoverable
two-phase Apply integrated through Native, ABI, EngineHost, Managed service, and
Inspector.

The chain is complete and the G6 exit criteria are satisfied for the accepted
scope. No source change was made in G6-5; no new full test run is claimed.

## 2. Latest accepted evidence

```text
Native Debug/Release : 1285 executed, 1283 PASS, 1 PICK-01, 1 SDK skip
test_content         : 176 tests, 175 PASS + 1 SDK skip
test_bridge          : 145/145 Debug and Release
UIA Debug/Release    : 14/14 each
Managed static       : 385
ABI                  : v1.19 / 66 ops
```

## 3. Proposed closure status

`P8-G6 — PASS — CLOSED — PREFAB OVERRIDE / APPLY / REVERT INTEGRATED — SCOPE-BOUND`

Retained limits:

- Marker fault points 3/4 are reused G4-3 evidence, not rerun here.
- G7 owns source update/reconciliation/conflict resolution.
- No UI recovery action; RecoveryRequired is exposed and blocks writes.
- Nested Prefab/Variant, Material/Texture and Skinning Overrides remain
  unsupported.
- 3D Picking remains unimplemented.
- Apply is not ordinary Scene Undo.

## 4. Next stage

`P8-G7 Source Update / Reconciliation` is READY / NOT STARTED, subject to a new
contract freeze. It must not reinterpret G6 source identity or RecoveryRecord
semantics.

## 5. Git

All G6 changes remain uncommitted. No commit/push/reset/clean/stash.