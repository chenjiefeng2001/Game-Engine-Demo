# P8 Scene Composition - Delivery Status

**Status**: P8 IN PROGRESS - G0 THROUGH G4 CLOSED; G5 CORE ACCEPTED; G6 CLOSED; G7-0 THROUGH G7-4 CLOSED; G7-5 SUBMITTED
**Branch**: `avalonia`
**HEAD**: `5de8f1a` (all P8 work uncommitted)
**Last updated**: 2026-11-13

## 1. Gate status

| Gate | Status | Closure / evidence |
|---|---|---|
| P8-G0 Scene Composition Map | CLOSED / ACCEPTED | `docs/P8-G0-Scene-Composition-Map.md` |
| P8 Gate A Composition Contract | CLOSED / FROZEN | `docs/P8-GateA-Scene-Composition-Contract.md` |
| P8-G1 Parent-Child Core | CLOSED | `docs/P8-G1-Parent-Child-Core-Closure.md` |
| P8-G2 Hierarchy Editor | CLOSED | `docs/P8-G2-Hierarchy-Editor-Closure.md` |
| P8-G3 Duplicate / Subtree Clone | CLOSED / CORE ACCEPTED | `docs/P8-G3-Duplicate-Closure.md`, policy |
| P8-G4-0 Prefab Asset Map | CLOSED / ACCEPTED | `docs/P8-G4-G0-Prefab-Asset-Map.md` |
| P8-G4-A Prefab Format Contract | CLOSED / FROZEN | `docs/P8-G4-Prefab-Format-Contract.md` |
| P8-G4-1 Native Model + Capture | CLOSED / PASS | `docs/P8-G4-1-Prefab-Native-Model-Closure.md` |
| P8-G4-2 Strict Reader / Writer | CLOSED / PASS | G4-2 closure + results docs |
| P8-G4-3 Asset Integration | CLOSED / PASS | G4-3 closure + results docs |
| P8-G4-4 Editor Workflow | CLOSED / PASS | G4-4 closure + results docs |
| P8-G4-5 Integrated Regression | CLOSED / PASS | G4-5 report + closure |
| P8-G5 Prefab Instantiate | CLOSED - CORE ACCEPTED | G5 report + closure; ABI count reconciled |
| P8-G6-0 Override/Apply/Revert Map | CLOSED / ACCEPTED | `docs/P8-G6-0-Override-Apply-Revert-Map.md` |
| P8-G6-A Override/Apply Contract | CLOSED / ACCEPTED | `docs/P8-G6-A-Override-Apply-Revert-Contract.md` |
| P8-G6-1 Override Metadata + Field Path | CLOSED / PASS | G6-1 report + closure |
| P8-G6-2 Inspector Override Tracking | CLOSED / PASS | G6-2 report + closure |
| P8-G6-3 Field/Instance Revert | CLOSED / PASS | G6-3 report + closure |
| P8-G6-4-0 Apply/Recovery Readiness | CLOSED / ACCEPTED | `docs/P8-G6-4-0-Implementation-Readiness.md` |
| P8-G6-4-1 Apply Candidate Builder | CLOSED / PASS | G6-4-1 report + closure |
| P8-G6-4-2 Recovery Record | CLOSED / PASS | G6-4-2 report + closure |
| P8-G6-4-3 Phase 1 Prefab Publish | CLOSED / PASS | G6-4-3 report + closure |
| P8-G6-4-4 Phase 2 Scene Metadata Publish | CLOSED / PASS | G6-4-4 report + closure |
| P8-G6-4-5 Startup Recovery | CLOSED / PASS | G6-4-5 report + closure |
| P8-G6-4-6 Apply ABI/UI | CLOSED / PASS | G6-4-6 report + closure |
| P8-G6-4-7 Fault Matrix / Closure | CLOSED / PASS | G6-4-7 report + closure |
| P8-G6-5 Integrated Verification / Closure | CLOSED / PASS | G6-5 report + closure |
| P8-G7-0 Source Reconciliation Map | CLOSED / ACCEPTED | `docs/P8-G7-0-Source-Reconciliation-Map.md` |
| P8-G7-A Source Reconciliation Contract | CLOSED / FROZEN | `docs/P8-G7-A-Source-Reconciliation-Contract.md` |
| P8-G7-1 Source Resolver | CLOSED / ACCEPTED | `docs/P8-G7-1-Source-Resolver-Closure.md` |
| P8-G7-2 Three-way Diff Engine | CLOSED / ACCEPTED | `docs/P8-G7-2-Three-way-Diff-Engine-Closure.md` |
| P8-G7-3 Conflict Model & Resolution Engine | CLOSED / ACCEPTED | `docs/P8-G7-3-Conflict-Model-Resolution-Closure.md` |
| P8-G7-4 Reconciliation Executor | CLOSED / ACCEPTED | `docs/P8-G7-4-Reconciliation-Executor-Closure.md` |
| P8-G7-5 Reconciliation ABI / Managed / UI | SUBMITTED - PENDING ACCEPTANCE | `docs/P8-G7-5-Reconciliation-ABI-Managed-UI-Closure.md` |
| P8-G7-6 Save / Cold Restart / Undo-Redo Integration | READY - NOT STARTED | requires G7-5 acceptance |

