# P8 G6-4-6 - Apply ABI / UI Closure

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Report**: `docs/P8-G6-4-6-Apply-ABI-UI-Report.md`

## 1. Delivered

- ABI v1.19 / 66 ops, `APPLY_PREFAB_OVERRIDES=65`.
- C export, EngineHost Dispatch, Managed service, Inspector Apply button.
- Native precondition validation and Phase 1 + Phase 2 composition.
- Apply success clears overrides and updates Source Identity.
- Inspector recovery/conflict/legacy gating and status feedback.
- C ABI and real UIA Apply coverage.

## 2. Evidence

```text
Native Debug/Release : 1282 executed, 1280 PASS, 1 PICK-01, 1 SDK skip
test_content         : 176 tests, 175 PASS + 1 SDK skip
test_bridge          : 142/142 Debug and Release
UIA Debug/Release    : 14/14 each
Managed static       : 385
```

## 3. Limits

Full Fault Matrix/final closure remains G6-4-7. No source reconciliation (G7).
Apply is not ordinary Scene Undo. One transient Debug B6 UIA timeout passed on
clean rerun.

## 4. Git

Uncommitted; no commit/push/reset/clean/stash.
