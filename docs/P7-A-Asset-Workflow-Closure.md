# P7-A — Asset Workflow Final Closure

**Status**: P7-A — CLOSED — COMPLETE — ASSET WORKFLOW VERIFIED — SCOPE-BOUND
**Branch/HEAD**: avalonia / `5de8f1a`（P7-A 未 commit/push）
**Precondition**: P6-1B CLOSED; P7-C CLOSED

## 1. Gate results

| Gate | Result | Document |
|---|---|---|
| G0 | COMPLETE — read-only map | `docs/P7-A-G0-Asset-Workflow-Map.md` |
| Gate A | FROZEN | `docs/P7-A-GateA-Asset-Workflow-Contract.md` |
| G1 Material Editing Core | PASS | `docs/P7-A-G1-Material-Editing-Core-Closure.md` |
| G2 Material Inspector | PASS — SCOPE-BOUND | `docs/P7-A-G2-Material-Inspector-Closure.md` |
| G3 Dependency Graph Core | PASS — source-provenance limit | `docs/P7-A-G3-Dependency-Graph-Core-Closure.md` |
| G4 AssetBrowser Dependencies | PASS — SCOPE-BOUND | `docs/P7-A-G4-AssetBrowser-Dependencies-Closure.md` |
| G5 Reimport Workflow | PASS — SCOPE-BOUND | `docs/P7-A-G5-Reimport-Workflow-Closure.md` |
| G6 Runtime Refresh | PASS — SCOPE-BOUND | `docs/P7-A-G6-Runtime-Refresh-Closure.md` |
| G7 End-to-End UIA | PASS | `docs/P7-A-G7-EndToEnd-Closure.md` |
| G8 Integrated Regression | PASS — baseline exceptions retained | this document |

## 2. Delivered workflow

`AssetBrowser Material` → Material Inspector → edit/validate → `UpdateMaterial` →
one RevisionStore publish (same GUID, new content-addressed Material path) →
MeshRuntimeScene reloads binding → production Viewport changes → cold restart restores value.

`Mesh/Material` → Show Dependencies → committed-data dependency/reverse list.

`Mesh` → Reimport Source / Reimport (Discard Edits) → existing OBJ/glTF/GLB import pipeline →
single atomic Publish → runtime revision refresh.

Local Material edits are recorded and block normal source reimport; explicit discard reimport
resets source content without changing the Material GUID.

## 3. ABI

Additive **v1.11** (real ABI increment, not "ABI unchanged"):

- Exports: `EditorSession_GetMaterialJson`, `UpdateMaterial`, `GetAssetDependencies`,
  `ReimportAsset`.
- EngineHost ops: `GET_MATERIAL_JSON=33`, `UPDATE_MATERIAL=34`,
  `GET_ASSET_DEPENDENCIES=35`, `REIMPORT_ASSET=36`; op count 37.
- `RuntimeResult` (76B), prior op numbers/status values, Scene/Mesh/Material formats unchanged.

## 4. Material capability matrix

| Field | Stored | Editable | Production shader |
|---|---|---|---|
| Base Color factor | yes | yes | yes (no texture case) |
| Base Color texture | yes | no (v1) | yes |
| Normal texture | yes | no (v1) | yes |
| Metallic / Roughness | yes | yes (stored) | no |
| Emissive factor/texture | yes | factor yes | no |
| Alpha cut/mode | yes | yes (stored) | no blending/alpha test |
| Occlusion / metallicRoughness texture | yes | no (v1) | no |

UI explicitly notes only Base Color currently affects production shading.

## 5. Evidence

### 5.1 Native (Debug == Release)

- `test_assetimport_commit`: **25/25 PASS** (G5 22 + 3 P7-A tests).
- `test_assetimport`: 148/148 PASS (G6/P7-C regressions retained).
- `test_mesh_pixel`: **11/11 PASS** (P7-C 10 + MaterialEditProductionPixels).
- `test_bridge`: 115/115 PASS (v1.11 ABI assertions updated).
- `test_content`: 129 discovered (128 PASS + 1 SDK SKIP).
- Full inventory: **1206 / 1206 executed; 1204 PASS / 1 FAIL (PICK-01) / 1 SKIP (shaderc)**.

P7-A native tests cover: field round-trip/validation/no-op revision, GUID/path stability,
cold restart, edit marker, guarded reimport, forced source reset, changed/missing source,
dependency forward/reverse graph, and production pixel change after Material save.

### 5.2 Managed / UIA

- Real `AvaloniaEditor.exe` UIA test: **1/1 PASS Debug, 1/1 PASS Release**.
- Covers: open project → AssetBrowser Mesh/Material → Add Mesh → Mesh Ready + production FBO
  non-clear pixels → Reimport Source → dependency view contains Material → Material Inspector
  edit Base Color → save → project save → editor close/relaunch → Mesh GUID/Ready/Viewport
  restored → Material Base Color == 0.25.
- Managed static test inventory: 379.

### 5.3 Builds

CMake Debug/Release full; EditorBridge Debug/Release; `editor_avalonia` Debug/Release;
IntegrationTests Debug/Release: all success (existing NU1900/CS warnings only).

## 6. Known limitations / accepted exceptions

- Source provenance is limited to Mesh JSON `sourceFile`; MTL/external buffer dependency files
  are not persisted in v1, so they are not shown in the dependency graph.
- Material texture slot editing is not implemented; only scalar fields are editable.
- Metallic/roughness/emissive/alpha are stored/validated but not consumed by current shader;
  UI marks this explicitly.
- Local-edited Material + source reimport does not merge; normal reimport is blocked, explicit
  discard resets to source.
- Dependency list is read-only; double-click navigation to a dependency asset is not implemented.
- No automatic asset GC; old content-addressed Material/Texture assets remain committed.
- Reimport failure UI path is covered natively; UIA covers success and edited-guard semantics
  indirectly through native tests.
- PICK-01 remains the only FAIL; shaderc SKIP remains conditional.
- P7-B (Scene editing/Undo/Gizmo) not started.

## 7. Workspace

- P7-A changes remain uncommitted; no commit/push/merge/rebase.
- P7-C and B6 uncommitted changes preserved; no revert/overwrite.
- dirty submodules ×3 and `crashes/` 115 untouched.
- P6-1B/P7-C historical closure docs unchanged.

## 8. Decision

**P7-A — CLOSED — COMPLETE — ASSET WORKFLOW VERIFIED — SCOPE-BOUND**

P7-B remains NOT STARTED and requires a separate contract/authorization.
