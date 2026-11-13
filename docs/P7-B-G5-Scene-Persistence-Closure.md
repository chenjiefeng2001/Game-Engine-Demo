# P7-B G5 - Scene Persistence Closure

**Status**: PASS

## Delivered

- Scene v1 persists additive rotation quaternion and scale; old scenes without
  these fields load as identity / one.
- Position, rotation, and scale survive Save Project and cold restart.
- Saved Scene JSON is the dirty baseline; Save does not clear history.
- Reopening starts with empty history and the loaded Scene as checkpoint.
- Delete/Undo/Redo restore component state, including Mesh GUIDs.

## Evidence

- `ImportCommitTest.TransformUndoRedoAndColdRestart` verifies values after
  save/reopen.
- Real UIA saves position 2, rotation Z 45, scale X 2, closes the editor,
  relaunches, and reads the same values from the Inspector.
- P6-1B/P7-A Scene and Mesh GUID persistence regressions remain green.
