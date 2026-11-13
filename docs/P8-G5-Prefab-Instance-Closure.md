# P8 G5 - Prefab Instance Closure

**Status**: CORE PASS - ACCEPTED - SCOPE-BOUND
**Contract**: P8 Gate A C8
**Report**: `docs/P8-G5-Prefab-Instance-Report.md`
**Results**: `docs/P8-G5-Test-Results.md`

## 1. Delivered

- Persistent Prefab Instance metadata:
  - Prefab GUID;
  - source revision;
  - instance root Scene Entity ID;
  - Local ID -> Scene Entity ID mapping.
- Scene v2 optional metadata, strict graph/schema validation, Capture/Instantiate
  persistence, v1 compatibility.
- Multi-instance instantiation with independent Scene IDs and preserved asset
  GUIDs/component data.
- Save/cold restart, Undo/Redo, and missing-asset fail-closed.
- ABI v1.16 / 63 ops:
  - `INSTANTIATE_PREFAB = 61`;
  - `GET_LAST_INSTANTIATED_ENTITY_ID = 62`;
  - C export, EngineHost dispatch/fetch, Managed service, Asset Browser button.

## 2. Verification

- Native Debug/Release: 1259 executed, 1257 PASS, 1 PICK-01, 1 SDK skip.
- `test_bridge`: 130/130.
- `test_content`: 165, 164 PASS + 1 skip.
- UIA: 13/13 Debug and Release (G4 workflow/regression; no dedicated Instantiate
  UIA claim).
- Managed static: 384.

## 3. Scope / limitations

- No Overrides, Apply, Revert, recovery, refactoring, or source update.
- Source revision is the project revision at instantiate.
- Nested Prefab instances are rejected.
- No dedicated Prefab Instance UIA workflow is claimed.
- Instance subtree editing does not have Override semantics; deleting a mapped
  entity removes the instance metadata record rather than persisting a broken map.
- Runtime/GPU resources are rebuilt, not copied.

## 4. Next

P8-G6 Overrides / Apply / Revert is READY / NOT STARTED, subject to separate
contract acceptance.

## 5. Git

G5 changes are uncommitted. No commit, push, reset, clean, or stash was performed.
