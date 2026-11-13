# P8 G0 - Scene Composition Map

**Status**: COMPLETE - ACCEPTED - READ-ONLY - GATE A CONTRACT FROZEN
**Scope**: investigation only. No source, test, CMake, ABI, Scene, or asset changes.
**Baseline**: P7-B CLOSED; ABI v1.12; Scene v1.

## 1. Workspace and stage state

| Item | Observed |
|---|---|
| Branch | `avalonia` |
| HEAD | `5de8f1a84045247839386865eb70abdbeff874fe` |
| Upstream divergence | `origin/avalonia...HEAD` = 0 / 0 |
| Working tree | 76 status entries; P7-B implementation and docs remain uncommitted |
| Dirty third-party modules | `third_party/d3d12ma`, `openal-soft`, `vma` |
| Preserved local crash artifacts | untracked `crashes/` |
| ABI | v1.12, `ENGINE_HOST_OP_COUNT=47` |
| Last recorded native baseline | 1208 discovered / 1208 executed; 1206 PASS, 1 PICK-01, 1 SDK SKIP |
| Last recorded managed static count | 379 |
| Last recorded P7-B UIA | 1/1 Debug, 1/1 Release |

This G0 did not clean, reset, stash, commit, or push the workspace.

## 2. Source map

| Area | Entry point | Current fact |
|---|---|---|
| Scene container | `engine/include/Engine/Core/Scene/Scene.h`, `engine/src/Core/Scene/Scene.cpp` | Root object vector; runtime hierarchy exists; recursive helpers exist |
| Entity | `engine/include/Engine/Core/GameObject/GameObject.h`, `engine/src/Core/GameObject/GameObject.cpp` | Runtime `uint32` ID, parent pointer, child shared_ptr list |
| Transform | `engine/include/Engine/Core/GameObject/TransformComponent.h`, `engine/src/Core/GameObject/TransformComponent.cpp` | Local/world matrices, parent transform pointer, exploratory world math |
| Editor Scene format | `engine/include/Engine/Core/Content/SceneSerializerV1.h`, `engine/src/Core/Content/SceneSerializerV1.cpp` | Flat `entities[]`; no parent, Scene ID, or Prefab metadata |
| Generic Scene serializer | `engine/src/Core/Scene/Serializer.cpp` | Supports recursive children, but is a different format and is not the Editor/RevisionStore contract |
| Editor/session ABI | `bridge/src/EditorSession.cpp`, `bridge/src/EngineHost.cpp`, `bridge/include/editor_bridge/capi.h` | All entity APIs use root-vector indices; no hierarchy/Prefab ops |
| Mesh runtime | `engine/src/Core/RHI/MeshRuntimeScene.cpp` | Collects Mesh components recursively through children |
| Production 3D | `engine/src/Core/Application.cpp` | Builds the production mesh list from root objects only |
| Asset identity | `engine/include/Engine/Core/Content/ContentAsset.h` | `AssetType` is Texture/Script/Mesh/Material; GUID is `ResourceGUID` |
| Revision commit | `engine/src/Core/Content/RevisionStore.cpp` | Atomically publishes manifest+scene revision; does not hash arbitrary asset files |
| Legacy Undo | `engine/src/Editor/UndoSystem.cpp` | ImGui-editor command stack; not the Avalonia P7-B snapshot history |
| Avalonia UI | `editor_avalonia/ViewModels/MainViewModel.cs`, `editor_avalonia/MainWindow.axaml` | Flat entity list; no hierarchy tree or Prefab UI |

## 3. Entity architecture and identity

### 3.1 Storage and traversal

- `Scene` owns `std::vector<std::shared_ptr<GameObject>> m_Objects` as the root list
  (`Scene.h:161`).
- `GetObjectCount()` returns only root objects (`Scene.h:113`).
- `GetTotalObjectCount()` recursively counts root objects and descendants
  (`Scene.cpp:77`).
- `AddObject()` appends to the root list only (`Scene.cpp:31`).
- `FindObject()` and `FindByID()` search recursively (`Scene.cpp:136`,
  `Scene.cpp:144`).
- `Scene::CollectRenderCommands()`, `Scene::OnCreate()`, `Scene::Update()`,
  `Scene::OnDestroy()`, and physics snapshot collection recurse through children.
- The Editor-facing indexing path is different: `EditorSession` repeatedly uses
  `m_EditScene->GetObjects()[index]` and `GetObjectCount()`, i.e. root index only
  (`EditorSession.cpp:461`, `EditorSession.cpp:485`, `EditorSession.cpp:567`).

