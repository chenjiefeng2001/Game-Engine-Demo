# P7-B G6 - Editor UIA Closure

**Status**: PASS - DEBUG AND RELEASE

## Scenario

`B6MeshEditorUITests.Mesh_asset_can_be_added_saved_and_reloaded_after_editor_restart`
now covers the integrated P7-B path:

1. Launch real `AvaloniaEditor.exe` and open a scratch project.
2. Import Mesh, select it in Asset Browser, Add Mesh to Scene.
3. Verify Mesh GUID + Ready and production Viewport non-clear pixels.
4. Edit Position/Rotation/Scale in Inspector, Apply.
5. Undo -> redo through toolbar buttons.
6. Drag Translate, Rotate, and Scale gizmo handles; Undo after each.
7. Edit Material, verify dependencies, Save Project.
8. Close, relaunch, select the entity, verify transform values, Mesh GUID,
   Ready, and production Viewport.
9. Verify the saved Material value.

## Result

- Debug: 1/1 PASS.
- Release: 1/1 PASS.
- Real Windows mouse messages are sent to the production editor window; gizmo
  handle coordinates come from the real camera projection, not hard-coded
  screen positions.
