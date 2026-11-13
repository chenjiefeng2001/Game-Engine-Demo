# P7-C G0 — Rendering Capability Map (Read-only)

**Status**: COMPLETE — MINIMAL MAP
**Baseline**: P6-1B CLOSED; native 1201/1201 (B6 v1.10)
**Scope**: only production Mesh draw semantics; no re-audit of B1–B6.

## 1. Current span data

- `MeshGpuSpan` (`engine/include/Engine/Core/RHI/MeshGpuResource.h`) already carries
  `firstIndex`, `indexCount`, `materialIndex`.
- `MeshAsset.primitives` / `MeshUploadInput.spans` preserve imported spans (B3/B5).
- **Gap confirmed**: `MeshRuntimeLoader` converted only vertices/indices into CPU `Mesh`;
  spans and per-vertex flags were dropped. `MeshRenderer::GetOrCreateGpuMesh` then fabricated
  one span covering the full index buffer and flags `0x07`.
- Therefore `MeshGpuResource::Spans()` was not the imported span set for runtime meshes.

## 2. Current Material mapping

- `MaterialRuntimeBinding` builds all material slots (`slots[]`) with baseColor/normal GPU textures.
- `MeshRenderer::ApplyMeshMaterialBinding` selected slot 0 only.
- Renderer issued one full-index-range `DrawIndexed`, so every span consumed slot 0.
- Missing later material slots already produced binding warnings, but the resolver rejected
  a missing later slot before binding; runtime failed instead of falling back.

## 3. Draw / path facts

- `IRenderContext::DrawIndexed` had no range; `OpenGLContext::DrawIndexed` calls
  `glDrawElements(GL_TRIANGLES, allIndices, GL_UNSIGNED_INT, nullptr)`.
- Production color paths in `MeshRenderer::Render` call full `DrawIndexed` after applying slot 0.
- Direct and RenderGraph both call the same `MeshRenderer::Render`; no separate draw model.
- RenderGraph FBO bind/restore and Direct fallback are B5/P4-E verified; no new defect observed.
- Current shader consumes baseColor/normal; no metallic/roughness/emissive/alphaMode runtime consumption.

## 4. Reusable contracts

- `MeshGpuCache` validates spans (non-empty, ordered, in range) and computes payload digest from
  actual spans/flags.
- `MeshMaterialBinding::slots` retains all slots; `MaterialBindingEntry` gains `available`.
- `MeshRuntimeLoader` is the single runtime assembly point.
- `MeshRuntimeScene` already reflects load state/counts into `MeshComponent`.

## 5. Candidate changes and minimal acceptance

| Candidate | Minimal acceptance |
|---|---|
| Propagate spans/flags into CPU Mesh and GPU key | Runtime GPU resource spans equal committed primitives |
| Per-span draw range | Each span drawn once; offsets/counts bounded |
| Span→material slot | Two spans/two materials produce distinct pixel regions |
| Missing slot fallback | Later missing slot falls back to slot 0; status Ready; warning retained |
| Direct/RG consistency | Same probes/background in both paths |
| Invalid spans | Fail closed, no out-of-range draw |

## 6. Not claimed / out of scope

- No new PBR/material fields, blending, culling, or alpha-mode consumption.
- No RenderGraph architecture change, async/multithreaded submission, Vulkan/D3D12.
- No performance improvement claim; multi-span draw-call cost was not benchmarked.
- `m_MeshCache_RHI` remains the known non-production pointer-key exception.

## 7. Decision

Proceed with P7-C1 (span/material production draw) using only C++ internal changes;
**no Bridge C ABI change is required**. RenderGraph qualification reuses existing
B5 pixel evidence and adds multi-material RG pixel coverage.
