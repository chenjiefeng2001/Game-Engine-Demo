# P8 Gate A - Scene Composition Contract

**Status**: FROZEN - IMPLEMENTATION NOT STARTED
**Precondition**: P8-G0 COMPLETE / ACCEPTED; P7-B CLOSED; ABI v1.12.
**Basis**: `docs/P8-G0-Scene-Composition-Map.md`.
**Scope**: this document freezes the P8 contract. It does not modify source, tests, CMake, ABI, Scene files, or assets.
**Change rule**: changes to C1-C10 require an explicit contract revision; they must not be invented during G1 implementation.

## 1. Frozen decisions

| Area | Frozen result |
|---|---|
| Scene Entity ID | Independent persistent nonzero `uint64` |
| Prefab Local ID | Independent nonzero `uint64`, unique inside one Prefab |
| Runtime GameObject ID | Process-local identity only |
| Hierarchy authority | Scene Composition Model |
| Runtime hierarchy | Projection built/synchronized from the Scene Model |
| Scene format | Scene v2; Scene v1 remains readable |
| Parent relation | Exactly one persisted Parent Reference |
| Reparent | Keep Local and Keep World |
| Singular parent matrix | Keep World fails closed |
| Parent delete | Delete subtree |
| Duplicate | New IDs; internal references remapped |
| Prefab | Committed Asset using existing RevisionStore semantics |
| Instance identity | Root Scene Entity ID |
| Override identity | Prefab Local ID + Component Type + Field Path |
| Revert | Scene Snapshot Transaction |
| Apply | Recoverable two-phase cross-domain operation; not globally atomic |
| ABI | Existing v1.12/ops 37-46 preserved; additive Scene Entity ID APIs |
| Undo/Redo | Existing P7-B Snapshot History only |
| Nested Prefab | Deferred |
| Prefab Variant | Deferred |

## 2. C1 - Scene Entity Identity

Scene Entity ID and Runtime GameObject ID are separate domains.

| Identity | Purpose | Lifetime |
|---|---|---|
| Runtime GameObject ID | Runtime object lookup / current Picking ID | Process lifetime |
| Scene Entity ID | Persistent Scene identity | Scene lifetime |
| Prefab Local ID | Entity identity inside one Prefab asset | Prefab asset lifetime |

Frozen rules:

1. Scene Entity ID is a nonzero `uint64`, unique within one Scene.
2. Prefab Local ID is a nonzero `uint64`, unique within one Prefab.
3. Scene Entity IDs are persisted in Scene files.
4. Prefab Local IDs are persisted in Prefab files.
5. Runtime `GameObject` IDs are never persisted and are reassigned on every
   load, Play clone, or Undo Scene replacement.
6. An allocator must prevent overflow and duplicate allocation.
7. A loader must reject duplicate Scene Entity IDs, duplicate Prefab Local IDs,
   zero IDs used as real IDs, and duplicate/missing mappings.
8. Scene Entity ID is not an asset GUID and is not required to be globally
   unique across Scenes.
9. Prefab instantiation creates a persisted mapping:
   `Prefab Local ID -> Scene Entity ID`.
10. Runtime GameObject IDs are not valid Prefab, Override, or cross-session
    identities.

The current `GameObject::GetNextID()` / `uint32 m_ID` implementation
(`engine/include/Engine/Core/GameObject/GameObject.h:25`,
`engine/src/Core/GameObject/GameObject.cpp:14-18`) remains runtime-only.

## 3. C2 - Single Hierarchy Authority

The Scene Composition Model is the only authority for composition. Runtime
GameObject parent/children are a projection, not a second editable truth.

```text
Scene Composition Model
  Entity ID
  Parent Entity ID
  Local Transform
  Components
        |
        v
Runtime GameObject Graph
        |
        v
MeshRuntimeScene / Production Renderer / Picking
```

Frozen rules:

1. Parent Reference is the only persisted hierarchy relationship.
2. Children are derived by querying Parent References or an indexed runtime
   projection; a second persisted Child List is forbidden.
3. Runtime `GameObject::m_Parent` / `m_Children` may remain for execution, but
   they must be built and synchronized from the Scene Model.
4. EditorSession, Save/Load, Undo/Redo, ABI identity, and Prefab mapping must all
   use the same Scene Entity identity.
5. Avalonia, Runtime GameObject, and ABI must not maintain independent,
   conflicting hierarchy authorities.
6. Sprite/Script bindings and any future instance metadata must be keyed by
   Scene Entity ID, not by root-vector position. The existing root-parallel
   `m_Bindings` model (`bridge/src/EditorSession.cpp:2851-2853`) must not be
   extended as the P8 identity model.
7. A parent must belong to the same Scene; self-parenting and cycles are
   rejected before mutation.

