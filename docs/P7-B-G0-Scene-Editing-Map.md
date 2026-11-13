# P7-B G0 — Scene Editing Map (Read-only)

**Status**: COMPLETE — MINIMAL MAP
**Baseline**: P6-1B CLOSED; P7-C CLOSED; P7-A CLOSED; HEAD `5de8f1a`
**Scope**: only the data/edit paths P7-B depends on; no source changes.

## 1. Workspace

- Branch `avalonia`; HEAD `5de8f1a`; P6-1B/P7-C/P7-A work remains uncommitted.
- dirty submodules ×3 and `crashes/` 115 must remain untouched.
- P7-B G0 changes no source/CMake/ABI.

## 2. Scene data model (verified)

- `Scene` stores root objects in `std::vector<std::shared_ptr<GameObject>>`.
- `SceneSerializerV1` uses flat `entities[]`; it does **not** serialize parent/child hierarchy.
- Entity name is persisted; GameObject numeric ID is process-local and not serialized.
- Scene v1 entity JSON currently persists only `position` (plus sprite/script GUIDs and contract components).
- Rotation and Scale exist in `TransformComponent` but are **not serialized** by SceneSerializerV1.
- `TransformComponent` authority: internal quaternion; Euler degrees are a cached Inspector view; local matrix = T*R*S; no persisted stable entity ID.

## 3. Component/runtime paths

- Components live in `GameObject`; MeshComponent persists `meshGuid` (B5/G4).
- `EditorSession::CreateEntity/DeleteEntity/AddComponent/RemoveComponent/SetComponentProperty` mutate the edit scene directly and call `MarkDirty`.
- `SaveProject` captures the live scene, publishes one revision, clears dirty.
- `MeshRuntimeScene::Sync` observes Scene GameObjects/Components each pump; no Scene-side GPU ownership.
- Removing/deleting Entity/Component eventually removes runtime GUID references on next sync.

## 4. Editing/UI paths

- Inspector currently exposes Position X/Y/Z only; no Rotation/Scale.
- Hierarchy is a flat Entity list with create/delete/rename/select.
- Gizmo buttons exist but only call `SetGizmoMode`; there is no handle geometry, hit testing, drag transaction, or viewport manipulation (`AcceptLegacyViewportCapability` gates them to the legacy backend).
- Viewport has production frame presentation and pointer routing/pick coordinate helpers; no object 3D picking is available (P4-B deferred).

## 5. Undo/Redo feasibility

Current state has **no command/history/transaction model** and only a boolean dirty flag. Available primitives:
- `SceneSnapshot` capture + `InstantiateScene` cold reconstruction;
- Entity/component names and serializable component data;
- Mesh GUIDs persisted through Component serialization.

Recommendation:
- Use typed transactions with before/after state:
  - Transform changes: before/after position+euler+scale.
  - Create: created entity snapshot.
  - Delete: full entity snapshot (all contract components).
  - Component add/remove: component payload.
- Do not use an unbounded whole-scene snapshot history.
- History is session-only (not persisted across restart).
- Undo/Redo only restores Scene state; it must not roll back Material/Texture revisions or asset Publish.
- Save checkpoint: compare current Scene snapshot with last saved snapshot; Undo below checkpoint recomputes dirty correctly.

## 6. Gaps / decisions required

| Gap | Fact |
|---|---|
| Rotation/Scale persistence | missing in Scene v1; additive optional fields needed |
| Stable Entity identity | no persisted entity ID; current name/order only |
| Parent/Child | not serialized; do not fake hierarchy tree |
| Transform ABI | only Get/SetEntityPosition exists |
| Undo/Redo ABI | missing |
| Gizmo | absent (buttons are placeholders) |
| Gizmo viewport projection | production camera matrices not exposed to managed |
| Save checkpoint | no saved-scene baseline tracking beyond dirty bool |

## 7. Recommended implementation plan

1. Add additive Scene v1 entity fields for rotation (quaternion) and scale, with old-scene defaults; no version bump.
2. Add native typed transaction/history core and additive ABI v1.12 operations:
   Get/SetEntityTransform, Undo/Redo, CanUndo/CanRedo, Begin/Commit/Cancel transaction.
3. Transform Inspector edits an Euler-degree view; native stores authoritative quaternion.
4. Hierarchy remains flat (no unpersisted parent/child UI); undo restores entity + component snapshots.
5. Gizmo requires an overlay/handle renderer and camera projection/hit-test path; implement after transform/history contracts are proven. Do not build full 3D object picking.
6. UIA validates Inspector edits, Undo/Redo, Save/cold restart, and production Viewport transforms.

## 8. Stop conditions

- ABI v1.11 cannot express Rotation/Scale or transactions → additive v1.12 required (expected).
- Scene v1 cannot accept optional rotation/scale without breaking old assets → additive fields/defaults preserve compatibility; otherwise stop for a format decision.
- Parent/Child has no serialized contract → keep flat v1 hierarchy; do not add UI-only hierarchy.
- Gizmo cannot reuse Viewport/camera projection without a new architecture → report before implementing G4.
