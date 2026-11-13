# P8 G4-4 - Test Results

**Status**: PASS - DEBUG / RELEASE
**ABI**: v1.15 / 61 ops
**UI tests**: 13/13 in each configuration

## 1. New G4-4 UIA

- `Prefab_create_publishes_asset_and_shows_dependency`
  - real editor open;
  - source hierarchy selected;
  - Create Prefab;
  - source entity/hierarchy preserved;
  - Asset Browser shows Prefab, content-addressed path, 32-hex GUID,
    Available status;
  - dependency view shows the direct Mesh reference as `[UNRESOLVED]`.
- `Prefab_create_failure_leaves_scene_and_reports_error`
  - legacy Script-bound entity;
  - visible failure diagnostic;
  - Scene hierarchy unchanged.
- `Prefab_publish_failure_leaves_scene_unchanged`
  - RevisionStore candidate path deliberately invalidated;
  - visible failure diagnostic;
  - Scene hierarchy unchanged.

## 2. Regression execution

```text
UIA Debug   : 13/13 PASS
UIA Release : 13/13 PASS
test_bridge Debug/Release : 127/127 PASS
test_content Debug/Release: 165 tests, 164 PASS + 1 SDK skip
Native Debug   : 1256 executed, 1254 PASS, 1 PICK-01, 1 SDK skip
Native Release : 1256 executed, 1254 PASS, 1 PICK-01, 1 SDK skip
Managed static: 384
CTest entries : 22
```

## 3. Historical/transient failures

- PICK-01 remains the known native failure.
- SDK shaderc remains the known conditional skip.
- `MaterialEditUpdatesProductionPixels` showed its existing GPU transient in
  one full inventory; the test passed standalone and clean Debug/Release
  inventories passed. It is not a G4-4 regression.
- G4-4 UIA covers real Windows editor windows in both configurations; Managed
  static tests are not used as a substitute.
