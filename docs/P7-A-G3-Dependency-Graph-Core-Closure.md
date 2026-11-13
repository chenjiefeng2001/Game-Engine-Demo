# P7-A G3 — Dependency Graph Core Closure

**Status**: PASS — WITH SOURCE-PROVENANCE LIMIT

- Native `GetAssetDependencies` returns JSON: asset identity, `dependencies`, `dependents`.
- Mesh deps: source file, Material per slot, Material Texture refs via slot/role.
- Material deps: all supported texture slots.
- Reverse edges scan committed Mesh/Material assets by path/GUID; no runtime cache dependency.
- `resolved` reflects project-root file availability; missing targets remain listed.
- Tests: `ImportCommitTest.AssetDependenciesReportMeshMaterialTexture` verifies Mesh→Material/
  Source/Texture and reverse Material→Mesh / Texture→Material edges.
- Limitation: MTL/external buffer source dependency files are not persisted in v1 Mesh assets;
  only `sourceFile` provenance is available.
