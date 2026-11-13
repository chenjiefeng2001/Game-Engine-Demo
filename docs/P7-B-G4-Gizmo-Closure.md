# P7-B G4 - Gizmo Closure

**Status**: PASS - SCOPE-BOUND

## Delivered

- Production Viewport overlay with X/Y/Z axis handles.
- Handles are projected from the real production 3D camera view-projection
  matrix (`GET_EDITOR_VIEW_PROJECTION`, 16 floats), not screen-space guessing.
- Translate, Rotate, and Scale modes:
  - Translate moves along the selected projected axis;
  - Rotate adds degrees around the selected Euler axis;
  - Scale multiplies the selected scale axis, clamped above 0.01.
- One drag is one native transaction:
  `BeginSceneEdit -> SetEntityTransform preview -> CommitSceneEdit`.
- Escape and window deactivation call `CancelSceneEdit` and restore the before
  state without history.
- Gizmo hit testing is handle-only; full 3D object picking remains deferred.
- Camera drag is bypassed only when a gizmo handle is hit.

## Evidence

Real UIA, Windows physical mouse messages against the production Viewport:

1. Translate drag changes PosX; Undo restores 2.0.
2. Rotate drag changes RotX; Undo restores 0.0.
3. Scale drag changes ScaleX; Undo restores 2.0.

Debug and Release UIA runs passed.

## Limits

- Overlay handles are not depth-occluded and there is no uniform center handle.
- Local-space gizmo axes are not offered; v1 uses world/root axes.
