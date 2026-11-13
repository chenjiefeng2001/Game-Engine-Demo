# P7-A G7 — End-to-End Workflow Closure

**Status**: PASS — REAL PROCESS UIA

- `B6MeshEditorUITests` (extended for P7-A) runs the real `AvaloniaEditor.exe`:
  1. imports OBJ/MTL and opens the project;
  2. selects Mesh in Asset Browser, Add Mesh to Scene;
  3. verifies Mesh GUID/Ready and production FBO non-clear pixels;
  4. clicks Reimport Source;
  5. Show Dependencies and verifies Material dependency row;
  6. selects Material, edits Base Color R to 0.25, saves;
  7. saves project, closes the editor;
  8. relaunches, reopens project, verifies Mesh GUID/Ready, Viewport non-clear pixels, and
     Material Base Color R == 0.25.
- Debug and Release UIA each pass 1/1.
- Failure paths are covered natively (missing source, edited-material guard, invalid data,
  Publish fault matrix).
- UIA viewport evidence uses the production presentation FBO receipt (`nonClear` pixels), not
  Inspector state alone.
