# P8 G6-3 - Revert Override Report

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Scope**: Field-level Revert, instance-level Revert, exact-source validation,
single Snapshot transaction, ABI `REVERT_OVERRIDE = 64`, minimal UI.
**Excluded**: Apply/Recovery (G6-4), source reconciliation (G7).

## 1. Delivered

### Native

- `EditorSession::RevertPrefabOverride(root, localId, component, field)`:
  - finds the exact override;
  - loads exact `sourceDigest`/`sourcePath`;
  - restores Transform/component base value;
  - deletes only that override;
  - updates sync state;
  - commits one P7-B Snapshot transaction.
- `EditorSession::RevertPrefabInstance(root)`:
  - requires a complete source identity and at least one override;
  - validates source Local ID count/mapping and component sets;
  - restores source names, Local Transforms, and allowlisted component data;
  - clears all overrides and sets `clean`;
  - keeps the anchor and Scene Entity IDs;
  - fails closed on mapping/component mismatches; never deletes unrelated Scene
    entities;
  - commits one Snapshot transaction.

Both preserve the Prefab asset revision and source identity.

### ABI / UI

- ABI v1.18 / 65 ops:
  - `REVERT_OVERRIDE = 64` (field request `root;local;component;field`;
    instance request `root;*`);
  - C exports `EditorSession_RevertOverride` and
    `EditorSession_RevertPrefabInstance`.
- EngineHost Dispatch and managed channel/legacy methods are wired.
- Inspector override combo, `Revert Field`, and `Revert Instance` controls.
- No Apply operation is added at this gate.

## 2. Tests

- `PrefabInstanceTest.RevertFieldAndInstanceRestoreBaseValues`:
  - field revert restores base and removes only the selected override;
  - other overrides remain;
  - Undo/Redo restore field and metadata as one transaction;
  - Mesh GUID override and instance revert restore base data;
  - anchor/child IDs and parent link remain;
  - Save/cold restart preserves clean source identity.
- `PrefabInstanceTest.RevertFailsClosedWhenSourceIsUnavailable`:
  - missing source rejects both field and instance revert;
  - overrides and edited Scene remain unchanged.
- C ABI test:
  - transform override, `EditorSession_RevertOverride`, and clean JSON result;
  - instance revert without overrides returns failure.
- UIA:
  - Inspector override, field Revert, and instance Revert on real editor.

## 3. Evidence

```text
Native Debug   : 1267 executed, 1265 PASS, 1 PICK-01, 1 SDK skip
Native Release : 1267 executed, 1265 PASS, 1 PICK-01, 1 SDK skip
test_content   : 169 tests, 168 PASS + 1 SDK skip
test_bridge     : 134/134 Debug and Release
UIA Debug       : 14/14
UIA Release     : 14/14 (one transient asset-search UIA run failed and passed on clean rerun)
Managed static  : 385
```

## 4. Known limits

- Apply and Recovery remain G6-4; final G6 count will be 66.
- Instance revert restores only persisted Prefab data (name, Local Transform,
  allowlisted component payloads); Mesh `visible`, Skinning, and Material/Texture
  fields remain outside the Override contract.
- No source update/reconciliation.
- Legacy G5 source identity fails closed for Revert.

## 5. Git

Uncommitted; no commit/push/reset/clean/stash.
