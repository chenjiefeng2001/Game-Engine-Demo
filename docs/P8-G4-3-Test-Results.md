# P8 G4-3 - Test Results

**Status**: PASS - DEBUG / RELEASE
**Contract**: `docs/P8-G4-Prefab-Format-Contract.md`
**ABI**: v1.15 / 61 ops

## 1. New Prefab asset tests

`PrefabAssetSessionTest` (7 tests):

1. Create Prefab publishes canonical content-addressed bytes, registers type 4,
   preserves the live Scene, and reads back with expected GUID.
2. Cold reopen preserves GUID/path/type and the **committed** Scene remains empty
   (unsaved source entities were not committed by Prefab Publish).
3. C ABI `EditorSession_CreatePrefabFromEntity` registers a Prefab asset.
4. Corrupt Prefab bytes fail Reader/dependency query; missing file reports
   `GetAssetStatus == 1`.
5. Revision fault points 0,1,2,5,6,7: old current bytes and asset count remain
   intact.
6. Revision fault points 3,4 on first publish: marker failure leaves no current
   and no registered asset.
7. Orphan candidate file remains after failed publish, is not in the manifest,
   and a cold reopen does not expose it as a Prefab asset.

Prefab direct Mesh dependency and Mesh -> Prefab dependent queries are covered
by test 1.

## 2. Execution

```text
test_content Debug   : 165 tests, 164 PASS, 1 SDK skip
test_content Release : 165 tests, 164 PASS, 1 SDK skip
test_bridge Debug    : 127/127 PASS
test_bridge Release  : 127/127 PASS
UIA Debug            : 10/10 PASS
UIA Release          : 10/10 PASS
```

Full native inventory:

```text
Debug   : 21 targets, 1256 discovered/executed, 1254 PASS, 1 PICK-01, 1 SDK skip
Release : 21 targets, 1256 discovered/executed, 1254 PASS, 1 PICK-01, 1 SDK skip
Managed static: 381
CTest entries : 22
```

## 3. Historical / transient failures

- `PICK-01` (`EditorHostAbiTest.D3PickRequestPumpFetchHappyPath`) remains the
  known native failure.
- SDK shaderc remains the known conditional skip.
- `MeshProductionPixelTest.MaterialEditUpdatesProductionPixels` showed the same
  GPU transient failure in one Release inventory; the test passed standalone and
  the clean Release inventory passed. It is not counted as a G4-3 regression.
- UIA was run for ABI/version regression only; Create Prefab UI is deliberately
  not tested before G4-4.
