# P7-A G1 — Material Editing Core Closure

**Status**: PASS

- Native `EditorSession::GetMaterialJson` reads committed Material bytes.
- `UpdateMaterial` applies only allowed scalar/array fields, validates finite values and
  alphaMode, re-validates via `MaterialAssetReader`, writes content-addressed new Material path,
  rebinds the same GUID in a candidate registry, and publishes once.
- Identical content is a no-op (zero revision).
- Edit marker persisted at `.engine-content/material-edits.json`.
- Tests: `ImportCommitTest.MaterialEditCommitColdRestartAndReimportGuard` covers field round-trip,
  invalid/unknown input, revision delta, GUID/path, no-op, cold restart, edit marker, guarded
  reimport and forced reset.
- ABI: additive v1.11 exports/ops; no asset format change.
