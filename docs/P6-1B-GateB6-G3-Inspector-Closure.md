# P6-1B Gate B6-G3 — Inspector MeshComponent

**Status**: PASS — SCOPE-BOUND

## 1. UI

New read-only Mesh section in the Inspector (visible when selected entity has `"Mesh"`):

| Field | Binding | AutomationId |
|---|---|---|
| Mesh Name | `MeshName` | `MeshNameValue` |
| Mesh GUID | `MeshGuid` | `MeshGuidValue` |
| Span Count | `MeshSpanCount` | `MeshSpanCountValue` |
| Material Slots | `MeshMaterialSlotCount` | `MeshMaterialSlotCountValue` |
| Load Status | `MeshStatus` | `MeshStatusValue` |
| Error | `MeshError` | `MeshErrorValue` |

`RefreshInspectorFromSession` refreshes Mesh alongside Camera/RigidBody; `FrameAdvanced` calls `RefreshMeshRuntimeStatus` so status changes converge without user action.

## 2. Data contract

`MeshComponent` now implements the existing component reflection contract (no new inspector ABI):

- `meshGuid` — String, editable, 32-hex validated (empty clears).
- `meshName` — String, read-only (resolved committed path basename).
- `spanCount`, `materialSlotCount` — Int, read-only; populated when runtime load reaches Ready.
- `runtimeStatus` — String, read-only; exact `MeshRuntimeStatus` token (`NotRequested/Ready/MissingAsset/...`).
- `runtimeError`, `resolvedRevision`, `resolvedAssetPath` — read-only.
- `visible` — Bool, editable (existing field).

Status is the existing B5 runtime state mirrored into the component; no second state machine.

## 3. Evidence

- `MeshComponentContractTest.ReflectionPropertiesExposeGuidAndDiagnostics`.
- `MeshRuntimeSceneGLTest.ReadyLoadsCpuGpuAndMaterialBinding` asserts span/material counts and Ready status via reflection.
- B6 UIA test asserts Inspector GUID equals imported GUID, status becomes Ready after cold restart, and Inspector is populated from the committed project.

## 4. Not included

Material editor, texture preview, vertex/submesh editing, material slot editing, dependency editor.