### 3.2 Runtime Entity ID

`GameObject` assigns a process-global monotonic `uint32` in its constructor:

```cpp
static uint32 GetNextID() { static uint32 s_NextID = 1; return s_NextID++; }
```

(`GameObject.h:25`, used by constructors at `GameObject.cpp:14` and
`GameObject.cpp:18`).

Properties:

- assigned at construction, never loaded from a Scene;
- not persisted by SceneSerializerV1;
- not stable across Editor restart, Undo/Redo Scene replacement, or Play clone;
- can be reused only at process lifetime boundaries, not as a durable identity;
- suitable for current-session Picking / lookup, not for Prefab mappings.

`EditorSession` does not use this ID as its ABI identity. Its ABI indexes are
root-vector positions (`EditorSession.cpp:485-604`). Picking converts a runtime
ID to a root index by `Scene::FindByID` plus a root-list scan
(`EngineHost.cpp:2781-2793`).

### 3.3 Binding vectors

Sprite/Script bindings are stored in `m_Bindings`, parallel to the root object
vector (`EditorSession.cpp:2851-2853`). Deleting, replacing, or reordering roots
rebuilds this vector by index. Any hierarchy or Prefab metadata stored in the
same way would be invalidated by reparent/undo/instantiate operations.

### 3.4 G0 conclusion

A persistent Scene identity is required before stable hierarchy, Prefab local-ID
mapping, Override targeting, and Undo restoration can be specified. The current
`uint32` runtime ID cannot serve that role.

## 4. Parent-child capability

### 4.1 Runtime support only

A runtime hierarchy already exists:

- `GameObject::m_Parent` is a raw pointer (`GameObject.h:256`).
- `GameObject::m_Children` is a shared_ptr list (`GameObject.h:257`).
- `SetParent()` detaches from the old parent and sets the Transform parent, but
  does **not** insert the object into the new parent's child list
  (`GameObject.cpp:87-108`).
- `AddChild()` calls `SetParent()` and then appends to `m_Children`
  (`GameObject.cpp:111-119`).
- `RemoveChild()` erases the list entry and clears the parent
  (`GameObject.cpp:122-137`).

There is no single enforced authority: `SetParent()` and `AddChild()` can leave
`m_Parent` and `m_Children` inconsistent if callers use them differently. The
old ImGui hierarchy panel shows this risk: its child creation calls both
`AddChild()` and `Scene::AddObject()`, making the child both a descendant and a
root (`SceneHierarchyPanel.cpp:210-214`).

### 4.2 Missing validation

There is no cycle, self-parent, ancestor, cross-Scene, or component-ownership
validation in `GameObject::SetParent()` or `Scene`. `FindByID()` and the World
matrix recursion can recurse indefinitely on a malformed parent cycle.

### 4.3 Delete behavior

`Scene::RemoveObject()` removes a root from `m_Objects`, or a child from its
parent list (`Scene.cpp:38-65`). Removing a root parent releases the subtree via
the parent's shared_ptr ownership. Existing GP01 evidence explicitly expects
descendants to leave the scene with the parent
(`tests/test_gp01/GP01EditorWorkflowTest.cpp:77-109`).

There is no promotion of children to roots. Therefore the Gate A proposal to
promote children on parent deletion is not the current behavior; choosing it
requires new behavior and new tests.

### 4.4 Missing persistence

SceneSerializerV1 has no parent field (`SceneSerializerV1.h:45-56`).
`CaptureScene()` iterates only `scene.GetObjects()` roots
(`SceneSerializerV1.cpp:139-168`). A hierarchy cannot survive Save, Undo/Redo,
Play clone, or cold restart today.

### 4.5 G0 conclusion

The runtime tree is a useful starting point, but it is not a complete hierarchy
system: identity, validation, persistence, Editor traversal, and deletion policy
are absent. G1 must not expose the current two-way structure as the contract
without first freezing one authoritative representation.

## 5. Transform and matrix contract

### 5.1 Convention

`TransformComponent` builds:

```cpp
local = translation * rotation * scaling;
world = parentWorld * local;
```

(`TransformComponent.cpp:120-138`). It uses glm matrices, which are
column-major and column-vector (`glm::vec4 result = w * p` at
`TransformComponent.cpp:163-169`). The effective contract is:

```
M_world = M_parent * M_local
p_world = M_world * p_local
```

Euler values are converted to/from a quaternion
(`TransformComponent.cpp:78-96`). Non-uniform and negative scale are stored as
Vec3 and multiply the matrix normally; they are not constrained by Transform.

