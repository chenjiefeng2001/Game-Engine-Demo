# P8 G4-2 - Strict Prefab Reader / Writer Closure

**Status**: PASS - CLOSED - STRICT PREFAB SERIALIZATION VERIFIED - SCOPE-BOUND
**Contract**: `docs/P8-G4-Prefab-Format-Contract.md` (unchanged)
**Implementation map**: `docs/P8-G4-2-Implementation-Map.md`
**Implementation report**: `docs/P8-G4-2-Reader-Writer-Implementation-Report.md`
**Test results**: `docs/P8-G4-2-Test-Results.md`

## 1. Delivered

- `engine/include/Engine/Core/Content/PrefabSerialization.h`
- `engine/src/Core/Content/PrefabSerialization.cpp`
- `tests/test_content/PrefabSerializationTest.cpp`
- `tests/CMakeLists.txt` test registration only

### Writer

`WritePrefab(model)` validates with the G4-1 model validator, normalizes entity
and component order, emits G4-A canonical JSON, and returns complete bytes plus
the full-byte `RevisionStore::HashBytes` digest. Invalid models return no bytes
and no digest.

### Reader

`ReadPrefab(bytes, expectedGuid)` enforces:

- exact `engine.prefab` format and formatVersion 1;
- duplicate JSON object key rejection;
- exact uint64 Local IDs and root ID;
- canonical nonzero Prefab GUID;
- exact entity/transform/component schemas;
- strict nested Skinning schemas;
- the complete G4-1 hierarchy/model validator;
- expected Asset GUID equality when requested;
- empty model on every failure.

Reader does not use AssetRegistry, EditorSession, EngineHost, or RevisionStore.

## 2. Contract trace

| Contract item | Result |
|---|---|
| Canonical bytes / deterministic ordering | PASS |
| uint64 precision | PASS (`(1ull << 63) + 7`) |
| Entity name / parent / transform | PASS |
| Five component strict schemas | PASS |
| Unknown fields/types/duplicate components | PASS |
| Legacy Sprite/Script | Rejected by G4-1 capture and serialization field policy |
| Full-byte Digest incl. prefabGuid | PASS |
| Missing dependencies not required | PASS |
| Fail-closed, no partial model | PASS |
| No Runtime IDs / GPU handles | PASS by model/format |

## 3. Verification

- Native Debug/Release: 1249 executed, 1247 PASS, 1 PICK-01, 1 SDK skip each.
- `test_content`: 165 tests, 164 PASS, 1 SDK skip (Debug and Release).
- `test_bridge`: 120/120 (Debug and Release).
- Managed static: 381.
- UIA not rerun; G4-2 is Native-only and changes no ABI/UI. G4-4/G4-5 owns UIA.

Historical PICK-01 and SDK skip remain separately tracked. A separate run
observed the existing `MaterialEditUpdatesProductionPixels` GPU flake; the clean
Debug and Release inventories passed it and it is not a G4-2 failure.

## 4. Known limits / handoff to G4-3

- No `Content::AssetType::Prefab` registration.
- No content-addressed file path, Manifest entry, or RevisionStore Publish.
- No ABI v1.15 / Create Prefab implementation.
- No Managed API or Editor UI.
- No Prefab Instantiate/Override/Apply.
- Reader accepts any valid entity/component order and normalizes only through
  the Writer; canonical ordering is a Writer property.

G4-3 can consume `WritePrefab().bytes`, `.digest`, `CapturePrefabFromScene()`,
and `ReadPrefab()` directly.

## 5. Git

G4-2 changes are uncommitted. No commit, push, reset, clean, or stash was
performed.
