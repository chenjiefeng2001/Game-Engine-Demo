# P8 G1 - Native Parent-Child Core Closure

**Status**: PASS - SCOPE-BOUND
**Contract**: `docs/P8-GateA-Scene-Composition-Contract.md` (frozen)
**Scope**: Native/EditorSession Scene Composition core. No G2 UI, Prefab, or public ABI additions.

## 1. Delivered

### Scene identity

- `GameObject` now carries a persistent nonzero `uint64 SceneEntityId` separate from
  the runtime `uint32` GameObject ID.
- `Scene` owns allocation and a `SceneEntityId -> GameObject*` registry.
- Duplicate IDs, zero IDs, and exhausted allocators fail closed.
- IDs survive Capture/Instantiate, Undo/Redo, Save, and cold restart.

### Scene Composition authority

`Scene` exposes:

- `AddEntity(tree, parentId)`
- `FindByEntityId(id)`
- `GetParentEntityId(id)`
- `GetAllEntities()`
- `SetEntityParent(child, parent, KeepLocal|KeepWorld)`
- `DeleteEntity(id)`
- `ValidateComposition()`

The runtime `GameObject` parent/children graph is kept in sync by these Scene
operations. IDs and hierarchy no longer depend on root-vector position.
`EditorSession` keeps legacy root-index APIs for compatibility and adds
ID-based internal operations for the native path.

### Scene v2

- Writer emits `version: 2`, `id`, optional `parent`, Local Transform, and
  contract components.
- Writer does not emit a redundant child list.
- Reader accepts Scene v1 as root-only and upgrades on the next normal Save.
- Reader rejects:
  - duplicate IDs;
  - zero IDs;
  - unknown parent IDs;
  - self-parenting;
  - parent cycles.
- `CaptureScene` captures the complete tree via `GetAllEntities()`.
- `InstantiateScene` creates all entities first, then applies Parent References.
- `CaptureSceneById` / `LoadResult.bindingsById` preserve child sprite/script
  bindings by Scene Entity ID.
- Scene v1 fixtures and P7-B fixtures remain valid.

### Reparent

- Keep Local preserves the local Transform.
- Keep World computes:

```text
local,new = inverse(parentWorld,new) * world,old
```

- Singular or non-finite parent matrices are rejected before mutation.
- Rotation/scale are decomposed into the existing quaternion/Vec3 Transform;
  skew/perspective that cannot be represented is rejected.
- Cycle/self detection occurs before detaching the child.

### Delete and Undo

- `DeleteEntity` removes the complete subtree and all Scene ID registry entries.
- Reparent and Delete integrate with the existing P7-B Snapshot History.
- No second Undo stack was introduced.
- Undo/Redo restore hierarchy, IDs, Local Transforms, and parent relationships.

### Transform propagation

- `TransformComponent` now invalidates the owner GameObject subtree.
- Changing a parent position/rotation/scale marks all descendants dirty.
- Child World Matrices are recomputed from the new parent without requiring a
  separate child edit.

## 2. Files changed

- `engine/include/Engine/Core/GameObject/GameObject.h`
- `engine/src/Core/GameObject/GameObject.cpp`
- `engine/include/Engine/Core/GameObject/TransformComponent.h`
- `engine/src/Core/GameObject/TransformComponent.cpp`
- `engine/include/Engine/Core/Scene/Scene.h`
- `engine/src/Core/Scene/Scene.cpp`
- `engine/include/Engine/Core/Content/SceneSerializerV1.h`
- `engine/src/Core/Content/SceneSerializerV1.cpp`
- `bridge/src/EditorSession.h`
- `bridge/src/EditorSession.cpp`
- `tests/test_content/SceneCompositionTest.cpp`
- `tests/test_bridge/test_scene_composition.cpp`
- `tests/CMakeLists.txt`

No public `capi.h`, EngineHost op, Scene v1 field, or Managed UI contract was
changed.

## 3. Tests

### New native tests

`SceneComposition` (8 tests):

- hierarchy Capture/Instantiate/Serialize/Deserialize with IDs, parents, and
  World positions;
- Scene v1 root-only load and v2 upgrade on Capture;
- duplicate ID, invalid parent, self-parent, and cycle rejection;
- Keep Local / Keep World numeric behavior;
- singular-parent rejection and cycle rejection;
- descendant World invalidation after parent movement;
- subtree delete and ID cleanup;
- duplicate-id AddEntity fails closed.

`SceneHierarchySessionTest` (2 tests, EditorSession):

- parent transaction -> Undo -> Redo -> Save -> reopen with stable IDs;
- Keep World reparent, subtree delete, Undo restore.

### Full native inventory

Both Debug and Release:

```text
Native targets      : 21
Discovered          : 1218
Executed            : 1218
Passed              : 1216
Failed              : 1 (PICK-01)
Skipped             : 1 (SDK shaderc)
Managed static      : 379
CTest entries       : 22
```

`test_content`: 137 tests (136 PASS + 1 SDK SKIP).
`test_bridge`: 117/117 PASS.

### UIA regression

Real `AvaloniaEditor.exe` P7-B B6 workflow remains:

- Debug: 1/1 PASS
- Release: 1/1 PASS

This includes Transform Inspector, Undo/Redo, gizmo drags, Material edit,
Save, cold restart, Mesh GUID/Ready, and production Viewport pixels.

## 4. Known limits / handoff to G2

- Production 3D `Application::RenderProduction3DFrame()` still builds its mesh
  list from root objects only. Child meshes can be loaded by MeshRuntimeScene but
  are not yet submitted by the production 3D path.
- Picking still resolves root indices only.
- No public hierarchy ABI/ops yet; `EditorSession` exposes internal C++ methods
  used by G1 tests. G2 decides the minimal Managed ABI set and version.
- No Hierarchy UI, drag/drop, duplicate, Prefab, or Override yet.
- Child sprite/script bindings are available in `bindingsById`; the legacy
  root-index binding vector remains for old APIs.
- G2 must not treat the new Scene map as a substitute for production renderer
  and Picking traversal verification.

## 5. Git

G1 changes are uncommitted. No commit, push, reset, clean, or stash was performed.
