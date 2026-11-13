# P8 G4-2 - Reader / Writer Implementation Map

**Status**: IMPLEMENTED - READ-ONLY REVIEW RECORD
**Contract**: `docs/P8-G4-Prefab-Format-Contract.md` (frozen)
**Baseline**: G4-1 accepted; ABI v1.14 / 60 ops (unchanged)

## 1. G4-1 model as implemented

`engine/include/Engine/Core/Content/PrefabAsset.h` provides:

- `PrefabModel`: formatVersion, prefabGuid, rootLocalId, entities.
- `PrefabEntity`: localId, name, optional parentLocalId, localTransform,
  components.
- `PrefabComponentData`: type string + strict-schema JSON payload.
- `PrefabLocalTransform`: position, quaternion, scale.
- `PrefabLocalIdMap`, `PrefabValidationResult`, `PrefabCaptureResult`.

`ValidatePrefabModel()` already checks IDs, root/parent graph, finite Transform,
component allowlist, duplicate component types, and strict component payloads.
G4-2 reuses this validator and does not reimplement the model.

## 2. Interface implemented

`engine/include/Engine/Core/Content/PrefabSerialization.h`:

```cpp
struct PrefabWriteResult { bool ok; std::string bytes; std::string digest; std::string error; };
struct PrefabReadResult  { bool ok; PrefabModel model; std::string error; };

PrefabWriteResult WritePrefab(const PrefabModel& model);
PrefabReadResult  ReadPrefab(std::string_view bytes,
                             const ResourceGUID* expectedGuid = nullptr);
std::string ComputePrefabDigest(std::string_view bytes);
bool PrefabModelsEqual(const PrefabModel& a, const PrefabModel& b);
```

A dedicated `PrefabDiagnostic` class is not required by G4-A; the frozen error
contract is represented by the explicit `error` string, which carries
top-level, field, component-type, and nested schema failures. Failure clears the
model and returns no bytes/digest.

## 3. Serialization decisions

### Canonical ordering

- Writer validates first.
- Entities are sorted by ascending `localId`.
- Components are sorted by stable type string.
- JSON is emitted with `nlohmann::json::dump(2)`, no trailing byte.
- Object keys use nlohmann's deterministic ordered-object behaviour.
- `PrefabModelsEqual` compares the persisted fields; round-trip tests normalize
  the model before semantic comparison.

### uint64

- Local IDs and root IDs are written as JSON unsigned integers.
- Reader accepts only integer JSON forms; `1.5`, negative, zero, and >uint64
  values reject.
- Large integer test uses `(1ull << 63) + 7` to prove no double conversion.

### Digest

- Frozen algorithm: `RevisionStore::HashBytes(complete canonical bytes)`.
- Returns the full 16-character lowercase FNV-1a 64-bit digest.
- `prefabGuid`, `formatVersion`, names, IDs, parents, transforms, components,
  and ordering are all covered.
- No path, registry, or RevisionStore dependency is introduced.

### Duplicate JSON keys

nlohmann's default parser is last-value-wins. G4-2 supplements it with a parse
callback that tracks object key sets by object stack and rejects duplicate keys
before accepting the document. This is tested at top-level and entity level.

## 4. Reader strictness

Reader:

- rejects empty/invalid/truncated/trailing JSON;
- requires the exact top-level fields and `format == "engine.prefab"`;
- requires `formatVersion == 1`;
- requires canonical 32-hex nonzero `prefabGuid`;
- optionally verifies the expected Asset GUID;
- parses Local IDs with exact uint64 rules;
- requires exact entity/root/parent/transform/component fields;
- rejects unknown and missing fields at every level;
- rejects duplicate/unknown component types and invalid payloads through the
  G4-1 strict validator;
- validates the resulting model with `ValidatePrefabModel`;
- returns an empty model on any failure.

No AssetRegistry, EditorSession, EngineHost, or RevisionStore is used by the
Reader/Writer.

## 5. Contract ambiguity check

- Digest algorithm was already frozen as `RevisionStore::HashBytes`.
- uint64 representation was already frozen as unsigned JSON integers, and G4-2
  verifies exact parsing.
- Entity/component ordering was frozen in G4-A; Writer normalizes and Reader
  accepts any valid order.
- No G4-A amendment was needed.

## 6. Verification entry

`tests/test_content/PrefabSerializationTest.cpp` covers Writer positive cases,
Reader positive/negative cases, uint64 precision, duplicate keys, deeply nested
Skinning validation, determinism, Digest sensitivity, and fail-closed returns.