The current runtime implementation has two-way parent/children state, no cycle
validation, and `SetParent()` does not insert into the new parent's child list
(`engine/src/Core/GameObject/GameObject.cpp:87-137`). G1 must not expose that
incomplete two-way state as the editable contract.

## 4. C3 - Scene Serialization v2

Scene v2 is the new composition format. Scene v1 remains a read-only
compatibility path.

### 4.1 Versioning

- Scene v1 reader remains available.
- A Scene v1 file loads with every entity treated as a Root.
- Scene v2 writer emits stable Entity IDs, Parent References, Local Transforms,
  and contract component data.
- Scene v2 reader strictly validates duplicate IDs, invalid parents,
  self-parenting, and cycles.
- Scene v2 writer never writes a redundant Child List.
- Scene v1 -> v2 upgrade happens through normal Load/Save whenever the user
  saves; G1 must not automatically rewrite user disk files during investigation.
- Failure cannot produce a half-valid Scene.

### 4.2 Logical v2 entity record

```text
Entity
  entityId          nonzero uint64, unique
  parentId          optional; absent/null = Root; must resolve in same Scene
  position          [x,y,z]
  rotation          [x,y,z,w] optional; default identity
  scale             [x,y,z] optional; default one
  spriteGuid        optional asset GUID
  scriptGuid        optional asset GUID
  components[]      stable type + serialized data
```

Exact JSON key spelling is an implementation detail but the logical fields and
their meanings are frozen. The v2 reader must reject invalid graph structure,
not silently drop an invalid parent or entity. The Prefab format has its own
independent `formatVersion`; it is not a Scene file alias.

### 4.3 Compatibility

- Existing Scene v1 flat fixtures must still load.
- Existing P7-B rotation/scale optional fields remain valid.
- A v1 load assigns new persistent Scene IDs in memory; the user's file is
  rewritten as v2 only when Save is requested.
- No silent change to old v1 field meanings is allowed.

## 5. C4 - Parent, Reparent, and Delete

### 5.1 Set Parent

- Parent must be in the same Scene.
- Self-parenting is rejected.
- Any cycle or ancestor relationship is rejected before mutation.
- A failed reparent leaves the Scene unchanged.

### 5.2 Reparent modes

| Mode | Frozen behavior |
|---|---|
| Keep Local | Preserve Local Transform; World changes with the new parent |
| Keep World | Preserve World Transform; recompute new Local Transform |

The matrix contract is:

```text
M_world = M_parent * M_local
M_local,new = inverse(M_parent,new) * M_world,old
```

If the new parent's World Matrix is singular or non-finite, Keep World is
rejected and the Scene is unchanged.

### 5.3 Dirty propagation

G1 must replace the current child-staleness behavior. When a parent Transform
changes, every descendant's World Matrix must be invalidated and recomputed.
The current recursive recalculation only happens when the child itself is dirty
(`engine/src/Core/GameObject/TransformComponent.cpp:120-157`).

### 5.4 Delete

- Deleting a Parent deletes the entire subtree by default.
- This preserves the existing `Scene::RemoveObject()`/GP01 subtree behavior
  (`engine/src/Core/Scene/Scene.cpp:38-65`,
  `tests/test_gp01/GP01EditorWorkflowTest.cpp:77-109`).
- Delete Subtree and Reparent are each one Scene transaction.
- Undo/Redo restores the complete hierarchy and Transform state.
- Delete-parent-then-promote-children is explicitly not part of P8.

## 6. C5 - Duplicate

Duplicate Entity and Duplicate Subtree both allocate new Scene Entity IDs.

Frozen rules:

1. Component data is copied by an explicit allowlist.
2. Mesh, Material, and Texture GUIDs are preserved; no asset revision is
   created or mutated.
3. Runtime GameObject IDs are reassigned.
4. GPU handles and Native runtime pointers are never copied.
5. Duplicate Subtree remaps every internal Parent Reference to new Scene IDs.
6. External Entity references are preserved only under explicit component rules;
   unsupported or ambiguous references fail closed rather than silently keeping
   an old Scene ID.
7. One Duplicate operation is one Scene Snapshot Transaction.
8. Failure leaves no partial entities or partial parent links.

G3 must publish the final component allowlist and the fail-closed policy for
unsupported components/references before coding Duplicate.

## 7. C6 - ABI Entity Handle Model

Existing ABI v1.12 and ops 37-46 remain unchanged. Existing root-index APIs
retain their legacy root-index meaning; they must not be silently redefined as
stable Entity IDs.

New P8 APIs are additive and accept **Scene Entity IDs**, never Runtime
GameObject IDs or Native pointers.

Candidate capabilities:

- Entity ID lookup / validation.
- Parent and child queries.
- Hierarchy enumeration.
- Reparent.
- Duplicate Entity / Subtree.
- Later: Prefab instantiate, instance status, Override get/apply/revert, source
  reconciliation.

