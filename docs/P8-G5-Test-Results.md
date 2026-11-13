# P8 G5 - Test Results

**Status**: CORE PASS - ACCEPTED - SCOPE-BOUND
**ABI**: v1.16 / 63 ops

## 1. New Prefab Instance tests

`PrefabInstanceTest` (3):

- multiple independent instances;
- Local ID -> Scene Entity ID mapping;
- shared Prefab GUID and source revision;
- Mesh GUID copied without runtime/GPU state;
- Save / cold reopen metadata recovery;
- Undo/Redo;
- missing Prefab bytes fail without partial entities.

The C ABI Prefab test now also invokes `EditorSession_InstantiatePrefab`.

## 2. Execution

```text
Native Debug   : 1259 executed, 1257 PASS, 1 PICK-01, 1 SDK skip
Native Release : 1259 executed, 1257 PASS, 1 PICK-01, 1 SDK skip
test_bridge     : 130/130 Debug and Release
test_content    : 165 tests, 164 PASS + 1 skip Debug and Release
UIA Debug       : 13/13 PASS (existing G4 workflow/regression)
UIA Release     : 13/13 PASS (existing G4 workflow/regression)
Managed static  : 384
CTest entries   : 22
```

G5 adds no dedicated Instantiate UIA test; the G4 UIA suite remains a regression
guard. G5's instance evidence is Native + ABI + Scene persistence.

## 3. Known retained failures

- PICK-01 remains the known native failure.
- SDK shaderc remains the known conditional skip.
- The historical `MaterialEditUpdatesProductionPixels` GPU transient remains
  recorded; it is not a G5 regression.

## 4. Observations / limits

- Source revision is the current project revision at instantiate time.
- No editing of instance subtree (add/delete/reparent child) is given special
  Override semantics in G5; deletion of an instance root/descendant removes its
  metadata record to avoid persisting a broken instance.
- No nested Prefab instance metadata is accepted.
- No Instance UIA claim; no Override/Apply/Revert.
