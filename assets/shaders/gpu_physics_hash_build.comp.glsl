#version 460 core
// ════════════════════════════════════════════════════════════════
// gpu_physics_hash_build.comp.glsl — 计数阶段（Phase 4-B）
//
// 本文件是运行时着色器的唯一来源（构建期嵌入 GL46ShaderSources.g.h）。
//
// ── 绑定契约 ────────────────────────────────────────────────
// PSO 注册名     : "gpu_physics_hash_build"  （Nsight 标签: CS_HashBuild）
// SSBO binding 0 : InBuf    — readonly，积分后粒子快照
// SSBO binding 1 : CountBuf — uint cellCount[]，本 pass 前 clear 过
// SSBO binding 2 : CellOfBuf  — uint cellOf[]，每粒子所属 cell
// SSBO binding 3 : OffsetBuf  — uint offset[]，每粒子在 cell 内的序号
//
// ── Uniform 契约 ────────────────────────────────────────────
//   u_ParticleCount / u_GridMin / u_CellSize / u_GridDim(vec3)
// ════════════════════════════════════════════════════════════════

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

struct Particle {
    vec3  position;
    float radius;
    vec3  velocity;
    float mass;
    vec4  color;
    float padding[4];
};

layout(std430, binding = 0) readonly buffer InBuf {
    Particle particles[];
} inBuf;

layout(std430, binding = 1) buffer CountBuf {
    uint cellCount[];
};

layout(std430, binding = 2) writeonly buffer CellOfBuf {
    uint cellOf[];
};

layout(std430, binding = 3) writeonly buffer OffsetBuf {
    uint offset[];
};

uniform uint  u_ParticleCount;
uniform vec3  u_GridMin;
uniform float u_CellSize;
uniform vec3  u_GridDim;   // 整数维度以 float 传递（< 2^24 精确）

uint linearCell(vec3 pos) {
    // 越界钳制：边界碰撞保证粒子位于 [boxMin+r, boxMax-r]，此处仅防御浮点误差
    ivec3 g = ivec3(floor((pos - u_GridMin) / u_CellSize));
    g = clamp(g, ivec3(0), ivec3(u_GridDim) - ivec3(1));
    return uint(g.x)
         + uint(g.y) * uint(u_GridDim.x)
         + uint(g.z) * uint(u_GridDim.x) * uint(u_GridDim.y);
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_ParticleCount) return;

    uint c = linearCell(inBuf.particles[idx].position);
    cellOf[idx]  = c;
    offset[idx]  = atomicAdd(cellCount[c], 1u);   // cell 内序号（乱序但唯一）
}
