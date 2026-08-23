#version 460 core
// ════════════════════════════════════════════════════════════════
// gpu_physics_hash_clear.comp.glsl — 网格计数清空（Phase 4-B）
//
// 本文件是运行时着色器的唯一来源（构建期嵌入 GL46ShaderSources.g.h）。
//
// ── 绑定契约 ────────────────────────────────────────────────
// PSO 注册名     : "gpu_physics_hash_clear"  （Nsight 标签: CS_HashClear）
// SSBO binding 0 : CountBuf — uint cellCount[]，每元素写 0
//
// ── 设计说明 ────────────────────────────────────────────────
// Broad-phase 采用"计数排序"而非链表桶：
//   - 仅依赖 atomicAdd（atomicExch 在部分编译器存在重载解析问题）
//   - 排序后邻域遍历为连续内存访问，缓存友好性优于链表随机跳转
// 约束：网格总 cell 数 ≤ 65536（CPU 侧自适应 cellSize 保证），
//       使 Scan 可在单工作组内完成。
// ════════════════════════════════════════════════════════════════

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 0) writeonly buffer CountBuf {
    uint cellCount[];
};

uniform uint u_CellCount;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_CellCount) return;
    cellCount[idx] = 0u;
}
