# P8 G4-5 - Integrated Regression Report

**Status**: PASS - CLOSED - INTEGRATED EVIDENCE ACCEPTED
**Scope**: G4-0 through G4-4 integration chain and final G4 closure.
**Source changes in G4-5**: NONE
**Test reruns in G4-5**: NONE (no source/ABI/UI change since G4-4)

## 1. Integrated chain audit

| Chain segment | Implementation | Evidence |
|---|---|---|
| Scene Entity/Subtree -> Capture | `engine/.../PrefabAsset.h/.cpp` | G4-1 `PrefabCapture` 11 tests |
| Local ID mapping | DFS `1..N`, Scene->Local maps | G4-1 multi-level/map tests |
| Strict validation | `ValidatePrefabModel`, component schemas | G4-1 + G4-2 negatives |
| Canonical Writer / Digest | `PrefabSerialization.h/.cpp` | G4-2 14 tests |
| Content-addressed file | `EditorSession::CreatePrefabFromEntity` | G4-3 Prefab asset tests |
| ContentRegistry / RevisionStore | existing manifest + Publish | G4-3 cold reopen + 8 fault points |
| ABI v1.15 / EngineHost | op 60 + C export + managed mapping | `test_bridge` 127/127 |
| Hierarchy Create Prefab | `PrefabNameBox` / `CreatePrefabBtn` | G4-4 UIA success |
| AssetBrowser / Dependencies | Prefab type/filter/path/GUID/status; Mesh dep | G4-4 UIA + G4-3 dependency tests |

The chain is complete. No parallel Prefab registry, serializer, or revision
authority exists.

## 2. Gate evidence (not summed)

| Gate | Status | Evidence |
|---|---|---|
| G4-0 Prefab Asset Map | ACCEPTED | read-only map; no source change |
| G4-A Format Contract | ACCEPTED / FROZEN | A1-A16 + C7 amendments |
| G4-1 Native Model + Capture | CLOSED | Native 1235, 1233 PASS, 1 PICK-01, 1 skip; `test_content` 151 |
| G4-2 Reader/Writer | CLOSED | Native 1249, 1247 PASS, 1 PICK-01, 1 skip; `test_content` 165; 14 serialization tests |
| G4-3 Asset Integration | CLOSED | Native 1256, 1254 PASS, 1 PICK-01, 1 skip; `test_bridge` 127; fault matrix |
| G4-4 Editor Workflow | CLOSED | UIA 13/13 Debug + Release; Managed static 384 |
| G4-5 Integrated Regression | PASS | this evidence audit; no rerun |

These are separate historical gate snapshots, not one cumulative test count.
The native count increased because later gates added tests; the same test was not
counted twice for G4-5.

## 3. Targeted checks performed

No source, test, CMake, ABI, or managed UI file changed after G4-4 acceptance.
Therefore G4-5 did not rerun the 1256-test native inventory, UIA, or full
workspace inventory. The existing G4-4 debug/release evidence remains the final
G4 UI evidence.

Checks performed for this gate:

- chain-to-file mapping above;
- ABI version/op ownership consistency (`v1.15`, `op=60`, `count=61`);
- closure status alignment across G4-1..G4-4;
- working-tree check for new source/test changes in G4-5: none.

## 4. Known limitations / archived exceptions

- PICK-01 remains the known native failure.
- SDK shaderc remains the known conditional skip.
- `MaterialEditUpdatesProductionPixels` has a recorded GPU transient that passed
  standalone/clean reruns; it is not a stable G4 failure.
- Prefab files are outside RevisionStore current hashes; failed publish may leave
  an orphan candidate. This is intentional and documented.
- No Prefab Instance, Local ID -> Scene Entity ID instantiate mapping, Anchor,
  Override, Apply/Revert, recovery, or source reconciliation.
- No same-GUID Prefab update UI/API; Create allocates a new asset GUID.
- Legacy Sprite/Script bindings remain fail-closed.
- Prefab storage path is fixed by G4-A; no arbitrary target path.
- Missing Mesh dependencies remain unresolved GUID references for G5.
- 3D Picking remains a G2 known limitation; G4 does not claim it.
- No dedicated Prefab production-pixel test; G4-4 validates the editor workflow,
  not Prefab instantiation rendering.

## 5. G4 exit criteria

| Requirement | Result |
|---|---|
| Native Prefab Model + Capture | PASS |
| Strict Reader/Writer + Digest | PASS |
| AssetType/Manifest/RevisionStore | PASS |
| ABI v1.15 / 61 ops | PASS |
| Create Prefab UI | PASS |
| AssetBrowser / Dependencies | PASS |
| Debug/Release UIA | PASS |
| Known limits recorded | PASS |
| New unresolved G4 failures | NONE |
