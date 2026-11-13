# P7-A G5 — Reimport Workflow Closure

**Status**: PASS — SCOPE-BOUND

- UI actions: **Reimport Source** and **Reimport (Discard Edits)**, enabled for a selected Mesh.
- Native `ReimportAsset` resolves Mesh JSON `sourceFile`, validates project-root/file existence,
  checks edited-material markers, and invokes the existing obj/gltf/glb import pipeline.
- Normal reimport is blocked when any referenced Material has local edits; diagnostic is explicit.
- Discard path bypasses the exact-path fast path, resets source Material content, preserves GUID,
  and clears edit markers only after successful import.
- Native tests:
  - `ReimportSourceChangedAndMissingFailClosed`: changed source updates revision/path with stable
    GUID; deleted source fails and leaves revision untouched.
  - `MaterialEditCommitColdRestartAndReimportGuard`: blocked normal reimport; forced reimport resets
    source values and advances revision.
- UIA test invokes the real Reimport Source button in the running editor.
- Publish atomicity is inherited from B3/B4 `RevisionStore::Publish`; no partial commit path added.
