# P8 G7-5 - Reconciliation ABI / Managed / Editor Workflow Closure

**Status**: SUBMITTED - PENDING ACCEPTANCE
**Gate**: P8-G7-5 Reconciliation ABI / Managed / Editor Workflow
**Baseline**: G7-4 CLOSED / G7-A FROZEN
**Scope**: ABI v1.20, Managed service, Inspector workflow and targeted real-process UIA.

## 1. ABI v1.20 / 70 ops

`ENGINE_HOST_ABI_VERSION = (1 << 16) | 20`; `ENGINE_HOST_OP_COUNT = 70`.

Append-only operations 66-69:

| ID | Operation | Shape |
|---|---|---|
| 66 | `ENGINE_HOST_OP_GET_PREFAB_RECONCILIATION_STATUS` | Fetch; root entity ID string; JSON status |
| 67 | `ENGINE_HOST_OP_PREVIEW_PREFAB_RECONCILIATION` | Fetch; root entity ID string; JSON deterministic plan |
| 68 | `ENGINE_HOST_OP_APPLY_PREFAB_RECONCILIATION` | Dispatch; root entity ID string; no unresolved conflicts |
| 69 | `ENGINE_HOST_OP_RESOLVE_RECONCILIATION_CONFLICT` | Dispatch; `root;conflictId;action`, optional value JSON in strArg2 |

Existing operations 0-65 are unchanged. Status and preview use Fetch and do
not mutate Scene/source. Apply and Resolve use Dispatch and return the standard
RuntimeResult.

## 2. Status / preview JSON

Status exposes:

- source revision/digest/path and legacy flag;
- `outOfDate`, `sourceUnavailable`, `sourceUnavailableReason`;
- `syncState`, Override count;
- `persistedConflictCount`, persisted conflict list, stable IDs;
- current plan conflict count as non-mutating preview metadata.

Preview exposes Base/Latest identity, `outOfDate`, and the full deterministic
plan: fields, structural actions, rebase decisions, conflicts and validation
errors. Preview never persists conflict records.

`Resolve` can bootstrap the persisted conflict state from the current preview
plan when no state exists; once at least one conflict remains it is persisted
with the stable ID. This keeps Preview Fetch-only while allowing the UI to
resolve the conflict shown in the preview.

## 3. Managed / UI workflow

`EditorHostService` / `IEditorOps` add:

- `GetPrefabReconciliationStatus`
- `PreviewPrefabReconciliation`
- `ApplyPrefabReconciliation`
- `ResolvePrefabReconciliationConflict`

Inspector additions:

- `PrefabReconciliationStatus` shows Up to date / Out of date / Source
  unavailable and persisted conflict count;
- `PrefabConflictList` shows stable conflict IDs and detail;
- `Preview Source`, `Apply Reconciliation`;
- `Take Source`, `Keep Local`, `Manual Edit` with JSON manual value input.

Apply is enabled only after a preview and with zero unresolved preview conflicts;
resolution refreshes Inspector status and Scene values. The production
EngineHost channel implements all four operations. The legacy Session fallback
does not implement the new reconciliation workflow.

## 4. Targeted UIA evidence

New real-process test:

`tests/EditorAvalonia.IntegrationTests/B6MeshEditorUITests.Prefab_reconciliation_status_preview_and_apply`

It prepares an out-of-date instance with a local field conflict, verifies:
Out-of-date status -> Preview -> stable `field:` conflict row -> Take Source
-> conflict list cleared -> Apply Reconciliation -> Up to date + PosX=5.0.

UIA Debug: 1/1 PASS.
UIA Release: 1/1 PASS.
Existing UIA suite was not rerun; this is targeted evidence for the new workflow.

## 5. Evidence / ABI hygiene

- Native Debug/Release: `1303 executed / 1301 PASS`; 1 known PICK-01 and 1 SDK
  skip.
- `test_content`: `183 tests / 182 PASS + 1 skip`.
- `test_bridge`: `156/156`.
- Managed static: `386`; CTest entries: `22`.
- ABI version: v1.20 / 70 ops.
- `RuntimeResult` and packet layouts unchanged.
- UIA targeted Debug/Release: 1/1 each.

Existing PICK-01 and SDK skip are historical registrations.

## 6. Scope limits / next gate

G7-5 does not auto-update all Instances, publish Prefab assets, add new
structural conflict types, or support structural Manual Edit beyond the
G7-3/G7-4 policies. Hierarchy rows do not carry a separate reconciliation badge;
the Inspector is the workflow surface. G7-6 owns Save/cold-restart/Undo/Redo
integration of the reconciled state and G7-7 owns whole-gate closure/
integration verification.

**Next**: P8-G7-6 Save / Cold Restart / Undo-Redo Integration.