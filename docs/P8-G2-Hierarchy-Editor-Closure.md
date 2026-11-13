# P8 G2 - Hierarchy Editor Closure

**Status**: PASS - SCOPE-BOUND
**Contract**: `docs/P8-GateA-Scene-Composition-Contract.md` (frozen)
**Scope**: ID-based Hierarchy ABI/UI, renderer hierarchy traversal, and picking
index consumption. Prefab/Duplicate/Apply remain P8-G3/G4.

## 1. Delivered

### ABI v1.13

`editor_bridge/capi.h` is now ABI v1.13 with `ENGINE_HOST_OP_COUNT = 58`.
Existing index APIs are preserved. Added:

| Op | Name |
| --- | --- |
| 47 | `GET_ENTITY_SCENE_ID` |
| 48 | `GET_TOTAL_ENTITY_COUNT` |
| 49 | `GET_ENTITY_PARENT_ID` |
| 50 | `GET_ENTITY_CHILD_COUNT` |
| 51 | `GET_ENTITY_CHILD_ID` |
| 52 | `GET_ENTITY_NAME_BY_ID` |
| 53 | `GET_ENTITY_TRANSFORM_BY_ID` |
| 54 | `SET_ENTITY_TRANSFORM_BY_ID` |
| 55 | `SET_ENTITY_PARENT` |
| 56 | `DELETE_ENTITY_BY_ID` |
| 57 | `RENAME_ENTITY_BY_ID` |

Entity IDs cross the ABI as decimal strings. `SET_ENTITY_PARENT` accepts
`"child;parent;mode"`, where `mode` is the native `0 = KeepLocal`,
`1 = KeepWorld` contract. The managed UI maps its `Keep World` / `Keep Local`
combo to that contract explicitly.

C exports were added for every new operation, together with managed interface,
channel, legacy, and `EditorBridgeApi` P/Invoke coverage.

### EditorSession

Added native ID-based operations:

- `GetEntitySceneId`
- `GetTotalEntityCount`
- `GetEntityParentId`
- `GetEntityChildCount`
- `GetEntityChildId`
- `GetEntityNameById`
- `GetEntityTransformById`
- `SetEntityTransformById`
- `SetEntityParent`
- `DeleteEntityById`
- `RenameEntityById`

All mutating operations remain covered by the existing P7-B Snapshot History.

### Renderer and picking

- `Application::RenderProduction3DFrame()` now builds its draw list from
  `Scene::GetAllEntities()` and submits descendants that are active in the
  hierarchy, rather than root objects only.
- `EngineHost::ResolveSceneIndexForEntityId()` now scans the flattened
  `GetAllEntities()` order. The existing 2D sprite ID pass therefore resolves
  hierarchy entities consistently with the renderer.
- 3D mesh picking is still not implemented; this is not a regression from G1.

### Hierarchy UI

- Hierarchy switched from Avalonia `TreeView` to an indented `ListBox` because
  `TreeView` virtualization/realization made UIA selection and drag delivery
  unreliable.
- `HierarchyRows` is rebuilt as a preorder list with stable Scene IDs, names,
  parent IDs, depth, indentation, and expand/collapse glyphs.
- Expand/collapse is UI-owned: collapsed nodes keep their row, children are
  filtered on rebuild, and the toggle preserves selection.
- Added `+ Child`, `Outdent`, and `ReparentModeBox` (`Keep World` / `Keep Local`).
- Inspector transform read/apply, rename, delete, and hierarchy selection now use
  the selected Scene ID. Child transforms remain editable.
- Auto-selects the first hierarchy row after open.
- Drag/drop reparent handlers are installed on the ListBox with tunnel pointer
  handlers; drag and `Outdent` both route through the same `ReparentNode`
  transaction.

## 2. Tests

### Native

Both Debug and Release:

```text
Native targets      : 21
Discovered          : 1218
Executed            : 1218
Passed              : 1216
Failed              : 1 (PICK-01, pre-existing)
Skipped             : 1 (SDK shaderc)
Managed static      : 380
CTest entries       : 22
```

- `test_content`: 137 tests, 136 PASS + 1 SDK skip.
- `test_bridge`: 117/117 PASS.
- `test_editor_capi` asserts ABI v1.13 and op count 58.

### UIA

`B6MeshEditorUITests` now contains two real editor workflows:

- P7-B mesh/Inspector/Save regression: PASS.
- `Hierarchy_reparent_keeps_scene_and_undo`: PASS.

G2 UIA covers create root, add child, child Inspector transform, undo/redo,
save, hierarchy parent persistence, `Outdent`, save, and undo/redo parent
restoration. Debug and Release: 2/2 PASS each.

## 3. Known limits / handoff

- Automated mouse-drag delivery into the Avalonia ListBox is not proven. The
  G2 UIA test uses `Outdent`, which reaches the same reparent transaction; the
  drag path is implemented but remains a manual/automation follow-up.
- Expand/collapse has no dedicated UIA assertion yet.
- Picking remains 2D-only; 3D mesh picking is outside the current P7-C scope.
- Prefab, duplicate/remap, override, and two-phase Apply are P8-G3/G4.
- The UI mode mapping is explicit (`Keep World` -> native `1`,
  `Keep Local` -> native `0`); do not pass the ComboBox index directly to the ABI.

## 4. Git

G2 changes are uncommitted. No commit, push, reset, clean, or stash was performed.
