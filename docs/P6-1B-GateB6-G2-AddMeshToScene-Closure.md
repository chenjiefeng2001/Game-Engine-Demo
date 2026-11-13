# P6-1B Gate B6-G2 — Add Mesh to Scene

**Status**: PASS — SCOPE-BOUND

## 1. User flow

- AssetBrowser bottom action: **Add Mesh to Scene** (`AddMeshToSceneBtn`, AutomationId present).
- Enabled only when a committed Mesh asset is selected (`CanAddSelectedMeshToScene`).
- Flow:
  1. Verify selected type is Mesh and `GetAssetStatus(index)==0`.
  2. `CreateEntity("Mesh")`.
  3. `AddComponent(entity,"Mesh")`.
  4. `SetComponentProperty(entity,"Mesh","meshGuid", <asset GUID>)`.
  5. Failure after create removes the entity (no half-valid persistent entity).
  6. Select the new entity and refresh Inspector.

## 2. Persistence contract

- Only `meshGuid` is persisted (Scene v1 component data).
- No mesh bytes copied into Scene; no GPU handle/pointer/path persistence.
- Multiple entities may reference the same GUID; B5 runtime reuses the same CPU/GPU/binding entry.

## 3. Runtime behavior

- Next `PumpOneFrame` runs `MeshRuntimeScene::Sync`; the GUID resolves committed current via `MeshAssetResolver`.
- Mesh appears in the production Viewport via the existing B5 renderer path.
- Missing/invalid asset or load failure leaves an explicit `MeshRuntimeStatus`; it does not mutate the GUID.

## 4. Tests

- `ImportCommitTest.MeshComponentGuidRoundTripThroughCAbi`:
  create entity → add Mesh → set `meshGuid` via C ABI → read back → SaveProject → cold Reopen → GUID/status restored; invalid GUID write rejected.
- B6 UIA test: AssetBrowser Mesh row → Add Mesh to Scene → Inspector GUID/Ready → production Viewport non-clear pixels.
- Property reflection test in `MeshComponentContractTest.ReflectionPropertiesExposeGuidAndDiagnostics`.

## 5. Not included

- No automatic centering/scaling/rotation.
- No gizmo, undo/redo, hierarchy editing.
- No second Mesh identity; Scene version remains 1.