### 5.2 Dirty propagation defect

`m_Dirty` is set by local setters (`TransformComponent.h:33-39`,
`TransformComponent.h:50`, etc.). `RecalculateMatrices()` recalculates the
parent only when the **child** is already dirty:

```cpp
if (m_Parent) {
    m_Parent->RecalculateMatrices();
    ...
}
```

(`TransformComponent.cpp:131-135`).

Changing only the parent does not mark its children dirty. A clean child can
therefore return a stale `m_WorldMatrix` after the parent moves. P8-G1 must add
downward dirty propagation (or a generation/version traversal) and covering
tests; this is a P0 correctness issue, not an optimization.

### 5.3 Singular inverse

`WorldToLocal()` and `WorldToLocalDir()` call `glm::inverse()` without checking
determinant or invertibility (`TransformComponent.cpp:179-194`). Keep World
reparent must reject singular parent matrices before applying local values;
otherwise it produces NaN/Inf.

### 5.4 Current Editor Transform behavior

`EditorSession::GetEntityTransform()` reads the root object's Transform and
returns position, Euler, scale (`EditorSession.cpp:567-578`).
`SetEntityTransform()` writes local position/Euler/scale
(`EditorSession.cpp:590-611`). No World Transform read/write API exists.

## 6. Runtime World Transform and rendering path

### 6.1 Editor session edits

All Editor ABI entity operations use root indices and modify Transform directly.
Undo snapshots call `CaptureScene()` and rebuild through `InstantiateScene()`
(`EditorSession.cpp:524-554`). Both paths are root-only and have no parent
metadata.

### 6.2 Mesh loading

`MeshRuntimeScene::Sync()` iterates roots but `CollectMeshComponents()`
recursively visits children and binds child Mesh components
(`MeshRuntimeScene.cpp:30-40`, `MeshRuntimeScene.cpp:59-68`). Runtime asset
binding therefore already has a child path.

### 6.3 Production 3D drawing

`Application::RenderProduction3DFrame()` builds `meshObjects` with:

```cpp
for (const auto &obj : scene.GetObjects()) { ... }
```

(`Application.cpp:944-952`). It does not recurse into children. Both Direct and
RenderGraph paths consume that same root-only list
(`Application.cpp:1025-1028`, `Application.cpp:1058`).

Consequence: a child Mesh can be loaded/bound by MeshRuntimeScene but still not
appear in the production 3D Viewport. G1 must flatten production 3D candidates
(or make the renderer hierarchy-aware) before claiming hierarchy rendering.

### 6.4 Other runtime paths

- `Scene::CollectRenderCommands()` recurses (`Scene.cpp:233-246`), covering the
  2D/Scene render-command path.
- `PhysicsRuntime2D` snapshots roots and descendants
  (`PhysicsRuntime2D.cpp:21-38`).
- `EditorHost::ResolveSceneIndexForEntityId()` resolves only root indices
  (`EngineHost.cpp:2781-2793`), so child Picking currently cannot select a child.

## 7. Scene serialization compatibility

### 7.1 Current v1 shape

`SerializedEntity` contains name, position, optional rotation/scale, sprite/script
GUIDs, and contract components (`SceneSerializerV1.h:45-56`). There is no
Scene ID, parent ID, local ID, Prefab metadata, or override data.

Writer emits `format: "engine.scene"` and `version: 1`
(`SceneSerializerV1.cpp:29-54`).

Reader:

- rejects future numeric versions (`SceneSerializerV1.cpp:64-76`);
- ignores unknown entity fields;
- ignores structurally malformed component entries;
- turns unknown registered component names into warnings, not a hard load failure
  (`SceneSerializerV1.cpp:237-247`).

`CaptureScene()` captures only root objects and root-parallel bindings
(`SceneSerializerV1.cpp:139-168`). `InstantiateScene()` creates every serialized
entity as a root with `outScene.AddObject(obj)`
(`SceneSerializerV1.cpp:190-253`).

### 7.2 Version decision

Additive optional fields appear technically possible in Scene v1 because the
reader tolerates unknown fields and defaults missing fields. However, a new
parent/ID/Prefab contract must also guarantee:

- old Scenes load as root-only with generated stable-in-session IDs;
- Undo snapshots preserve the same IDs;
- Save writes IDs and parents;
- old readers are not required to understand the new semantics (forward-read
  compatibility is not provided by current v1);
- no silent loss of parent/Prefab data occurs in `CaptureScene()`.

