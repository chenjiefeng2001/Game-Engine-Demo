#version 460 core
// ════════════════════════════════════════════════════════════════
// gpu_physics_collide_spatial.comp.glsl — 27-Cell 邻域 Jacobi 碰撞（Phase 4-C/D）
//
// 本文件是运行时着色器的唯一来源（构建期嵌入 GL46ShaderSources.g.h）。
//
// ── 绑定契约 ────────────────────────────────────────────────
// PSO 注册名     : "gpu_physics_collide_spatial" （Nsight 标签: CS_CollideSpatial）
// SSBO binding 0 : InBuf     — readonly，稳定输入快照（与 brute-force 版一致）
// SSBO binding 1 : OutBuf    — 碰撞修正结果
// SSBO binding 4 : StartBuf  — uint cellStart[]（排他前缀和，长度 = cell 数）
// SSBO binding 5 : SortedBuf — uint sortedIdx[]（按 cell 分组的粒子索引）
//
// ── Uniform 契约（与 RecordCollideSpatialPass 对应）──────────
//   u_Dt            float  （未消费，保持与 brute-force 内核绑定兼容）
//   u_Restitution   float
//   u_ParticleCount uint
//   u_GridMin / u_CellSize / u_GridDim(vec3)  — 与 hash_build 相同
//
// ── 语义约束（Phase 4 核心架构红线）─────────────────────────
// 求解器语义与 brute-force 版【逐行一致】：
//   - Jacobi 快照：全部线程只读 InBuf，写 OutBuf，无原位修改
//   - 无条件写回所有槽位
//   - 冲量公式 / 质量加权位置修正 / 弹性系数完全相同
// 空间哈希仅改变"评估哪些候选对"：
//   候选集 = 27 个邻接 cell 的有序分段 [cellStart[c], cellStart[c+1])
//   完备性条件：cellSize ≥ 2×maxRadius ⇒ 所有可能接触对必然落在
//   27-cell 邻域内（Ring7 由 1-step Δv/Δpos 门禁验证）
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

layout(std430, binding = 1) writeonly buffer OutBuf {
    Particle particles[];
} outBuf;

layout(std430, binding = 4) readonly buffer StartBuf {
    uint cellStart[];
};

layout(std430, binding = 5) readonly buffer SortedBuf {
    uint sortedIdx[];
};

uniform float u_Dt;
uniform float u_Restitution;
uniform uint  u_ParticleCount;
uniform uint  u_CellCount;
uniform vec3  u_GridMin;
uniform float u_CellSize;
uniform vec3  u_GridDim;   // 整数维度以 float 传递（< 2^24 精确）

ivec3 cellCoords(vec3 pos) {
    ivec3 g = ivec3(floor((pos - u_GridMin) / u_CellSize));
    return clamp(g, ivec3(0), ivec3(u_GridDim) - ivec3(1));
}

uint encodeCell(ivec3 g) {
    return uint(g.x) + uint(g.y) * uint(u_GridDim.x)
         + uint(g.z) * uint(u_GridDim.x) * uint(u_GridDim.y);
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_ParticleCount) return;

    Particle p = inBuf.particles[idx];
    vec3 originPos = p.position;   ///< 真 Jacobi：距离始终基于快照位置（与 brute 内核一致）
    uint probeEval = 0u;   // DEBUG PROBE (Phase 4)
    uint probeFix  = 0u;   // DEBUG PROBE (Phase 4)

    const ivec3 base = cellCoords(p.position);

    // ── 27-cell 邻域遍历（连续内存分段）──
    for (int oz = -1; oz <= 1; ++oz)
    for (int oy = -1; oy <= 1; ++oy)
    for (int ox = -1; ox <= 1; ++ox) {
        ivec3 g = base + ivec3(ox, oy, oz);
        if (any(lessThan(g, ivec3(0))) || any(greaterThanEqual(g, ivec3(u_GridDim))))
            continue;                                   // 网格外

        const uint c  = encodeCell(g);
        const uint cs = cellStart[c];
        const uint ce = (c + 1u < u_CellCount) ? cellStart[c + 1u] : u_ParticleCount;

        for (uint k = cs; k < ce; ++k) {
            const uint j = sortedIdx[k];
            if (j == idx) continue;

            Particle other = inBuf.particles[j];
            vec3 diff = other.position - originPos;
            float dist = length(diff);
            float minDist = p.radius + other.radius;

            if (dist < minDist && dist > 0.0001) {
                vec3 normal = diff / dist;
                float overlap = minDist - dist;
                float totalMass = p.mass + other.mass;
                if (totalMass > 0.0001) {
                    float pWeight = other.mass / totalMass;
                    p.position -= normal * overlap * pWeight;

                    vec3 relVel = p.velocity - other.velocity;
                    float velAlongNormal = dot(relVel, normal);
                    if (velAlongNormal < 0.0) {
                        vec3 impulse = normal * velAlongNormal
                                     * (1.0 + u_Restitution) / totalMass;
                        p.velocity -= impulse * other.mass;
                    }
                    ++probeFix;
                }
            }
            ++probeEval;
        }
    }

    // 无条件写回 —— 与 brute-force 内核相同的 Ping-Pong 完整性契约
    p.padding[0] = uintBitsToFloat(probeEval);   // DEBUG PROBE (Phase 4)
    p.padding[1] = uintBitsToFloat(probeFix);    // DEBUG PROBE (Phase 4)
    outBuf.particles[idx] = p;
}
