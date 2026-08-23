#version 460 core
// ════════════════════════════════════════════════════════════════
// gpu_physics_collide.comp.glsl — GPU 物理 Collide Pass（单一真理源）
//
// 本文件是运行时着色器的唯一来源：
//   构建期由 tools/embed_shaders.cmake 嵌入至
//   engine/src/OpenGL/GL46ShaderSources.g.h（GENERATED, DO NOT EDIT）
// 修改本文件 → 重新构建 → 自动生效。禁止在 C++ 中手写 GLSL。
//
// ── 绑定契约（Phase 2 Ping-Pong）────────────────────────────
// PSO 注册名     : "gpu_physics_collide"     （Nsight 标签: CS_Collide）
// SSBO binding 0 : InBuf  — std430 readonly，积分后状态（稳定快照输入）
// SSBO binding 1 : OutBuf — std430，碰撞修正结果
// Workgroup      : local_size_x = 256 = GPUPhysicsConfig::workGroupSize
//
// ── Uniform 契约（与 GPUPhysicsEngine::RecordCollidePass 对应）──
//   u_Dt            float  （当前未消费；Phase 4 空间哈希将使用）
//   u_Restitution   float  碰撞弹性系数
//   u_ParticleCount uint   粒子总数（越界守卫 + 暴力遍历上界）
//
// ── 算法状态 ────────────────────────────────────────────────
// 当前为 O(N²) 全量暴力遍历 —— Phase 0 基线实测（RTX 3070 Laptop）:
//   16K=13.3ms / 64K=110.7ms / 256K≈1.27s
// Phase 4 将替换为空间哈希网格 26-Cell 邻接遍历，
// 本实现保留作为 Ring7 内核等价性 diff 的参考路径。
//
// ── 求解语义变更（Phase 2）─────────────────────────────────
// 旧版：单缓冲原位更新（Gauss-Seidel 式，读到的邻居可能已被本趟修改）
// 新版：src→dst 分离（Jacobi 式，全部线程读同一稳定输入快照）
//       ⇒ 确定性提升：同输入必产生同输出，与线程调度顺序无关
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

layout(std430, binding = 1) buffer OutBuf {
    Particle particles[];
} outBuf;

uniform float u_Dt;
uniform float u_Restitution;
uniform uint  u_ParticleCount;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_ParticleCount) return;

    Particle p = inBuf.particles[idx];
    vec3 originPos = p.position;   ///< 真 Jacobi：距离始终基于快照位置（顺序无关）
    uint probeEval = 0u;   // DEBUG PROBE (Phase 4)
    uint probeFix  = 0u;   // DEBUG PROBE (Phase 4)

    // 碰撞搜索：遍历所有粒子（暴力参考实现，读稳定快照）
    // Phase 4 将替换为空间哈希网格 26-Cell 邻接遍历
    for (uint j = 0; j < u_ParticleCount; ++j) {
        if (j == idx) continue;
        Particle other = inBuf.particles[j];
        vec3 diff = other.position - originPos;
        float dist = length(diff);
        float minDist = p.radius + other.radius;

        if (dist < minDist && dist > 0.0001) {
            vec3 normal = diff / dist;
            float overlap = minDist - dist;
            float totalMass = p.mass + other.mass;
            if (totalMass < 0.0001) continue;
            float pWeight = other.mass / totalMass;
            p.position -= normal * overlap * pWeight;

            vec3 relVel = p.velocity - other.velocity;
            float velAlongNormal = dot(relVel, normal);
            if (velAlongNormal < 0.0) {
                vec3 impulse = normal * velAlongNormal * (1.0 + u_Restitution) / totalMass;
                p.velocity -= impulse * other.mass;
            }
            ++probeFix;
        }
        ++probeEval;
    }

    // 无条件写回 —— 未碰撞粒子也必须落到目标槽位（Ping-Pong 完整性）
    p.padding[0] = uintBitsToFloat(probeEval);   // DEBUG PROBE (Phase 4)
    p.padding[1] = uintBitsToFloat(probeFix);    // DEBUG PROBE (Phase 4)
    outBuf.particles[idx] = p;
}
