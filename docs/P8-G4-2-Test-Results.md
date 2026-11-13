# P8 G4-2 - Test Results

**Status**: PASS - DEBUG / RELEASE
**Test binary**: `test_content`
**New suite**: `PrefabSerialization` (14 tests)
**ABI unchanged**: v1.14 / 60 ops

## 1. New serialization suite

Positive coverage:

- Writer deterministic bytes and digest;
- complete round-trip of names, IDs, parents, transforms, and all five component
  types;
- normalized entity and component ordering;
- Local ID `(1ull << 63) + 7` exact uint64 round-trip;
- Digest changes for name, parent, transform, component, Mesh GUID, and Prefab
  GUID;
- empty component lists and missing dependencies (Reader does not require them).

Negative coverage:

- empty, malformed, truncated, and trailing JSON;
- duplicate top-level and entity object keys;
- unknown/missing top-level and entity fields;
- unknown component fields/types;
- duplicate component types;
- bad/zero/uppercase GUIDs;
- unsupported formatVersion;
- expected GUID mismatch;
- multiple roots, missing parent, self parent, cycle/no-root;
- zero/negative/fractional/out-of-range Local IDs;
- malformed/non-finite Transform arrays;
- Camera zoom, Collider shape, RigidBody motion, Skinning visible;
- malformed nested Skinning bind arrays, unknown bone fields, malformed animation
  tracks;
- Writer invalid-model failure with no bytes/digest;
- Reader failure with empty model.

## 2. G4-2 execution

```text
test_content Debug   : 165 tests, 164 PASS, 1 SDK skip
test_content Release : 165 tests, 164 PASS, 1 SDK skip
test_bridge Debug    : 120/120 PASS
test_bridge Release  : 120/120 PASS
```

Full native inventory:

```text
Debug   : 21 targets, 1249 discovered/executed, 1247 PASS, 1 PICK-01, 1 SDK skip
Release : 21 targets, 1249 discovered/executed, 1247 PASS, 1 PICK-01, 1 SDK skip
Managed static: 381
CTest entries : 22
```

The only native failure is the historical `PICK-01`
(`EditorHostAbiTest.D3PickRequestPumpFetchHappyPath`). The one skip is the
existing shaderc SDK condition. Neither is a G4-2 regression.

## 3. Notes

- UIA was not rerun. G4-2 changes no ABI, Managed UI, or Editor path and UIA
  belongs to G4-4/G4-5.
- During repeated full inventory, `MeshProductionPixelTest.MaterialEditUpdates-
  ProductionPixels` was observed to be a GPU flake; it passed on the clean
  repeated Debug/Release inventory and was not counted as a G4-2 failure.
- No test was skipped or suppressed to obtain the G4-2 result.
