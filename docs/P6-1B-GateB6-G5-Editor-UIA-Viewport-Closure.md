# P6-1B Gate B6-G5 — Real Editor UIA + Production Viewport

**Status**: PASS — REAL PROCESS UIA + PRODUCTION FBO PIXELS

## 1. UIA flow (implemented)

`tests/EditorAvalonia.IntegrationTests/B6MeshEditorUITests.cs`:

1. Test creates an isolated project root and writes an empty committed-project shell + real OBJ/MTL.
2. Test imports the OBJ through the production bridge (`EditorSession_ImportAsset type=2`) before launching the editor.
3. Launches the real `AvaloniaEditor.exe --m1import-host <root>` and waits for the exact host title.
4. Selects the **Asset Browser** tab; waits for both a `.mesh` and `.material.json` row (Mesh + Material discovery).
5. Selects the Mesh row and invokes the real **Add Mesh to Scene** button.
6. Asserts Inspector `MeshGuidValue` equals the imported GUID and `MeshStatusValue == Ready`.
7. Reads `ViewportFrameStats` (AutomationId) populated from `ViewportDisplayReceipt`: production presentation FBO `nonClear` pixels must exceed 20,000 (quad coverage; axes alone are far below).
8. Saves via the real **Save** button.
9. Closes the editor process; relaunches the editor and reopens the same project.
10. Selects the restored hierarchy entity; asserts GUID and Ready status again, and reasserts Viewport `nonClear` pixels.

All controls are located by AutomationId, not screen coordinates.

## 2. Production viewport evidence

- `ViewportFrameStats` is updated in `MainWindow.OnFrameAdvanced` from `ViewportFramePresenter.LastDisplayed`.
- That receipt comes from the existing `EngineHost_Fetch(GET_PRESENTATION_FRAME)` path, which performs real `glReadPixels` on the production presentation FBO (B5 verified).
- Therefore the UIA assertion proves the actual production Viewport contains Mesh geometry after cold restart, not just Inspector state.
- Direct path is the editor default. RenderGraph remains covered by B5 `test_mesh_pixel` (9/9), including production readback, and is not regressed by B6 changes (no renderer changes).

## 3. Failure behavior

- Asset unavailable: Add action is refused using `GetAssetStatus`; no entity is created.
- Invalid GUID write: component property rejects; entity is removed by the Add flow.
- Runtime load failure: B5 `MeshRuntimeScene` mirrors explicit status (`MissingAsset/InvalidAsset/ReaderFailure/...`) into the Inspector; no fake resource identity.
- Reimport/committed-current behavior is unchanged from B5/B6-G4.

## 4. Evidence

- Debug UIA: 1/1 PASS.
- Release UIA: 1/1 PASS.
- B6 UIA test is a managed IntegrationTest, outside the native CTest inventory; it is run explicitly with `dotnet test tests/EditorAvalonia.IntegrationTests/... --filter FullyQualifiedName~B6MeshEditorUITests`.

## 5. Limitations

- UIA requires an interactive Windows desktop; on non-interactive agents the test becomes Inconclusive (existing harness convention).
- UIA verifies Direct production path; RenderGraph pixel proof remains B5-owned.
- No screenshot/bitmap diff in managed test; the pixel evidence is the production FBO non-clear count from the same frame displayed in `ViewportImage`.