If those conditions cannot be met without changing existing field meanings, Gate
A must freeze a Scene v2 migration rather than force additive v1. This decision
is not made by G0.

### 7.3 Generic serializer is not the contract

`engine/src/Core/Scene/Serializer.cpp` does serialize `children` recursively
(`Serializer.cpp:96-123`, `Serializer.cpp:207-242`) and has a recursive load.
But it uses a different root shape, Euler angles, `JsonSerializer` component
factories, and is not used by `EditorSession` or `RevisionStore`. It is not a
drop-in replacement for the committed Scene contract and must not be used to
bypass Prefab/Scene validation.

## 8. Asset pipeline and Prefab storage map

### 8.1 Existing asset model

`AssetType` is `Texture=0, Script=1, Mesh=2, Material=3`
(`ContentAsset.h:33`). Manifest type strings are strict; unknown strings reject
the manifest (`ContentAsset.cpp:187-193`). GUIDs are 128-bit UUID v4 values
(`ResourceGUID.cpp:12-22`), stored in Scene JSON as hex.

`ContentRegistry` resolves GUID to project-relative path
(`ContentAsset.h:85`); `LoadManifest` validates version, GUID/path uniqueness,
path normalization, and type strings (`ContentAsset.cpp:114-250`).

### 8.2 RevisionStore

`RevisionStore::Publish()` writes candidate manifest+scene, re-loads and
validates the candidate, then atomically replaces `current`
(`RevisionStore.cpp:321-424`, `RevisionStore.cpp:472-555`). It hashes the
manifest and Scene bytes in the current pointer (`RevisionStore.cpp:475-476`).
It does not hash arbitrary asset files such as a Prefab payload.

Material edits show the current asset pattern: validate a new material payload,
write a new content-addressed `.material.json`, rebind the same GUID, publish a
new manifest revision with the existing Scene bytes
(`EditorSession.cpp:2494-2557`). A failed publish leaves the new file as a
candidate artifact but does not corrupt the previous current revision.

### 8.3 Prefab storage options

| Option | Shape | Reuse | Main risk |
|---|---|---|---|
| A | Content-addressed Prefab file + manifest entry (`AssetType::Prefab`) + existing `RevisionStore.Publish` with unchanged Scene bytes | Best match to Material/Mesh identity and manifest semantics | Asset file bytes are outside the revision hash; failed publish can leave orphans |
| B | Embed Prefab data in manifest | One atomic pointer | Large manifests; poor asset browsing; violates current separation |
| C | Separate Prefab revision store | Strong Prefab history | Second commit system, incompatible with current `current` semantics |
| D | Treat Prefab as a Scene file alias | Minimal writer work | Violates committed asset identity and dependency rules |

G0 recommends Option A as the preferred candidate for Gate A, not a frozen
decision. The Prefab Reader must be strict (format/version, local-ID uniqueness,
parent validity, cycles, component allowlist), analogous to
`ReadMaterialAssetJson()` (`MaterialAssetReader.cpp:82-120`). Unknown
components or unknown required fields must reject, not silently drop.

### 8.4 Prefab and Scene commit boundary

The current system cannot atomically commit a Prefab asset byte change and a
Scene metadata change as one transaction. `RevisionStore` commits manifest+Scene;
the Prefab file is a separate file referenced by the manifest. Apply must
therefore be specified as staged publication plus Instance metadata update, with
an explicit recovery/conflict state if the second phase fails. It must not be
labelled globally atomic without new infrastructure.

## 9. Undo/Redo compatibility

Avalonia uses the P7-B snapshot history, not the legacy `engine/src/Editor/
UndoSystem.cpp` command stack.

Current behavior:

- `BeginSceneEdit` captures `CaptureSceneJson()` (`EditorSession.cpp:615-621`);
- `CommitSceneEdit` stores before/after JSON, cap 64
  (`EditorSession.cpp:624-635`);
- `UndoSceneEdit` / `RedoSceneEdit` call `ApplySceneJson`
  (`EditorSession.cpp:654-673`);
- `ApplySceneJson` deserializes and rebuilds the whole Scene through
  `InstantiateScene` (`EditorSession.cpp:530-554`).

Consequences for P8:

- parent/children, Scene IDs, Prefab instance metadata, and Override data must
  all be represented in `CaptureSceneJson()` before any hierarchy operation can
  be undoable;
- `ApplySceneJson` regenerates GameObjects, so runtime IDs change today; Prefab
  local-ID mappings and selection must not depend on them;
