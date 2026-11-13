# P6-1B Asset Import Delivery Index (B1–B6)

**Status**: P6-1B — CLOSED — COMPLETE — SCOPE-BOUND
**Branch/HEAD**: avalonia / `5de8f1a`（B6 未 commit/push）

This is an index only; historical Gate settlements remain authoritative and are not rewritten.

| Stage | Scope | Settlement / record |
|---|---|---|
| B1 | glTF/GLB parser + shared import IR | `docs/P6-1B-GateB1-Parser-Record.md` |
| B2 | OBJ/MTL parser + contract | `docs/P6-1B-GateB2-ObjMtl-Record.md` |
| B3 | Import commit, GUID stability, digest v2, Publish | `docs/P6-1B-GateB3-Commit-Record.md` |
| B4 | Texture preprocessing / sidecar / sRGB classification | `docs/P6-1B-GateB4-Texture-Preprocess-Record.md` |
| B5 | Runtime Mesh: G0 map, G1 reader, G2 GPU cache, G3 material/texture binding, G4 MeshComponent runtime, G5 production pixels, G6 closure | `docs/P6-1B-GateB5-Runtime-Mesh-Closure.md` + G0–G5 documents |
| B6 | AssetBrowser, Add Mesh to Scene, Inspector, persistence, real UIA + viewport | `docs/P6-1B-GateB6-Editor-Model-Closure.md` + G0–G5 documents |

## ABI increment (explicit)

B6 delivered a **minimal additive Bridge ABI increment v1.10** — this is a real ABI change, not "ABI unchanged":

- `EditorSession_GetAssetType`: Material now returns `3` (Mesh remains `2`).
- `EditorSession_GetAssetStatus` export (committed asset availability).
- `ENGINE_HOST_OP_GET_ASSET_STATUS = 32`; op count 33.
- `RuntimeResult` layout (76B), existing op numbers/status values, and packet layouts are unchanged.
## Current production chain

Import (OBJ/MTL/glTF/GLB) → TexturePreprocess/MeshAssetWriter → single RevisionStore Publish →
committed manifest current → MeshAssetResolver (committed-only) → MeshAssetReader → MeshUploadAdapter →
MeshGpuCache → MaterialRuntimeBinding/TextureGpuCache → MeshRenderer →
Direct/RenderGraph → presentation FBO → production Viewport.

Editor: committed asset list → MeshComponent.meshGuid (Scene v1) → Save/Reload →
MeshRuntimeScene resolves current → Inspector status → real UIA test verifies production FBO pixels.

## Current baseline

- Native: 1201 discovered = 1201 executed; 1199 PASS / 1 FAIL (PICK-01) / 1 SKIP (shaderc).
- Managed static: 379 tests; B6 real UIA 1/1 PASS Debug/Release.
- Known accepted: PICK-01, SDK conditional SKIP, v1 first-material policy, non-normal data map shader consumers, non-production `m_MeshCache_RHI` exception.
