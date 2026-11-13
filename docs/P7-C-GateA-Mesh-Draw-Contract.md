# P7-C Gate A — Mesh Draw Contract (Frozen)

**Status**: FROZEN
**Applies to**: production Direct and RenderGraph paths.

## 1. Primitive spans

- A runtime Mesh resource carries the committed span set:
  `firstIndex`, `indexCount`, `materialIndex`.
- Spans are validated by `MeshGpuCache::ValidatePayload`: non-empty, non-decreasing,
  `firstIndex + indexCount <= indexCount`; empty/invalid payload fails upload.
- Each valid span issues exactly one indexed draw range.
- Zero-count/out-of-range spans never issue a draw.
- The old synthetic single-span path remains only for procedural/legacy meshes with no spans.

## 2. Material slot mapping

- `span.materialIndex` selects `MeshMaterialBinding::slots[materialIndex]` when that slot
  is available.
- `0xFFFFFFFF` (unspecified) or an invalid/missing later slot falls back to slot 0.
- Slot 0 missing/invalid remains fail-closed at load time (`MaterialBindingFailure`).
- Fallback is a draw policy only; it does not rewrite Material assets or Mesh JSON.
- Valid multiple materials are drawn per span; B5's “first material only” behavior is
  superseded only for spans that carry valid slots. B5 historical documents are unchanged.

## 3. Draw state

- Existing state remains: depth test/write managed by production caller, culling/blending
  unchanged, alpha output from the current shader.
- No new PBR fields, alpha blending, culling policy, or material parameter consumption is
  introduced in P7-C.
- Material baseColor/normal binding per slot uses the existing G3 path.

## 4. Direct / RenderGraph

- Both paths use the same `MeshRenderer::Render` and same per-span loop.
- Same Mesh/Material/Camera input must produce the same span/material selection and
  probe pixels within tolerance.
- RenderGraph must not change runtime ownership or FBO bind/restore semantics.

## 5. ABI / persistence

- No Bridge C ABI change.
- No Scene format or GUID change.
- C++ additive changes only: `Mesh` span/flags storage, `DrawIndexedRange` default interface,
  renderer span loop, binding `available` flag.
- Material/Mesh asset formats unchanged.

## 6. Acceptance matrix

| Case | Required evidence |
|---|---|
| Single span | B5 baseline pixels unchanged |
| Two spans/two materials | left/right probe colors distinct, Direct + RG |
| Missing later material | slot-0 fallback pixels, status Ready, binding warning |
| First material missing | load fails closed |
| Span bounds | cache validates; no out-of-range draw |
| Multi-entity same GUID | shared GPU/binding; transforms independent |
| Reimport | new span/material set and pixels after revision |
| FBO/RG | existing B5/P4-E tests remain green |
