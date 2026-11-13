# P7-B G1 - Transform and History Core Closure

**Status**: PASS - SCOPE-BOUND

## Delivered

- Scene v1 additively persists `rotation` (`[x,y,z,w]`, default identity) and
  `scale` (`[x,y,z]`, default one) without a Scene version bump.
- `TransformComponent` remains authoritative (position, quaternion, scale).
- Typed session history stores before/after Scene JSON snapshots:
  - one transaction = one user action;
  - bounded to 64 entries, oldest trimmed;
  - redo branch cleared on a new commit;
  - no-op commits do not add history;
  - undo/redo replace Scene state only, never assets or revisions.
- Save checkpoint is the last saved Scene JSON; dirty is recomputed from the
  current Scene after commit, undo, redo, and reopen.
- `GetEntityTransform` / `SetEntityTransform` reject invalid indices and
  non-finite values; rotation input is Euler degrees at the UI boundary.

## ABI

Additive **v1.12**, op count **47**:

- `GET_ENTITY_TRANSFORM=37` (36 bytes, 9 floats)
- `SET_ENTITY_TRANSFORM=38` (9 floats)
- `UNDO_SCENE=39`
- `REDO_SCENE=40`
- `CAN_UNDO_SCENE=41`
- `CAN_REDO_SCENE=42`
- `BEGIN_SCENE_EDIT=43`
- `COMMIT_SCENE_EDIT=44`
- `CANCEL_SCENE_EDIT=45`
- `GET_EDITOR_VIEW_PROJECTION=46` (64 bytes, production camera)

Prior op numbers, status values, `RuntimeResult`, and Scene/Mesh/Material formats
are unchanged.

## Evidence

- `ImportCommitTest.TransformUndoRedoAndColdRestart`: transform set/get, undo,
  redo, NaN rejection, save/reopen persistence.
- `ImportCommitTest.EntityDeleteUndoRestoresMeshGuid`: delete/undo restores the
  Mesh component GUID and order.
- Full native inventory: 1208 discovered / 1208 executed; 1206 PASS,
  1 PICK-01, 1 SDK SKIP.
