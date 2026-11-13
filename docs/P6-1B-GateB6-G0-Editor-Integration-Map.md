# P6-1B Gate B6-G0 — Editor Integration Map (Read-only)

**Status**: COMPLETE — MINIMAL MAP
**Branch/HEAD**: avalonia / `5de8f1a`
**Scope**: only the five B6 integration areas; no B1–B5 re-audit.

## 1. AssetMetaDb

- Legacy `AssetMetaDb` remains zero-production-consumer; it cannot represent Mesh/Material (type coercion to Texture).
- B6 uses the existing authoritative path instead: `EditorSession` + `ContentRegistry` + `RevisionStore` current manifest.
- Decision: **do not revive AssetMetaDb**.

## 2. AssetBrowser

- `editor_avalonia/MainWindow.axaml` Asset Browser tab: `AssetList` (AutomationId already present), type filter, search, import/rename.
- Data: `MainViewModel.ReloadCollectionsFromSession` enumerates `GetCounts` + per-index `GetAssetPath/Type/Guid`.
- Gap: `GetAssetType` returned `2` for both Mesh and Material; managed mapped `2` to `Unknown`; filter had only All/Texture/Script.
- No Mesh/Material rows, no availability state.

## 3. Scene / MeshComponent

- `MeshComponent` is registered as `"Mesh"` in `ComponentRegistryGo`; persists `meshGuid`; Scene v1 unchanged.
- Existing C ABI supports CreateEntity + AddComponent + Get/SetComponentProperty, but MeshComponent had zero reflection properties and there was no Mesh GUID property.
- No managed Add-to-Scene path.

## 4. Inspector

- Inspector is hardcoded to Transform/Camera/RigidBody; no Mesh section.
- Component property read/write uses the existing reflection contract (`GetPropertyCount/Desc/Value/SetPropertyValue`), no descriptor enumeration API is required for fixed fields.
- Decision: expose Mesh diagnostics through the existing component-property channel (no new inspector ABI).

## 5. Managed Bridge / Runtime Status

- `EditorSession`/`EngineHost` already expose asset enumeration, entity CRUD, component add/property, save, warnings/errors, and production presentation frame Fetch.
- EngineHost op space had no asset availability op and no component enumeration; component enumeration was not needed for B6.
- Mesh runtime status is mirrored into `MeshComponent` by `MeshRuntimeScene::Sync`; therefore it can be read through component properties once MeshComponent exposes them.
- Existing UIA infrastructure: real Avalonia process + Windows UI Automation (`tests/EditorAvalonia.IntegrationTests`), existing `--m1import-host` temp-project bootstrap (hardcodes `assets/gp01/manifest.json` + `Main.scene`).
- Viewport production pixels are available to the editor as `ViewportDisplayReceipt.NonClearPixels` from the real presentation FBO fetch; no new pixel API needed for B6 UIA observation.

## 6. B6 implementation decisions

1. Additive ABI increment **v1.10**:
   - `EditorSession_GetAssetStatus` export (committed asset availability).
   - `ENGINE_HOST_OP_GET_ASSET_STATUS = 32`; `OP_COUNT = 33`.
   - `GetAssetType`: Material now returns `3` (Mesh remains `2`); no struct/RuntimeResult layout change.
2. `MeshComponent` gains reflection properties:
   `meshGuid` (editable String), `meshName`, `spanCount`, `materialSlotCount`, `runtimeStatus`, `runtimeError`, `resolvedRevision`, `resolvedAssetPath` (read-only), `visible` (Bool).
3. Add to Scene is implemented as managed orchestration of existing C ABI:
   CreateEntity → AddComponent("Mesh") → SetComponentProperty("meshGuid"); failure rolls the entity back.
4. No new Scene format, GUID semantics, RevisionStore behavior, renderer architecture, or second Mesh identity.

## 7. Constraints retained

- No `m_MeshCache_RHI`/renderer refactor.
- No Material Editor/PBR/multi-material expansion.
- No Undo/Redo, hierarchy editor, gizmo, 3D picking, Vulkan/D3D12.
- UIA remains real-process Windows UI Automation (no Avalonia.Headless).
