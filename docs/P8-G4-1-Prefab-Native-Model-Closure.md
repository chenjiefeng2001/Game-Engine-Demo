# P8 G4-1 - Prefab Native Model + Scene Capture Closure

**Status**: PASS - CLOSED - SCOPE-BOUND
**Contract**: `docs/P8-G4-Prefab-Format-Contract.md` (frozen)
**Scope**: Native Prefab value model, strict component validation, Scene subtree
Capture, Local ID mapping, and Native tests. No Reader/Writer, AssetType,
RevisionStore Publish, ABI, Managed, or UI changes.

## 1. Delivered

### Native model

New `Engine::Content::PrefabAsset` types:

- `PrefabModel`
- `PrefabEntity`
- `PrefabComponentData`
- `PrefabLocalTransform`
- `PrefabLegacyBinding`
- `PrefabCaptureOptions`
- `PrefabLocalIdMap`
- `PrefabValidationResult`
- `PrefabCaptureResult`

The model is value-only. It contains no Runtime GameObject ID, GPU handle,
renderer cache, Scene pointer, staging path, or history metadata.

### Strict component validation

`ValidatePrefabComponentData()` implements the frozen G4-A schemas for:

- `Mesh`
- `Camera`
- `Collider`
- `RigidBody`
- `Skinning`

Unknown types, unknown fields, duplicate types, wrong types/ranges, malformed
nested data, and non-finite values fail closed. Existing component
`Deserialize()` methods are not used as the validation authority.

### Scene subtree Capture

`CapturePrefabFromScene(scene, sceneEntityId, options)`:

- captures the selected entity and all descendants;
- assigns nonzero `uint64` Local IDs `1..N` by deterministic DFS preorder;
- records Scene Entity ID -> Local ID and Local ID -> Scene Entity ID;
- preserves name, Local Transform, parent relationships, and Mesh GUIDs;
- sorts components by stable type string;
- rejects legacy Sprite/Script bindings;
- rejects unknown/non-contract components and invalid component payloads;
- validates the complete model before returning it;
- returns no partial model or mapping on failure.

The selected subtree root is always the Prefab root; an external Scene parent is
not copied into the Prefab.

### Model validation

`ValidatePrefabModel()` checks:

- formatVersion 1 and nonzero Prefab GUID;
- nonzero, unique Local IDs;
- exactly one root, matching `rootLocalId`;
- valid Parent Local IDs, no self-parent, no cycles;
- all entities reachable from the root;
- finite Local Transform values;
- component allowlist, duplicate types, and strict payload schemas.

## 2. Files

- `engine/include/Engine/Core/Content/PrefabAsset.h`
- `engine/src/Core/Content/PrefabAsset.cpp`
- `tests/test_content/PrefabCaptureTest.cpp`
- `tests/CMakeLists.txt` (test source registration only)

No `capi.h`, EngineHost, EditorSession, managed service, or UI file changed.

## 3. Tests

New `PrefabCapture` suite (11 tests):

- single entity identity/name/transform;
- multi-level subtree DFS Local IDs and parent remap;
- allowlisted Mesh/Camera/Collider/RigidBody/Skinning components and Mesh GUID;
- unsupported component rejection with no partial model;
- unknown component field rejection;
- duplicate component type rejection;
- legacy Script binding rejection;
- non-finite Transform rejection;
- duplicate Local ID model rejection;
- multiple-root and missing-parent model rejection;
- child-subtree root dropping an external Scene parent.

Full native inventory:

```text
Debug   : 21 targets, 1235 discovered/executed, 1233 PASS, 1 PICK-01, 1 SDK skip
Release : 21 targets, 1235 discovered/executed, 1233 PASS, 1 PICK-01, 1 SDK skip
Managed static: 381
CTest entries : 22
```

- `test_content`: 151 tests, 150 PASS + 1 SDK skip (Debug and Release).
- `test_bridge`: 120/120 PASS (Debug and Release, run separately).
- UIA was not rerun for G4-1; this gate changes no ABI/UI and UIA remains a
  G4-4/G4-5 requirement.

## 4. Known limits

- Prefab JSON Reader/Writer are not implemented (G4-2).
- AssetType, manifest registration, content-addressed file, Digest, and
  RevisionStore Publish are not implemented (G4-3).
- No Create Prefab ABI or editor workflow (G4-4).
- No Prefab Instance, Local ID -> Scene ID instantiation, Override, Apply,
  Revert, Nested Prefab, or Variant.
- Capture takes a caller-supplied nonzero Prefab GUID; asset identity creation
  belongs to G4-3.
- Capture refuses legacy Sprite/Script bindings by frozen G4-A policy.

## 5. Git

G4-1 changes are uncommitted. No commit, push, reset, clean, or stash was
performed.