Final G4 state:

`P8-G4 — CLOSED — COMPLETE — COMMITTED PREFAB ASSET MODEL & EDITOR WORKFLOW VERIFIED — SCOPE-BOUND`

Final G6 state:

`P8-G6 — PASS — CLOSED — PREFAB OVERRIDE / APPLY / REVERT INTEGRATED — SCOPE-BOUND`

## 2. Delivered capability summary

- Scene hierarchy with stable Scene Entity IDs, parent/child, Keep Local/World,
  subtree delete and undo.
- Hierarchy editor with drag/outdent, expand/collapse, duplicate/subtree clone.
- Committed Prefab model:
  - DFS Local IDs, strict component schemas;
  - canonical `.prefab.json` with full-byte Digest;
  - strict Reader/Writer;
  - `AssetType::Prefab = 4`, Manifest/ContentRegistry, content-addressed files;
  - RevisionStore Publish and committed-Scene boundary.
- ABI v1.15 / 61 ops with `CREATE_PREFAB_FROM_ENTITY = 60`.
- ABI v1.16 / 63 ops with Prefab Instantiate and Local ID -> Scene ID metadata.
- ABI v1.17 / 64 ops (G6-2 Inspector override tracking).
- ABI v1.18 / 65 ops (G6-3 Revert)
- ABI v1.19 / 66 ops (G6-4-6 Apply).
- Real editor Create Prefab workflow, AssetBrowser Prefab type/filter/path/GUID/
  status, and dependency view.
- G4-4 also fixed the G2 ListBox `HierarchyRows` refresh duplication.

## 3. Test evidence policy

G4-5 does not sum historical test counts. Final per-gate evidence is recorded in
each closure; the latest G4-4 snapshot is:

- Native Debug/Release: 1256 executed, 1254 PASS, 1 PICK-01, 1 SDK skip.
- `test_content`: 165 tests, 164 PASS + 1 skip.
- `test_bridge`: 127/127.
- UIA Debug/Release: 13/13.
- Managed static: 384.

Known retained issues:

- PICK-01 (`D3PickRequestPumpFetchHappyPath`).
- SDK shaderc conditional skip.
- One historical GPU transient in `MaterialEditUpdatesProductionPixels`, passed
  on standalone and clean reruns.
- 3D Picking remains unimplemented.


## 4. G5 implementation boundary

P8-G5 implements:

- Prefab Local ID -> Scene Entity ID instance mapping;
- instance anchor/root metadata;
- committed Scene persistence and cold-start rebuild;
- fresh Scene IDs and runtime/GPU isolation;
- Native/ABI instance tests.

Still excluded: Overrides, Apply, Revert, recovery, source reconciliation,
Nested Prefab, and dedicated Instantiate UIA.

Latest G5 evidence: Native 1259 (1257 PASS, 1 PICK-01, 1 SDK skip),
`test_bridge` 130/130, `test_content` 165 (164 PASS + 1 skip), UIA 13/13
Debug/Release (existing workflow regression; no Instantiate UIA claim),
Managed static 384.

## 5. Next phase boundary

P8-G6 Overrides / Apply / Revert remains READY / NOT STARTED. G5 ABI count reconciliation is resolved (63 ops). It must not reopen
G4 format/Digest or G5 Local ID/anchor semantics.

## 6. Git

All P8 changes are uncommitted. No commit, push, reset, clean, or stash was
performed. Dirty submodules and `crashes/` remain untouched.
