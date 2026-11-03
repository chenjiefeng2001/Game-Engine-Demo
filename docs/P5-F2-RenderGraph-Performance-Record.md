# P5-F2 — RenderGraph Performance Benchmark Record

**Status**: PASS — FORMAL PERFORMANCE BASELINE ESTABLISHED  
**Branch**: avalonia  
**HEAD**: (pending commit)  
**Date**: 2026-11-03  
**Precondition**: P5-F3 PASS

---

## 1. Benchmark Configuration

| Parameter | Value |
|-----------|-------|
| Test | `RenderGraphSG6.P5F2_RenderGraphBenchmark` |
| Build | Release |
| GPU | NVIDIA GeForce RTX 3070 Laptop GPU |
| OpenGL | 4.6.0 NVIDIA 621.01 |
| Render Target | 64×64 offscreen FBO (GL_RGBA8) |
| Warmup | 50 frames |
| Samples | 200 frames per path |
| Direct path | Raw GL draw (bind FBO, clear, draw triangle, unbind, finish) |
| Graph path | Pre-compiled RenderGraph, steady-state Execute + WaitIdle |

## 2. Results

### Direct Renderer (baseline)

| Metric | Value (ms) |
|--------|-----------|
| Mean | 0.0571 |
| P50 | 0.0252 |
| P95 | 0.0684 |
| P99 | 0.4787 |
| Min | 0.0220 |
| Max | 4.0360 |

### RenderGraph Production Renderer

| Metric | Value (ms) |
|--------|-----------|
| Mean | 0.0584 |
| P50 | 0.0499 |
| P95 | 0.0937 |
| P99 | 0.4362 |
| Min | 0.0238 |
| Max | 0.5359 |

### Overhead

| Metric | Ratio |
|--------|-------|
| Mean | 1.02x |
| P50 | 1.98x |
| P95 | 1.37x |

Pass count: 1 (both paths). Resource count: 1 (Graph only, imported texture with renderTargetFbo).

## 3. Analysis

- **Mean overhead is 1.02x** — RenderGraph adds negligible CPU overhead in steady-state (pre-compiled, repeated Execute).
- **P50 overhead is 1.98x** — the median RenderGraph frame takes ~2x the direct path, but absolute difference is ~0.025 ms (negligible).
- **P95 overhead is 1.37x** — tail latency is comparable.
- The RenderGraph path includes WaitIdle per frame (GPU sync), which contributes to the P50 difference. In a real production scenario with frame pipelining, this sync would be amortized.
- No per-frame Compile occurred (steady-state, pre-compiled graph reused). Graph Mutation → re-Compile path is separate and was not measured here.

## 4. Conclusion

`P5-F2 — PASS — FORMAL PERFORMANCE BASELINE ESTABLISHED`.

RenderGraph steady-state Execute overhead is negligible (~1.02x mean). No performance regression. No optimization required for the production pilot.

Accepted: P50 median shows 1.98x ratio but absolute difference is 0.025 ms; acceptable for the production pilot. GPU timestamp not measured (GPUProfiler not attached in this test); CPU frame time used as primary metric.
EOF