# P8 G6-5 - Integrated Verification Report

**Status**: PASS - EVIDENCE INTEGRATED - PENDING ACCEPTANCE
**Scope**: G6-0 through G6-4 integration audit and final G6 closure.
**Source changes in G6-5**: NONE
**Test reruns in G6-5**: NONE

## 1. Integrated chain

| Segment | Evidence |
|---|---|
| Override metadata / Source Identity / field paths | G6-1 closure |
| Inspector Override Tracking | G6-2 closure + UIA |
| Field and instance Revert | G6-3 closure + tests |
| Apply Candidate Builder | G6-4-1 closure |
| Durable Recovery Record | G6-4-2 closure |
| Phase 1 Prefab Revision Publish | G6-4-3 closure + fault tests |
| Phase 2 Scene Metadata Publish | G6-4-4 closure + fault tests |
| Startup Recovery / idempotency | G6-4-5 closure + tests |
| Apply ABI / Inspector | G6-4-6 closure + C ABI/UIA |
| Fault matrix / final G6-4 closure | G6-4-7 closure |

The chain is complete: Inspector write -> Override metadata -> Revert or
Candidate Apply -> Recovery Record -> Phase 1 Prefab Publish -> Phase 2 Scene
Metadata Publish -> Startup Recovery -> managed Apply UI.

## 2. Evidence policy

G6-5 does not add the historical test counts into one cumulative number. The
latest accepted G6-4 snapshot is:

```text
Native Debug/Release : 1285 executed, 1283 PASS, 1 PICK-01, 1 SDK skip
test_content         : 176 tests, 175 PASS + 1 SDK skip
test_bridge          : 145/145 Debug and Release
UIA Debug/Release    : 14/14 each (G6-4-6 evidence)
Managed static       : 385
ABI                  : v1.19 / 66 ops
```

Each lower gate retains its own closure evidence. G6-5 did not rerun Native
inventory or UIA because no source, ABI, or UI file changed after G6-4-7.

## 3. Integrated checks

- Override identity remains `Prefab Local ID + Component Type + Field Path`.
- Unsupported Skinning/Mesh-visible/Material/Texture fields remain fail-closed.
- Revert is a single Scene Snapshot transaction and does not publish Prefab.
- Apply is two-phase and not ordinary Scene Undo.
- Candidate preserves identity/name/hierarchy/asset GUIDs and uses G4 Writer.
- Recovery Record states are hints; actual committed Manifest/Prefab/Scene
  evidence governs startup recovery.
- Phase 1 can leave a legal intermediate current; Phase 2 advances it.
- Apply changes only the target Instance; other Instances, Scene Entity IDs and
  Hierarchy remain unchanged.
- Recovery is idempotent and `RecoveryRequired` blocks further Apply/Revert.
- ABI additions preserve all prior operation numbers.

## 4. Retained limits

- Fault points 3/4 are historical G4-3 first-publish marker evidence, not rerun
  on the versioned G6 baseline.
- Source update/reconciliation and conflict resolution remain G7.
- No UI recovery action; RecoveryRequired is visible and native operations are
  blocked.
- Nested Prefab and Prefab Variant remain unsupported.
- Material/Texture and Skinning overrides remain unsupported; Mesh visible is
  not an Override field.
- 3D mesh Picking remains unimplemented.
- Apply does not roll back a successfully published Prefab Revision through
  Scene Undo.

## 5. G6 exit criteria

| Requirement | Result |
|---|---|
| Override identity/metadata and field paths | PASS |
| Inspector override tracking | PASS |
| Field/instance Revert | PASS |
| Candidate builder | PASS |
| Recovery Record | PASS |
| Phase 1/Phase 2 Publish | PASS |
| Startup Recovery/idempotency | PASS |
| Apply ABI/UI | PASS |
| Fault matrix / historical exceptions | PASS with recorded limits |
| No new stable G6 failure | PASS |