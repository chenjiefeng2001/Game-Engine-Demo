# P6-1B Gate B6-G4 — Scene Persistence / Cold Restart

**Status**: PASS — SCOPE-BOUND

## 1. Contract

- Scene version remains 1.
- `MeshComponent` persists only `meshGuid` (`ToHex`); runtime status/revision/path/GPU handles are not serialized.
- Save uses the existing `RevisionStore::Publish` + Scene Serializer path.
- Reload resolves committed current and rebuilds runtime resources via B5; Scene GUID does not need rebinding after reimport.

## 2. Native evidence

`ImportCommitTest.MeshComponentGuidRoundTripThroughCAbi`:

1. Import OBJ (real `EditorSession_ImportAsset` type 2).
2. Create entity + AddComponent Mesh + SetComponentProperty `meshGuid` via C ABI.
3. `SaveProject` (published revision).
4. Destroy/recreate session (`Reopen`).
5. Read `meshGuid` and `runtimeStatus` via component properties → GUID preserved, status `NotRequested` before Sync.
6. Invalid GUID write rejected; entity/component remains valid with previous GUID.

Existing B5 evidence also covers cold host reload and committed-current re-resolution; B6 does not re-audit B5.

## 3. UIA evidence

`B6MeshEditorUITests`:

- real editor process opens a temp project (`--m1import-host`) with a committed Mesh/Material import;
- AssetBrowser select Mesh → Add Mesh to Scene → Inspector GUID/Ready;
- Save via real Save button;
- process closes, editor relaunches, project reopens;
- hierarchy selection restores Inspector GUID/Ready and production Viewport non-clear pixels.

Scratch project contains no GPU handles in Scene and no dependence on the prior process cache.

## 4. Limitations

- Cold restart is full `EngineHost`/editor process restart in the UIA test; no separate OS-level machine reboot.
- Missing asset scenes load with an explicit component status rather than a fake GUID; covered by B5 failure tests and native status contract.
