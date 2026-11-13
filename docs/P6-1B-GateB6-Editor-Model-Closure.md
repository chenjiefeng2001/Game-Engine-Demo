# P6-1B B6 — Editor Model Integration Final Closure

**Status**: P6-1B B6 — CLOSED — COMPLETE — EDITOR MODEL INTEGRATED — SCOPE-BOUND
**Branch/HEAD**: avalonia / `5de8f1a`（B6 未 commit/push）
**Precondition**: B5 CLOSED — PRODUCTION PIXEL VERIFIED

## 1. Gate ledger

| Gate | Result | Document |
|---|---|---|
| B6-G0 | COMPLETE — minimal map | `docs/P6-1B-GateB6-G0-Editor-Integration-Map.md` |
| B6-G1 | PASS — AssetBrowser Mesh/Material/status | `docs/P6-1B-GateB6-G1-AssetBrowser-Closure.md` |
| B6-G2 | PASS — Add Mesh to Scene | `docs/P6-1B-GateB6-G2-AddMeshToScene-Closure.md` |
| B6-G3 | PASS — Inspector Mesh diagnostics | `docs/P6-1B-GateB6-G3-Inspector-Closure.md` |
| B6-G4 | PASS — Save/reload/cold restart | `docs/P6-1B-GateB6-G4-Scene-Persistence-Closure.md` |
| B6-G5 | PASS — real UIA + production viewport | `docs/P6-1B-GateB6-G5-Editor-UIA-Viewport-Closure.md` |
| B6-G6 | PASS — integrated regression | this document |

## 2. Delivered workflow

`Asset Import (B5)` → committed current → AssetBrowser (Mesh/Material/status) → Add Mesh to Scene →
MeshComponent.meshGuid → Save Scene → close editor → relaunch/reopen → GUID restored →
MeshRuntimeScene reloads committed current → production Viewport shows Mesh.

## 3. Bridge / ABI changes

Minimal additive increment **ENGINE_HOST_ABI_VERSION = v1.10**:

- `EditorSession_GetAssetStatus` export and EngineHost op `GET_ASSET_STATUS = 32`.
- `GetAssetType` now returns `3` for Material (`2` remains Mesh).
- Op count 33; all existing op numbers/status values/`RuntimeResult` layout unchanged.
- Managed mirror updated (`EditorBridgeApi.EngineHostExpectedVersion`); legacy channel uses the new export.

No Scene format, Mesh GUID, RevisionStore, renderer, or B5 pixel contract change.

## 4. MeshComponent contract additions

- Reflection properties (existing component contract, no new inspector ABI): `meshGuid`, `meshName`, `spanCount`, `materialSlotCount`, `runtimeStatus`, `runtimeError`, `resolvedRevision`, `resolvedAssetPath`, `visible`.
- Runtime counts and status are populated by B5 `MeshRuntimeScene`; status/revision/path remain non-persistent.
- `meshGuid` write validates 32-hex; empty clears; invalid is rejected.

## 5. Tests / regression

Native (Debug/Release, current binaries):

- `test_assetimport_commit`: 22/22 PASS (G5 20 + B6 asset type/availability + Mesh GUID C ABI round-trip).
- `test_assetimport`: 148/148 PASS (incl. G6 3-channel regressions and runtime count assertions).
- `test_content`: 129 discovered (128 PASS + 1 SDK SKIP).
- `test_bridge`: 115/115 PASS (v1.10 ABI tests updated).
- `test_mesh_pixel`: 9/9 PASS Debug/Release (B5 production pixel chain unchanged).

Full native inventory (Debug == Release):

| Metric | G6 baseline | B6 result | Delta |
|---|---|---|---|
| discovered / executed | 1198 / 1198 | **1201 / 1201** | +3 native B6 tests |
| PASS | 1196 | **1199** | +3 |
| FAIL | 1 (PICK-01) | **1 (PICK-01)** | 0 |
| SKIP | 1 (shaderc) | **1 (shaderc)** | 0 |
| managed static | 378 | **379** | +1 B6 UIA test |

- PICK-01 unchanged: `test_pick_transport.cpp:231/233`.
- SDK SKIP unchanged: `ShaderCompileRule.SkippedWithoutVulkanOrShaderc`.
- Managed UIA: B6 test **1/1 PASS** in Debug and Release (real process, real controls, production FBO non-clear pixels, save/relaunch/reopen).

Builds: CMake Debug/Release full; EditorBridge Debug/Release; `editor_avalonia` Debug/Release; IntegrationTests Debug/Release.

## 6. Known limitations

- UIA requires interactive Windows desktop; non-interactive runs are Inconclusive (existing convention).
- UIA viewport evidence is production-FBO non-clear pixel count via `ViewportFrameStats`; RenderGraph pixel proof stays B5-owned.
- PICK-01 remains open; SDK SKIP remains conditional.
- Linear data maps other than normal remain unconsumed by the production shader (B5/B4 contract).
- No Material Editor / PBR / multi-material / texture preview / hierarchy editor / gizmo / undo / 3D picking / Vulkan/D3D12.
- `m_MeshCache_RHI` pointer-key exception remains out of scope.
- B6 did not change `m_MeshCache_RHI`, B5 docs, or third-party submodules.

## 7. Workspace

- B6 changes remain uncommitted; no commit/push/merge/rebase.
- dirty submodules ×3 and `crashes/` 115 preserved.
- B5 historical closure documents unchanged.

## 8. Final decision

**P6-1B B6 — CLOSED — COMPLETE — EDITOR MODEL INTEGRATED — SCOPE-BOUND**

Editor workflow now reaches the real production Viewport after save and cold restart.