Frozen ABI rules:

1. Do not renumber, reuse, or alter existing op values.
2. Do not change existing structure layouts, status values, or `RuntimeResult`.
3. Do not expose GPU handles or Native object pointers.
4. Every new operation defines invalid-ID, cross-Scene, cycle, stale-revision,
   and failure results.
5. The final ABI version and op count are chosen only after the actual G1/G2
   API set is known. Gate A does not pre-allocate a version number.
6. G1 may implement the Native Scene Composition core before adding the full
   Managed ABI; G2 determines the minimum Managed surface from real usage.

The current ABI is v1.12 with `ENGINE_HOST_OP_COUNT=47`
(`bridge/include/editor_bridge/capi.h:480`,
`bridge/include/editor_bridge/capi.h:551-636`).

## 8. C7 - Prefab Asset and Storage

Prefab is a formal committed Asset using existing RevisionStore semantics, not
a second commit system.

### 8.1 Asset identity

- Add `AssetType::Prefab`; preserve all existing AssetType numeric values and
  serialized behavior.
- The actual enum value is selected in G4 from the existing enum maximum; it is
  not hardcoded in Gate A.
- AssetBrowser must distinguish Prefab assets.
- Prefab reads go through the committed manifest/AssetResolver path.
- Prefab GUID remains stable across Prefab source revisions.

### 8.2 Prefab file

The Prefab file must carry at least:

| Field | Purpose |
|---|---|
| `formatVersion` | Prefab schema version |
| `prefabGuid` | Stable Prefab asset identity |
| `rootLocalId` | Root entity |
| `entities[]` | Entity records |
| `localId` | Stable identity inside this Prefab |
| `parentLocalId` | Internal hierarchy relation |
| `localTransform` | Local transform |
| `components[]` | Supported component data |

Asset references inside a Prefab are GUIDs. No GPU handle, Runtime Entity ID,
Native pointer, staging path, or temporary file path is persisted.

The Prefab Reader must be strict about format/version, unique Local IDs, valid
Parent Local IDs, self/cycle rejection, component allowlist, and asset
references. Unknown required data rejects; unsupported components do not
silently disappear.

### 8.3 Publication

Frozen sequence:

```text
Candidate
  -> full validation
  -> semantic digest
  -> RevisionStore Publish
  -> current updated only on success
```

- Publish failure leaves the old `current` and old committed bytes intact.
- Prefab semantic fields must contribute to the candidate digest.
- A Prefab must not overwrite a committed file outside the candidate/Publish
  path.
- A content-addressed path or equivalent immutable candidate artifact is
  required unless G4 proves a stronger atomic alternative.

Nested Prefab and Prefab Variant are deferred.

## 9. C8 - Prefab Instance and Override Identity

A Prefab Instance is anchored by its Root Scene Entity ID.

Each Instance records at least:

- Prefab GUID.
- Source Revision identity.
- Root Scene Entity ID.
- `Prefab Local ID -> Scene Entity ID` mapping.
- Override records.
- Instance synchronization/conflict status.

Override identity is exactly:

```text
Prefab Local ID + Component Type + Field Path
```

Entity name, array position, hierarchy display order, and root index are never
Override identities.

The first P8 override scope is:

- Local Transform.
- Fields explicitly allowlisted by an existing component contract.
- Mesh/Material GUID references already covered by the component contract.

Unsupported components/fields are not silently overridden.

## 10. C9 - Apply / Revert Transaction Boundary

Revert is a normal Scene Transaction. Apply is a recoverable cross-domain
operation and is **not** claimed to be globally atomic.

### 10.1 Revert

1. Read the current committed Prefab Source.
2. Restore the Instance fields from the corresponding Local ID.
3. Remove the corresponding Override record.
4. Update Runtime.
5. Commit one Scene History transaction.
6. Do not modify any Prefab Asset Revision.

### 10.2 Apply

Frozen two-phase outline:

1. Read Prefab Source and Instance Overrides.
2. Build a new Prefab Candidate.
3. Fully validate the Candidate.
4. Persist an Apply Recovery Record.
5. Publish the new Prefab Revision.
6. Update Instance Source Revision and Override Metadata.
7. Mark the Recovery Record complete.
8. Remove the Recovery Record only after completion is durable.

Startup handling:

- Publish not observed: restore the old state.
- Publish observed and Instance metadata update incomplete: continue completion.
- Commit state unknown: mark RecoveryRequired; never silently overwrite.
- Completed record: clear it.

Apply must be fault-injected before Publish, after Publish, during metadata
update, and across restart. If reliable recovery persistence cannot be achieved
in G6, Apply must be deferred rather than given weaker failure semantics.

Scene Undo does not roll back a successfully published Prefab Asset Revision.

## 11. C10 - Undo / Redo

All Scene Composition operations use the existing P7-B Snapshot History:

