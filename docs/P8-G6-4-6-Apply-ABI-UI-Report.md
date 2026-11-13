# P8 G6-4-6 - Apply ABI / EngineHost / Managed / Inspector Report

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Scope**: Public Apply operation and minimal Inspector workflow. No extra
Fault Matrix closure (G6-4-7).

## 1. Delivered

ABI:

- v1.19 / 66 ops;
- `ENGINE_HOST_OP_APPLY_PREFAB_OVERRIDES = 65`;
- C export `EditorSession_ApplyPrefabOverrides`;
- EngineHost Dispatch;
- Managed service/channel/legacy path;
- `EV_COUNT` unchanged.

Native `EditorSession::ApplyPrefabOverrides`:

- rejects playing state, recovery-required, legacy source identity, empty
  overrides, recovery/conflict sync state;
- runs Phase 1 then Phase 2 via existing implementations;
- returns success only after Phase 2 completion; Phase 1/Phase 2 failure keeps
  the Recovery Record for startup recovery.

Inspector:

- `ApplyPrefabBtn` (`Apply Overrides`) is enabled only for a Prefab Instance with
  overrides, valid source identity, and non-recovery/conflict state;
- apply success clears overrides and updates source revision/path/digest;
- status shows sync state, override count, and source revision;
- Apply is not wrapped as ordinary Scene Undo.

## 2. Tests

- C ABI test extended through Save -> Phase 1/Phase 2 Apply: new content-addressed
  path, clean sync state, no remaining override field.
- UIA `Prefab_instance_inspector_tracks_override_metadata` extended with
  Inspector Apply: override -> Apply button -> clean/no overrides.
- Existing startup recovery and Phase 1/2 tests remain passing.

Focused/full evidence:

```text
Native Debug/Release : 1282 executed, 1280 PASS, 1 PICK-01, 1 SDK skip
test_content         : 176 tests, 175 PASS + 1 SDK skip
test_bridge          : 142/142 Debug and Release
UIA Debug/Release    : 14/14 each
Managed static       : 385
```

One Debug UIA rerun had the existing transient B6 child-parent timeout; the
clean rerun passed. It is not a stable G6-4-6 failure.

## 3. Known limits

- Full crash/fault matrix and final closure remain G6-4-7.
- No UI recovery action/dialog; RecoveryRequired is displayed as disabled state
  and blocked by native validation.
- No source reconciliation (G7).
- Apply is not ordinary Scene Undo.

## 4. Git

Uncommitted; no commit/push/reset/clean/stash.
