# P7-B Gate A — Scene Editing Contract (Frozen)

**Status**: FROZEN — implementation not started

## C1 Transform

- Authority: `TransformComponent` (position Vec3, internal quaternion, scale Vec3).
- Inspector/UI uses Euler degrees cached from the quaternion; native edits normalize the quaternion and reject non-finite values/zero length.
- Additive Scene v1 entity fields:
  - `rotation`: `[x,y,z,w]` quaternion (default identity when absent);
  - `scale`: `[x,y,z]` (default `[1,1,1]` when absent).
  Old scenes remain valid; no Scene version bump.
- Scale accepts finite values, including negative; zero is allowed by the math but may produce degenerate normals; UI must show the value, not silently clamp.
- Translation/rotation/scale are local/root semantics; no local/world toggle until parent/child is a persisted Scene contract.

## C2 Entity identity / lifecycle

- v1 identity = name + Scene order (no persisted numeric entity ID); `Undo` restores the saved name/components/order.
- Create/Delete/AddComponent/RemoveComponent are single transactions.
- Delete snapshots all contract components; Undo restores them; Mesh GUIDs are unchanged.
- No asset deletion/revision mutation from Scene operations.
- `MeshRuntimeScene` resynchronizes after Scene replacement; old runtime references are released by existing B5/G4 rules.

## C3 Hierarchy

- v1 is a flat Entity list; no parent/child UI until serialization contract exists.
- UI supports create empty entity, select, delete, Add Mesh, and status display.
- Rename uses existing name persistence.
- No duplication/prefab/merge in P7-B.

## C4 Undo/Redo

- Typed transaction with before/after state; one user action = one transaction.
- History is session-only; cap 64 transactions (memory-bounded; trim oldest).
- Undo/Redo restores Scene state only; **never** rolls back Material/Texture revisions or asset Publish.
- New mutation after Undo clears the redo branch.
- Failed commands do not enter history and must not half-modify Scene.
- Save checkpoint = last saved Scene snapshot; dirty is recomputed after Undo/Redo; Save does not clear history.
- Cold restart starts with empty history and the loaded Scene as baseline.

## C5 Gizmo

- Modes: Translate / Rotate / Scale; axes X/Y/Z; optional uniform scale only if Transform/runtime contract supports it.
- One drag = Begin transaction → update preview → Commit single transaction; Escape/focus loss/unload = Cancel and restore before state, no history.
- Handle hit testing is Gizmo-only; full 3D object picking remains deferred (P4-B).
- Viewport/camera projection, DPI and letterbox must be reused; no screen-space guessing.
- A gizmo may be a UI overlay but must drive the same native Scene transform/history path.

## C6 ABI

- Baseline v1.11; additive v1.12 expected for:
  Get/SetEntityTransform, Undo/Redo, CanUndo/CanRedo, Begin/Commit/CancelSceneEdit.
- Existing op numbers/status/RuntimeResult layout unchanged.
- No GPU handles/Scene pointers across ABI.
- Scene v1 additive fields only; no version bump unless old compatibility fails.

## C7 Acceptance

Native transform/entity/history tests; Scene save/load with rotation/scale; real Windows UIA Inspector + Hierarchy + Undo/Redo + cold restart; Gizmo math + production pixel/transform evidence; Direct/RenderGraph regression; P6-1B/P7-C/P7-A regression.
