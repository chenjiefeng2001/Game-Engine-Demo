# P8 G4-2 - Reader / Writer Implementation Report

**Status**: IMPLEMENTED - VERIFICATION PENDING ACCEPTANCE
**Contract**: `docs/P8-G4-Prefab-Format-Contract.md` (unchanged)
**Scope**: Native canonical Writer, strict Reader, full-byte Digest,
round-trip/determinism. No AssetType, ContentRegistry, RevisionStore, ABI,
Managed, or UI changes.

## 1. Files

Added:

- `engine/include/Engine/Core/Content/PrefabSerialization.h`
- `engine/src/Core/Content/PrefabSerialization.cpp`
- `tests/test_content/PrefabSerializationTest.cpp`

Changed:

- `tests/CMakeLists.txt` (serialization test registration only)

Not changed:

- `capi.h`, EngineHost, EditorSession, managed service, UI;
- `Content::AssetType`;
- `RevisionStore`;
- ABI remains v1.14 / 60 ops.

## 2. Writer

`WritePrefab(model)`:

1. calls `ValidatePrefabModel(model)`;
2. rejects invalid input without producing bytes or a digest;
3. copies and sorts entities by `localId`;
4. sorts each entity's components by stable type string;
5. emits the G4-A JSON with `dump(2)`;
6. computes the full-byte Digest with
   `RevisionStore::HashBytes(bytes)`.

The Writer outputs `format: "engine.prefab"`, `formatVersion: 1`, canonical
`prefabGuid`, `rootLocalId`, entity name/parent/transform, and strict component
payloads. Root parent is JSON `null`.

## 3. Reader

`ReadPrefab(bytes, expectedGuid)`:

- parses JSON with duplicate-object-key detection;
- rejects malformed, truncated, trailing, or non-object input;
- requires exact top-level/entity/component fields;
- requires exact `formatVersion` and canonical GUID;
- reads Local IDs as exact uint64 JSON integers;
- rejects zero, negative, fractional, and out-of-range IDs;
- validates Transform array lengths and finite values;
- validates every component through G4-1's strict component validator;
- validates root/parent/cycle/reachability through the G4-1 model validator;
- verifies `expectedGuid` when supplied;
- returns an empty model and explicit error on any failure.

Reader does not query AssetRegistry or require dependencies to exist.

## 4. Digests

`ComputePrefabDigest(bytes)` exposes the frozen full-byte hash. The Writer returns
the same value. Tests prove:

- repeated writes are byte-identical;
- repeated hash of the same bytes is identical;
- name, parent, transform, component, Mesh GUID, and Prefab GUID changes all
  change bytes and digest.

## 5. Safety and boundaries

- No Runtime GameObject ID, GPU handle, renderer cache, pointer, staging path, or
  history data is serialized.
- Legacy Sprite/Script data is not part of the format; unknown fields reject.
- Reader/Writer are independent of AssetType and RevisionStore.
- Failure never returns partial bytes or a partial model.
