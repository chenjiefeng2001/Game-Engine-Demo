# P8 G3 - Duplicate / Subtree Clone Closure

**Status**: PASS - SCOPE-BOUND
**Contract**: `docs/P8-GateA-Scene-Composition-Contract.md` C5/C10
**Policy**: `docs/P8-G3-Duplicate-Policy.md` (frozen before implementation)
**Scope**: Entity and Subtree Duplicate, fresh Scene IDs, internal parent
remapping, atomic failure, Undo/Redo, Save/reopen, and managed/UI access.
Prefab, Duplicate remap of Prefab instance metadata, and asset revisions remain
P8-G4+.

## 1. Delivered

### Native Scene

`Scene::DuplicateEntity(sourceId, includeSubtree, newRootId, idMap, error)`:

- allocates fresh nonzero Scene Entity IDs for every copied node;
- leaves source IDs unchanged;
- places the copied root under the source root's parent; root sources remain
  roots;
- remaps every internal Parent Reference to the new root/subtree IDs;
- copies Local Transform, active/layer state, and only the frozen component
  allowlist;
- assigns a unique root display name (`Name_Copy`, `Name_Copy_2`, ...);
- never copies runtime GameObject IDs, GPU handles, Mesh/Texture pointers,
  physics handles, or Scene pointers.

### Component allowlist

Copied by serialize/deserialize through the Component Contract:

- `Mesh`
- `Camera`
- `Collider`
- `RigidBody`
- `Skinning`

Transform is built-in. Mesh GUID is preserved; runtime resources are rebuilt.
Unknown/non-contract/unregistered components fail the whole operation.
`MeshRendererComponent`, legacy `SpriteComponent`, and legacy script/sprite
bindings are rejected rather than silently dropped (see Policy).

### Atomicity

- Component cloning and hierarchy construction happen on detached objects before
  Scene registration.
- IDs are allocated only after all clones validate.
- Scene ID-map insertion failure rolls back inserted IDs.
- No parent link or root entry is created until the full clone succeeds.
- Failed Duplicate enters no history and leaves source/total counts unchanged.

### EditorSession / ABI / UI

- `EditorSession::DuplicateEntityById()` wraps Duplicate in the existing P7-B
  Snapshot History; auto-commits when no user transaction is active.
- ABI v1.14, `ENGINE_HOST_OP_COUNT = 60`:
  - `DUPLICATE_ENTITY = 58` (Dispatch; ID string, `arg0` subtree flag)
  - `GET_LAST_DUPLICATED_ENTITY_ID = 59` (Fetch; 8 bytes)
  - `EditorSession_DuplicateEntity(..., outNewRootId)` C export.
- Managed EditorHostService/channel/legacy paths expose
  `DuplicateEntity(id, includeSubtree)` and select the new root.
- Hierarchy has `Duplicate` and `Dup Tree` actions. The copied entity/subtree
  is selected after reload.

## 2. Tests

### New native

`SceneComposition`:

- Subtree duplicate: all new IDs, internal parent remap, transform and Mesh GUID
  copy, runtime Mesh status reset to `NotRequested`;
- single-entity duplicate: children are not copied;
- unsupported component duplicate: fails atomically with no entity added.

`SceneHierarchySessionTest`:

- Duplicate Subtree is one transaction; Undo removes all copies; Redo restores
  stable copied IDs; Save/reopen preserves copied IDs and parent links;
- invalid Duplicate fails without entering Undo history.

`EditorBridgeTest`:

- C ABI Duplicate returns a new root ID, remaps the child, supports Undo, and
  preserves the original hierarchy.

### UIA

`Hierarchy_duplicate_is_one_transaction_and_saves_new_ids`:

- creates a hierarchy entity in the real editor;
- clicks `Duplicate`;
- saves and verifies two unique Scene IDs;
- Undo removes the complete duplicate; Save;
- Redo restores the same copied ID; Save.

### Full inventory

Both Debug and Release:

```text
Native targets      : 21
Discovered          : 1224
Executed            : 1224
Passed              : 1222
Failed              : 1 (PICK-01, pre-existing)
Skipped             : 1 (SDK shaderc)
Managed static      : 381
CTest entries       : 22
```

- `test_content`: 140 tests, 139 PASS + 1 SDK skip.
- `test_bridge`: 120/120 PASS.
- UIA: 10/10 PASS in both Debug and Release.

## 3. Known limits / handoff

- G3 intentionally rejects entities with legacy Sprite/Script bindings or
  non-contract components. This is fail-closed, not a silent copy.
- No allowlisted component has an external Scene Entity reference; a future
  component with one must define an explicit remap rule before joining the list.
- Outside references (the source subtree's external parent placement) are
  preserved by placement, not rewritten into a new parent ID.
- Duplicate does not create or modify asset revisions.
- Prefab instance metadata, Duplicate of Prefab IDs, and Variant semantics are
  P8-G4+.
- 3D Picking remains unimplemented (G2 known limitation).
- The duplicate render path uses G2's production `GetAllEntities()` traversal;
  no new duplicate-specific pixel fixture was added.

## 4. Git

G3 changes are uncommitted. No commit, push, reset, clean, or stash was performed.
