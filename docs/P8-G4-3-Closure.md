# P8 G4-3 - Prefab Asset Integration Closure

**Status**: PASS - CLOSED - PREFAB ASSET INTEGRATION VERIFIED - SCOPE-BOUND
**Contract**: `docs/P8-G4-Prefab-Format-Contract.md` (frozen)
**Scope**: Prefab AssetType, manifest/registry, content-addressed storage,
RevisionStore Publish, committed Scene boundary, Reader/dependency integration,
ABI v1.15. UI workflow remains G4-4.

## 1. Delivered

- `Content::AssetType::Prefab = 4`, type string `"Prefab"`.
- `EditorSession::CreatePrefabFromEntity`:
  - captures the live Scene subtree;
  - uses a fresh nonzero GUID and the G4-2 canonical Writer/Digest;
  - stages `assets/prefabs/<stem>.<digest16>.prefab.json`;
  - rejects path collisions and unsupported legacy bindings;
  - registers the GUID/path/type in a candidate manifest;
  - publishes with committed Scene bytes;
  - updates the registry/asset order only on success;
  - emits existing `EV_ASSET_IMPORTED type=4`.
- ABI v1.15 / 61 ops, op `CREATE_PREFAB_FROM_ENTITY = 60`, C export and
  EngineHost dispatch.
- Managed constant/P/Invoke/service mapping and Asset Browser Prefab type/filter;
  no Create Prefab UI.
- Prefab Reader integration with expected Asset GUID.
- Prefab direct Mesh dependencies and Mesh -> Prefab dependents.
- Corruption/missing/orphan and all eight RevisionFaultPoint timings covered.

## 2. Boundary compliance

- Texture/Script/Mesh/Material values unchanged; Prefab is value 4.
- Prefab bytes are staged before Publish, not covered by current manifest/Scene
  hashes.
- Old current/Manifest/Scene remain intact on failure.
- Orphan candidate files are allowed and remain unregistered.
- No cross-file atomic claim is made.
- No `create prefab` UI/Instantiate/Override was added.

## 3. Verification

- Native Debug/Release: 1256 executed, 1254 PASS, 1 PICK-01, 1 SDK skip.
- `test_content`: 165 tests, 164 PASS + 1 skip.
- `test_bridge`: 127/127 (Debug and Release).
- UIA: 10/10 (Debug and Release); ABI regression only, no Create Prefab UI.
- Managed static: 381.

The `MaterialEditUpdatesProductionPixels` GPU transient failed one Release
inventory and passed standalone and on clean rerun. It is retained in the test
history and not treated as a G4-3 regression.

## 4. Known limits

- Create Prefab editor workflow and UIA remain G4-4.
- No same-GUID Prefab update API; each Create allocates a new asset GUID.
- Prefab file bytes remain outside RevisionStore hashes.
- Missing dependencies remain valid GUID references for G5 runtime handling.
- Legacy Sprite/Script binding remains fail-closed.

## 5. Git

G4-3 changes are uncommitted. No commit, push, reset, clean, or stash was
performed.
