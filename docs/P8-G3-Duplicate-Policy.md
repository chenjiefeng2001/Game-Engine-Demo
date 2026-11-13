# P8 G3 - Duplicate Policy

**Status**: FROZEN FOR G3 IMPLEMENTATION
**Contract**: `docs/P8-GateA-Scene-Composition-Contract.md` C5
**Scope**: Duplicate Entity and Duplicate Subtree only. Prefab and override
identity remain P8-G4+.

## 1. Component allowlist

Duplicate copies the following data. Transform is built-in and is copied as
Local Transform.

| Component | Contract type | Copied data | Runtime state |
| --- | --- | --- | --- |
| Transform | built-in | position, rotation, scale | local transform only |
| Mesh | `Mesh` | mesh GUID | runtime Mesh/GPU resources are rebuilt, never shared |
| Camera | `Camera` | serialized camera fields | runtime state rebuilt |
| Collider | `Collider` | serialized collider fields | physics body is rebuilt/unbound |
| RigidBody | `RigidBody` | serialized body fields | physics body is rebuilt/unbound |
| Skinning | `Skinning` | serialized skeleton/skinned mesh/animation data | animation runtime rebuilt |

Unknown, non-contract, or unregistered components fail the whole operation.
`MeshRendererComponent`, legacy `SpriteComponent`, and script bindings are not
cloned by G3. A entity carrying such a component is rejected rather than copied
with missing data. G3 does not silently drop a component.

## 2. Asset references

- Mesh GUID is preserved exactly.
- Material/Texture GUIDs and RevisionStore entries are not modified.
- No asset revision is created or published by Duplicate.
- Runtime GPU handles, Mesh pointers, Texture pointers, physics handles, and
  scene/runtime pointers are never copied.

## 3. Entity references and parent remapping

- Duplicate Subtree allocates a new Scene Entity ID for every copied node.
- Parent References inside the copied subtree are remapped to the new IDs.
- The copied root keeps the source root's parent. Root sources produce new roots.
- No Child arrays are persisted; only Parent References are rewritten.
- None of the allowlisted components carries a Scene Entity ID reference. If a
  future component adds an external entity reference, it is not allowed into
  this list until an explicit remap policy is frozen.

## 4. Atomicity and history

- All clones and component deserialization happen before the Scene ID map or
  hierarchy is mutated.
- A failure leaves no new entities, no new parent links, and no new bindings.
- A successful duplicate is one P7-B Snapshot Transaction; Undo/Redo restore
  the complete copied subtree and IDs.
- Duplicate uses the existing Snapshot History. No second Undo stack is added.

## 5. API

Native Scene:

```cpp
bool DuplicateEntity(uint64 sourceId, bool includeSubtree,
                     uint64* newRootId,
                     std::unordered_map<uint64, uint64>* idMap,
                     std::string* error);
```

EditorSession/ABI expose the same operation by Scene Entity ID, with the
EditorSession layer copying legacy sprite/script bindings only when those
components are present under an explicit future contract. G3 itself rejects
such entities.

## 6. Verification

- New IDs for every duplicated node; source IDs unchanged.
- Internal parent References remap; external parent/root placement preserved.
- Mesh/Camera/Collider/RigidBody/Skinning data round-trip.
- Unsupported components fail atomically.
- Undo/Redo and Save/reopen preserve copied hierarchy and IDs.
- Duplicated mesh subtrees enter production rendering through the G2
  `GetAllEntities()` path.
