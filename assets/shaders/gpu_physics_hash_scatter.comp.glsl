#version 460 core
// ════════════════════════════════════════════════════════════════
// gpu_physics_hash_scatter.comp.glsl — 粒子散射进有序槽位（Phase 4-B）
//
// 本文件是运行时着色器的唯一来源（构建期嵌入 GL46ShaderSources.g.h）。
//
// ── 绑定契约 ────────────────────────────────────────────────
// PSO 注册名     : "gpu_physics_hash_scatter" （Nsight 标签: CS_HashScatter）
// SSBO binding 2 : CellOfBuf — uint cellOf[]
// SSBO binding 3 : OffsetBuf — uint offset[]
// SSBO binding 4 : StartBuf  — uint cellStart[]
// SSBO binding 5 : SortedBuf — uint sortedIdx[]（输出：按 cell 分组的粒子索引）
//
// 无原子操作：slot = cellStart[cellOf[i]] + offset[i]，各槽位互斥。
// ════════════════════════════════════════════════════════════════

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 2) readonly buffer CellOfBuf {
    uint cellOf[];
};

layout(std430, binding = 3) readonly buffer OffsetBuf {
    uint offset[];
};

layout(std430, binding = 4) readonly buffer StartBuf {
    uint cellStart[];
};

layout(std430, binding = 5) writeonly buffer SortedBuf {
    uint sortedIdx[];
};

uniform uint u_ParticleCount;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_ParticleCount) return;
    sortedIdx[cellStart[cellOf[idx]] + offset[idx]] = idx;
}
