# P8 G4-5 - Integrated Regression & G4 Closure

**Status**: PASS - CLOSED - INTEGRATED EVIDENCE ACCEPTED
**Integrated report**: `docs/P8-G4-5-Integrated-Regression-Report.md`
**G4 scope**: Committed Prefab Asset Model + minimal editor creation workflow.
**G5 scope remains untouched**: no Instantiate or Instance metadata.

## 1. Closure decision

All G4 exit conditions are satisfied by the integrated evidence:

- Prefab Native Model and Scene subtree Capture;
- Local ID mapping, strict component validation;
- canonical Writer, strict Reader, full-byte Digest;
- `AssetType::Prefab = 4`, Manifest/registry registration;
- content-addressed `.prefab.json` storage;
- RevisionStore Publish with committed Scene bytes and fault matrix;
- ABI v1.15 / 61 ops with `CREATE_PREFAB_FROM_ENTITY = 60`;
- real Hierarchy Create Prefab workflow;
- AssetBrowser type/filter/path/GUID/status and dependency view;
- Debug/Release UIA 13/13.

No new G4 source, test, CMake, ABI, or UI change was made in G4-5. No full test
rerun is claimed by this gate; the report uses historical gate evidence and a
chain/status audit.

## 2. Final status

`P8-G4 — CLOSED — COMPLETE — COMMITTED PREFAB ASSET MODEL & EDITOR WORKFLOW VERIFIED — SCOPE-BOUND`

## 3. Retained known limitations

- PICK-01 and SDK shaderc skip remain historical.
- GPU transient in `MaterialEditUpdatesProductionPixels` is recorded, not
  reclassified.
- Prefab file bytes are outside RevisionStore current hashes; orphan candidates
  are allowed and unregistered.
- An arbitrary target path is not offered (G4-A fixed `assets/prefabs`).
- No Prefab Instance, Local ID runtime mapping, Overrides, Apply/Revert, or
  source reconciliation.
- No same-GUID Prefab update workflow; create allocates a new asset GUID.
- Legacy Sprite/Script bindings remain fail-closed.
- Missing dependencies remain unresolved refs for G5.
- 3D Picking remains a G2 limitation.
- No dedicated Prefab instantiation pixel test (G5/G8).

## 4. Next stage

`P8-G5 Prefab Instantiate` is READY / NOT STARTED.

G5 must add:

- Prefab Local ID -> Scene Entity ID mapping;
- Instance anchor/root metadata;
- committed Scene persistence and cold-start rebuild;
- runtime ID/GPU isolation;
- promote/undo behavior as separately contracted.

G5 must not reinterpret G4's Local IDs, file format, or Digest contract.

## 5. Git

G4 cumulative changes remain uncommitted. No commit, push, reset, clean, or
stash was performed.