- bindings are root-index parallel and must become identity-based if hierarchy
  replacement is supported;
- `m_SceneHistory` stores no explicit entity references, so it avoids dangling
  pointers but currently achieves that by replacing the whole Scene;
- 64 snapshots of a deep subtree can be large; G1/G3 should measure/bound this;
- Prefab asset Publish remains outside Scene History; Apply needs an explicit
  cross-domain boundary.

The legacy `UndoSystem` is separate and should not be wired into Avalonia P8
without a dedicated decision.

## 10. ABI extension map

### 10.1 Current ABI

`EditorSession_*` exposes entity operations by root index and asset operations by
asset index. EngineHost op values 1-46 are frozen; v1.12 count is 47
(`capi.h:480`, `capi.h:551-636`). There are no Parent, Children, Reparent,
Duplicate, Prefab, or Override operations.

### 10.2 Candidate additions

| Capability | Candidate ABI shape | Decision needed |
|---|---|---|
| Stable identity | `GetEntityId(index)`, `FindEntityById(id)` | Restore IDs after Undo/load; index semantics |
| Parent | `GetEntityParent(index)`, `GetEntityChildren(index)` | Return IDs or flattened indexes |
| Reparent | `SetEntityParent(child, parent, mode)` | Keep Local/World, cycle failure, atomic transaction |
| Flattened traversal | `GetEntityCount` semantics, `GetEntityDepth` | Root-index compatibility |
| Duplicate | `DuplicateEntity(index, subtree)` | New IDs, parent placement, single transaction |
| Prefab create | `CreatePrefabFromSubtree(rootIndex, path)` | Asset GUID/revision and Publish |
| Prefab instantiate | `InstantiatePrefab(assetIndex, parentIndex)` | Local-ID mapping and rollback |
| Overrides | Get/Apply/Revert/Resolve | Override identity and conflict persistence |
| Source update | Refresh/Reconcile instance | Three-way base/source/instance |

All new operations must reject invalid indices, invalid hierarchy, unknown
Prefab components, and stale revisions with typed failures. Existing op numbers
must not be reused or renumbered.

### 10.3 Index versus stable handle

The current ABI uses array indices for entity access. A hierarchy changes the
meaning of a flat root index and makes selection unstable across reparent. G0
proposes two candidate designs for Gate A:

1. **Compatibility index**: define entity index as DFS order over the scene tree,
   keep existing op numbers, and add stable-ID ops for Prefab/Override. Old flat
   scenes retain the same index order.
2. **ID-first API**: add a stable handle for all new operations and gradually
   migrate old indexes. Larger ABI/UI change but stronger identity.

G0 recommends investigating option 1 first for minimal ABI churn, without
freezing it. The ABI version number is intentionally not specified in this
document; it must be derived from the chosen operations.

## 11. Risk register

| Priority | Risk | Evidence | Consequence | Required control |
|---|---|---|---|---|
| P0 | Runtime Entity ID is not persistent | `GameObject.h:25`, `GameObject.cpp:14-18` | Prefab mapping/Overrides break after restart/Undo | Add persisted Scene ID and Prefab Local ID |
| P0 | SceneSerializer captures roots only | `SceneSerializerV1.cpp:139-168` | Save/cold restart/Undo drops hierarchy | Parent/ID-aware snapshot writer + reader |
| P0 | No cycle/self/ancestor validation | `GameObject.cpp:87-108` | Infinite World recursion / corrupted tree | Native validation before reparent/load |
| P0 | World matrix does not dirty children | `TransformComponent.cpp:120-157` | Child meshes use stale transforms | Downward dirty propagation + tests |
| P0 | Production 3D list is root-only | `Application.cpp:944-952` | Child Mesh is loaded but not rendered | Flatten 3D traversal or hierarchy renderer |
| P0 | Parent delete policy is delete-subtree | `Scene.cpp:38-65`, `GP01EditorWorkflowTest.cpp:77-109` | Gate A "promote children" would change behavior | Freeze delete policy explicitly |
| P0 | Prefab Apply is cross-domain | `RevisionStore.cpp:321-323`, material pattern `EditorSession.cpp:2537-2541` | Cannot claim atomic Asset+Scene Apply | Two-phase commit/recovery state or narrowed Apply |
| P1 | Parent/children can diverge | `GameObject.cpp:87-137` | UI tree differs from Transform parent | One authoritative parent relation |
| P1 | m_Bindings is root-index parallel | `EditorSession.cpp:2851-2853` | Reparent/reorder corrupts sprite/script binding | Identity-keyed bindings |
| P1 | Scene history cap is 64 whole-scene JSONs | `EditorSession.cpp:624-635` | Large subtree snapshots consume memory | Measure/bound or typed deltas later |
| P1 | Asset bytes are outside revision hash | `RevisionStore.cpp:472-476` | Orphan candidate Prefab file after failed Publish | Content-addressed path + cleanup policy |
| P1 | Picking resolves roots only | `EngineHost.cpp:2781-2793` | Children cannot be picked in production | Hierarchy-aware selection |
| P1 | Two serializers differ | `SceneSerializerV1.cpp` vs `Scene/Serializer.cpp:96-123` | Accidental Prefab/Scene format split | One strict committed contract |
| P2 | Legacy ImGui hierarchy logic is incomplete | `SceneHierarchyPanel.cpp:180-273` | Reusing it introduces root/child duplication | Do not port; build AvEditor path |
| P2 | AssetBrowser type mapping is hardcoded 0-3 | `MainViewModel.cs:1979-1993` | Prefab asset invisible until additive type path is added | Additive AssetType/UI map |