- Reparent.
- Delete Subtree.
- Duplicate Entity.
- Duplicate Subtree.
- Prefab Instantiate.
- Revert Override.
- Conflict Resolution.

Frozen rules:

1. No third Scene History stack.
2. Do not mix the legacy `Engine::UndoSystem` with Avalonia Snapshot History.
3. One user action = one transaction.
4. Failed commands do not enter History.
5. Save does not clear History.
6. Prefab Asset Publish is not rolled back by Scene Snapshot Undo; C9 governs
   that recovery.
7. CaptureScene/InstantiateScene must preserve Scene IDs, Parent References,
   bindings by entity ID, and Prefab Instance metadata before hierarchy
   operations are considered undoable.

The current snapshot path captures only root entities
(`engine/src/Core/Content/SceneSerializerV1.cpp:139-168`) and rebuilds a new
Scene (`bridge/src/EditorSession.cpp:530-554`). G1 must fix this before any
G2/G3 hierarchy action is accepted.

## 12. Mandatory work from G0 findings

### 12.1 Production 3D traversal

MeshRuntimeScene recursively collects child Mesh components
(`engine/src/Core/RHI/MeshRuntimeScene.cpp:30-40`), but
`Application::RenderProduction3DFrame()` builds its mesh list from
`scene.GetObjects()` roots only (`engine/src/Core/Application.cpp:944-952`).
Both Direct and RenderGraph consume that root-only list.

Therefore:

- G1: unified World Transform and descendant dirty propagation.
- G2: hierarchy UI and runtime graph synchronization.
- G3: copied subtrees must render in production Viewport.
- G8: multi-level Parent Direct/RenderGraph pixel regression.
- Picking must be hierarchy-aware or explicitly bounded in G8. A recursive
  MeshRuntimeScene binding is not evidence that production rendering or Picking
  already supports hierarchy.

### 12.2 Snapshot hierarchy

G1 exit must include Scene v2 CaptureScene/InstantiateScene round-trip, not just
Native Reparent tests. Undo, Redo, cold restart, and Play clone must all see the
same parent relationships.

## 13. P7-B compatibility

- Existing Scene v1 fixtures remain readable.
- Existing P7-B Transform, Undo/Redo, Gizmo, and save/cold-restart tests remain
  valid for flat Scenes.
- Existing ABI v1.12 ops 37-46 and RuntimeResult layout remain intact.
- Existing Material/Mesh/Texture GUIDs and RevisionStore behavior are unchanged.
- PICK-01 remains independently registered; P8 must not reclassify it.
- The legacy ImGui `Engine::UndoSystem` is not reused.
- The generic `JsonSerializer` child tree format is not adopted as the Scene or
  Prefab commit format.

## 14. Gate exit criteria

| Gate | Required before exit |
|---|---|
| G1 | Scene v2, persistent IDs, Parent Reference validation, Keep Local/World, subtree delete, dirty propagation, CaptureScene/InstantiateScene round-trip, Undo/Redo |
| G2 | Real hierarchy UI; renderer and Picking consumption path verified; UI and Scene cannot diverge |
| G3 | Duplicate IDs, component allowlist, internal parent remap, atomic failure behavior |
| G4 | Prefab Reader/Writer, strict hierarchy validation, committed AssetType, atomic publication |
| G5 | Multiple independent Instances, Local ID mapping, cold restart |
| G6 | Override identity, Revert, Apply recovery or explicit Defer |
| G7 | Three-way Base/Source/Instance reconciliation with stable Local IDs |
| G8 | Multi-level hierarchy pixels, Picking result, ABI compatibility, full regression |

## 15. Deferred

- Nested Prefab.
- Prefab Variant / inheritance.
- Prefab merge, multi-user collaboration.
- Scene streaming / World Partition.
- Multi-Scene editing.
- Automatic asset GC.
- Full ECS redesign.
- P7-C Draw State / PBR expansion.

## 16. Open items for later gates (not contract ambiguity)

These are implementation decisions allowed after Gate A, but must not violate
C1-C10:

- Exact `uint64` allocator algorithm and overflow policy.
- Exact Scene v2 JSON key spelling.
- Exact ABI op numbering/version after the API surface is known.
- Prefab content-addressed path convention and digest representation.
- Recovery Record file location/format.
- Conflict UI and manual resolution details.
- Whether hierarchy Picking is completed in G8 or explicitly bounded.

## 17. Current status

```text
P8 G0     : ACCEPTED - READ-ONLY - COMPLETE
P8 Gate A : FROZEN - CONTRACT RECORDED - IMPLEMENTATION NOT STARTED
P8 G1     : NOT STARTED
```

No source, test, CMake, ABI, Scene, or asset changes are part of this freeze.
The next allowed implementation action is G1 only after this document is
accepted as the P8 working contract.
