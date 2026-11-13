# P7-C — Production Rendering Capability Closure

**Status**: P7-C — CLOSED — COMPLETE — PRODUCTION RENDERING VERIFIED — SCOPE-BOUND
**Branch/HEAD**: avalonia / `5de8f1a`（P7-C 未 commit/push）
**Precondition**: P6-1B CLOSED; B6 ABI v1.10 baseline

## 1. Scope delivered

- Runtime Mesh keeps committed primitive spans and vertex flags.
- Production renderer draws each valid span with its material slot.
- Direct and RenderGraph use the same span/material draw path.
- Missing/invalid later material slots fall back to slot 0; slot 0 missing remains fail-closed.
- Existing B5 single-material pixel behavior remains unchanged for single-span / unspecified-slot meshes.

## 2. Implementation

| Change | File |
|---|---|
| CPU Mesh carries spans + vertex flags | `engine/include/Engine/Core/Renderer/Mesh.h` |
| Runtime loader propagates spans/flags | `engine/src/AssetImport/MeshRuntimeLoader.cpp` |
| GPU resource uses committed spans/flags (not synthetic) | `engine/src/Core/RHI/MeshRenderer.cpp` |
| Per-span draw range API | `engine/include/Engine/Core/IRenderContext.h`, `engine/src/OpenGL/OpenGLContext.cpp` |
| Per-span material slot selection / fallback | `engine/src/Core/RHI/MeshRenderer.cpp`, `MaterialRuntimeBinding.h` |
| Optional later material slots (slot0 remains mandatory) | `engine/src/AssetImport/MeshMaterialResolver.cpp` |
| Runtime warnings surfaced on component | `MeshComponent.h`, `MeshRuntimeScene.cpp` |

No Bridge C ABI, Scene format, Mesh/Material asset format, GUID or RevisionStore change.

## 3. Contract

- Span order is preserved; each valid span draws exactly once with its `firstIndex/indexCount`.
- `span.materialIndex` selects the binding slot when available; otherwise falls back to slot 0.
- Empty/invalid/out-of-range spans fail upload validation; zero-count spans never draw.
- Procedural/legacy meshes without spans keep the synthetic single-span behavior.
- Material fallback is render policy only; `runtimeWarnings` reports missing later slots.
- Draw state remains the existing shader contract (baseColor/normal consumed; metallic/roughness/emissive/alphaMode/blending not expanded).

## 4. Evidence

### 4.1 Production pixels (`test_mesh_pixel`, Debug + Release = 10/10)

- **Single span**: existing sRGB/GLB/Direct+RG probes unchanged.
- **Multi span / two materials** (`MultiSpanRendersDistinctMaterialSlots`):
  left probe = material 0 ambient red, right probe = material 1 ambient blue;
  Direct and RenderGraph both match; span offsets/counts from committed mesh JSON asserted (0/6, 6/6).
- **Fallback** (`MissingLaterMaterialFallsBackToSlotZero`):
  committed slot-1 material removed; runtime status Ready; right span renders slot-0 red
  instead of silently blue/missing; Direct + RenderGraph.
- Background probes and coverage remained valid; no clear-only or stale-frame pass.
- B6 real-editor UIA remained 1/1 PASS Debug/Release with the new renderer path (single material).

### 4.2 Builds / regression

- CMake Debug full and Release full: success.
- EditorBridge Debug/Release: success; `editor_avalonia` Debug/Release: success.
- Native inventory Debug == Release:
  **1202 discovered = 1202 executed; 1200 PASS / 1 FAIL (PICK-01) / 1 SKIP (shaderc)**.
- `test_mesh_pixel`: 10/10 both configs.
- `test_assetimport`: 148/148 both configs.
- B6 UIA: 1/1 Debug, 1/1 Release.

## 5. RenderGraph qualification

- Same `MeshRenderer::Render` span/material loop is used by Direct and RenderGraph.
- Multi-material Direct/RG probes match within ±2.
- Existing P4-E/B5 FBO bind/restore and failure behavior tests remain green.
- No RenderGraph architecture, resource ownership, async submission, or profiler change.

## 6. Limits / known unverified

- **Performance**: multi-span draw-call cost is not benchmarked; no performance claim made.
- **Draw state**: culling/blending/alphaMode/metallic-roughness/emissive remain outside current
  production shader consumption; only existing baseColor/normal behavior is claimed.
- **Fallback diagnostics**: binding warnings are mirrored to `MeshComponent.runtimeWarnings`;
  no dedicated UI panel was added.
- **Invalid span source**: committed B3 writer emits contiguous spans; out-of-range validation is
  covered by cache/adapter fail-closed, not by a malicious committed fixture.
- PICK-01 and shaderc SKIP remain the accepted baseline exceptions.
- `m_MeshCache_RHI` non-production pointer-key exception unchanged.
- Vulkan/D3D12 Mesh rendering not supported.

## 7. Decision

**P7-C — CLOSED — COMPLETE — PRODUCTION RENDERING VERIFIED — SCOPE-BOUND**

P7-A/P7-B were not started; they require separate authorization and contracts.
