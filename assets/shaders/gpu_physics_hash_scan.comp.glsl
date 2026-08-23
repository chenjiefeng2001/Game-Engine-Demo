#version 460 core
// ════════════════════════════════════════════════════════════════
// gpu_physics_hash_scan.comp.glsl — 单工作组排他前缀和（Phase 4-B）
//
// 本文件是运行时着色器的唯一来源（构建期嵌入 GL46ShaderSources.g.h）。
//
// ── 绑定契约 ────────────────────────────────────────────────
// PSO 注册名     : "gpu_physics_hash_scan"   （Nsight 标签: CS_HashScan）
// SSBO binding 1 : CountBuf  — uint cellCount[]（输入：每 cell 粒子数）
// SSBO binding 2 : StartBuf  — uint cellStart[]（输出：exclusive prefix sum）
//
// ── Uniform 契约 ────────────────────────────────────────────
//   u_CellCount uint — 网格总 cell 数（≤ 65536，CPU 侧保证）
//
// ── 算法 ────────────────────────────────────────────────────
// 单工作组分块串行扫描：
//   每线程负责一段连续 chunk（chunk 内串行累加）→ 共享内存汇总 →
//   tid0 对 chunk 总量做排他扫描 → 各线程回写自己 chunk 的绝对起点。
// 复杂度 O(C / 512 + C) @ 单工作组；C ≤ 65536 时 < 0.05ms。
// ════════════════════════════════════════════════════════════════

layout(local_size_x = 512, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 1) buffer CountBuf {
    uint cellCount[];
};

layout(std430, binding = 2) writeonly buffer StartBuf {
    uint cellStart[];
};

uniform uint u_CellCount;

shared uint chunkSum[512];
shared uint chunkBase[512];

void main() {
    const uint tid  = gl_LocalInvocationID.x;
    const uint nT   = 512u;
    const uint cCnt = u_CellCount;
    const uint chunkLen = (cCnt + nT - 1u) / nT;

    // ── Pass 1: chunk 内串行求和 ──
    uint sum = 0u;
    {
        const uint begin = tid * chunkLen;
        const uint end   = min(begin + chunkLen, cCnt);
        for (uint c = begin; c < end; ++c) sum += cellCount[c];
    }
    chunkSum[tid] = sum;
    barrier();

    // ── Pass 2: tid0 对 chunk 总量做排他扫描 ──
    if (tid == 0u) {
        uint run = 0u;
        for (uint t = 0; t < nT; ++t) {
            chunkBase[t] = run;
            run += chunkSum[t];
        }
    }
    barrier();

    // ── Pass 3: 回写本 chunk 每 cell 的绝对起点 ──
    uint run = chunkBase[tid];
    {
        const uint begin = tid * chunkLen;
        const uint end   = min(begin + chunkLen, cCnt);
        for (uint c = begin; c < end; ++c) {
            cellStart[c] = run;
            run += cellCount[c];
        }
    }
}
