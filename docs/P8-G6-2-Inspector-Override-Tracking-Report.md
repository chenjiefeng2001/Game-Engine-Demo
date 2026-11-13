# P8 G6-2 - Inspector Override Tracking Report

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G6-A-Override-Apply-Revert-Contract.md`
**Scope**: Automatic Override tracking through existing Inspector write paths,
instance context resolution, UI override status. No Revert/Apply.

## 1. Delivered

### Instance context

- `Scene::FindPrefabInstanceForEntity` walks the Scene parent chain to the
  Prefab Instance Anchor.
- EditorSession resolves the Local ID from `localToScene`.
- Non-instance edits retain the existing write path.

### Tracking seam

`SetEntityTransformById` and `SetComponentProperty` now:

- resolve the owning instance/local ID;
- load and verify the exact Prefab source (`sourceDigest`/`sourcePath`);
- fail closed for legacy/incomplete source identity;
- compare the new value with the Prefab base value;
- add/update/remove an Override through the G6-1 model;
- update `syncState`;
- keep the existing P7-B Snapshot transaction.

Transform records Local position/quaternion/scale components. Component fields
use the frozen G6-A allowlist; unsupported Mesh `visible`, Skinning, and
Material/Texture fields fail before mutation. A `Mesh.meshGuid` edit is tracked.

### UI feedback

- Native `GetPrefabInstanceJsonForEntity` and ABI
  `GET_PREFAB_INSTANCE_JSON = 63`.
- Inspector displays Prefab instance status, sync state, override count, and the
  list of overridden `Component.Field` entries.
- No Revert/Apply controls were added.

### ABI note

G6-2 adds only `GET_PREFAB_INSTANCE_JSON = 63`; ABI is v1.17 / 64 ops at this
gate. The G6-A final target (66 ops with Revert/Apply) remains for G6-3/G6-4.

## 2. Tests

- `PrefabInstanceTest.InspectorWritesTrackAndClearOverrides`:
  - Transform edit creates override;
  - base-value restore removes it;
  - Undo/Redo restores field + metadata;
  - Mesh GUID edit tracks;
  - Save/cold reopen preserves override;
  - Mesh `visible` rejects without mutation/override.
- `Prefab_instance_inspector_tracks_override_metadata` UIA:
  - prepared instance opened in real editor;
  - Inspector status identifies Prefab;
  - Apply Transform creates `Transform.position.x` override;
  - Undo removes the tracked override.
- `PrefabOverrideTest` (G6-1) and Scene v2 persistence remain passing.

## 3. Evidence

```text
Native Debug   : 1265 executed, 1263 PASS, 1 PICK-01, 1 SDK skip
Native Release : 1265 executed, 1263 PASS, 1 PICK-01, 1 SDK skip
test_content   : 169 tests, 168 PASS + 1 SDK skip
test_bridge     : 132/132 Debug and Release
UIA Debug       : 14/14
UIA Release     : 14/14
Managed static  : 385
```

Historical PICK-01 and SDK skip remain unchanged. No other stable failure.

## 4. Known limits

- Revert/Apply/Recovery remain G6-3/G6-4.
- ABI final count remains 66 after Revert/Apply operations are added.
- Existing component property channel addresses root entities by index; G6-2
  tracks Transform for any mapped instance entity and component fields for the
  instance root through the current Inspector path. Child component Inspector
  expansion remains outside this gate.
- Mesh `visible`, Skinning, Material/Texture overrides remain rejected.
- Legacy G5 source identity cannot track edits; a clear failure is returned.

## 5. Git

Uncommitted; no commit, push, reset, clean, or stash.