## 12. Gate A decision proposal

Gate A should resolve these items before G1 coding:

1. **Scene identity representation**: `ResourceGUID`-style 128-bit scene ID,
   separate 64-bit counter, or another stable value. Persist it in Scene v1 or
   move to v2. Define generation, duplicate, Undo restore, and collision rules.
2. **Prefab Local ID**: distinct ID domain and representation; mapping to/from
   Scene IDs; behavior when source entities are deleted/reordered.
3. **Hierarchy authority**: prefer a single persisted parent reference with
   children derived by traversal, rather than persisting both parent and child
   lists. Existing runtime `m_Children` can remain a cache only with invariant
   validation.
4. **Scene v1 fields**: optional `id`, `parent`, and Prefab metadata in flat
   `entities[]`; or Scene v2. Define old-scene fallback and forward-read policy.
5. **ABI index model**: keep DFS-index compatibility for existing ops or add a
   stable handle-first API. Freeze new op numbers only after this choice.
6. **Delete parent behavior**: keep current delete-subtree, or promote children
   with Keep World. The current code and tests implement delete-subtree.
7. **Reparent semantics**: default Keep World or Keep Local; singular parent
   rejection; one transaction per drag.
8. **World transform path**: downward dirty propagation and hierarchy-aware
   production 3D rendering; no child mesh may be silently absent.
9. **Prefab asset format**: chosen extension/path, `AssetType::Prefab` value,
   strict Reader/Writer, local-ID rules, source revision identity, and atomic
   publication. Option A (content-addressed file + manifest) is the preferred
   candidate, not frozen.
10. **Instance/Override storage**: side table keyed by Prefab GUID/root Scene
    ID/local-ID map, versus per-entity metadata. Override field identity must be
    Local ID + component type + field path.
11. **Apply/Revert boundary**: Asset Publish versus Scene History. Freeze the
    two-phase recovery state or narrow Apply until cross-domain atomicity exists.
12. **Persistence of conflicts/status**: Base/Source/Instance data and Conflict
    state must survive cold restart if G7 is in scope.

## 13. G0 exit assessment

### Resolved by G0

- Scene root shape, runtime hierarchy, and recursive helper behavior.
- Runtime ID lifecycle and why it cannot be a persistent identity.
- Exact flat serialization and root-only snapshot behavior.
- Matrix convention, current World formula, dirty propagation gap, and singular
  inverse risk.
- Root-only production 3D and Picking paths versus recursive MeshRuntimeScene.
- Existing AssetType/manifest/RevisionStore semantics and the lack of
  cross-domain atomicity.
- P7-B snapshot history boundaries and the separate legacy UndoSystem.
- ABI v1.12 surface and candidate additive operations.

### Not resolved by G0

- Whether Scene v1 additive fields are sufficient or Scene v2 is required.
- Final Scene/Prefab ID representation and ownership.
- Final parent delete policy and reparent default.
- Final Prefab file path/format and Apply transaction protocol.
- Final ABI version/op numbers and index-versus-handle choice.

### Exit condition

G0 is complete because every implementation area has a real source entry point,
and the unresolved items are explicit Gate A decisions. G1 should not start
until C1-C9 in `docs/P8-GateA-Scene-Composition-Contract.md` are frozen.

**Next step**: Gate A contract is recorded in
`docs/P8-GateA-Scene-Composition-Contract.md`; G1 implementation has not
started.
