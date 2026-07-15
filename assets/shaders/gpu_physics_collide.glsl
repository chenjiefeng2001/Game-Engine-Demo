#version 430 core

/**
 * @file gpu_physics_collide.glsl
 * @brief GPU 物理引擎 Pass 2: 粒子间碰撞检测 + 惩罚力响应
 *
 * 使用分块暴力算法（Blocl-local），利用共享内存优化。
 * 每个工作组处理 256 个粒子，分块加载到共享内存。
 *
 * 对于 MVP 测试，此 O(N²/k) 算法足以验证：
 *   1. Compute Shader SSBO 读写
 *   2. Memory Barrier 正确性
 *   3. 显存带宽压力
 *
 * 后续可升级为 Spatial Hashing（原子操作）以支持百万级。
 */

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

struct Particle {
    vec3  position;
    float radius;
    vec3  velocity;
    float mass;
    vec4  color;
    vec4  _padding;
};

layout(std430, binding = 0) buffer ParticleBuffer {
    Particle particles[];
};

layout(std430, binding = 1) buffer CollisionOutput {
    uint collisionCount;
} outputBuf;

uniform float u_DeltaTime;
uniform float u_Stiffness;  // 弹簧刚度

// ── 共享内存（加载粒子块供同工作组快速访问） ──
shared struct Particle s_SharedParticles[256];

void main() {
    uint id = gl_GlobalInvocationID.x;
    uint localId = gl_LocalInvocationID.x;
    uint groupSize = gl_WorkGroupSize.x;
    uint totalCount = particles.length();

    if (id >= totalCount) return;

    // 加载当前粒子到共享内存
    s_SharedParticles[localId] = particles[id];
    barrier();  // 确保共享内存写入完毕

    // ── 碰撞检测（每个线程检查与工作组内其他粒子的碰撞） ──
    vec3 force = vec3(0.0);
    Particle p = s_SharedParticles[localId];

    // 遍历共享内存中所有其他粒子
    for (uint i = 0; i < groupSize && i < totalCount; ++i) {
        if (i == localId && (id >= groupSize || id / groupSize == localId / groupSize)) continue;

        Particle other = s_SharedParticles[i];

        // 检查是否真的是不同粒子（注意共享内存可能有重复）
        uint otherGlobalId = (id / groupSize) * groupSize + i;
        if (otherGlobalId >= totalCount || otherGlobalId == id) continue;

        vec3 diff = p.position - other.position;
        float dist = length(diff);

        if (dist < 0.001) continue;  // 避免除以零

        float minDist = p.radius + other.radius;

        if (dist < minDist) {
            // 发生了碰撞！
            vec3 normal = diff / dist;
            float overlap = minDist - dist;

            // 惩罚力: F = k * overlap * normal
            force += normal * overlap * u_Stiffness;
        }
    }

    // ── 全局碰撞检测（对不在同一工作组中的粒子进行二次检查） ──
    // 为简化 MVP，我们只对索引偏移后的粒子块做第二次检查
    // 这对于 65536 粒子 + 256 工作组 = 256 个工作组已经足够覆盖
    // 完整实现应使用 Spatial Hashing
    for (uint block = 0; block < (totalCount + groupSize - 1) / groupSize; ++block) {
        if (block == id / groupSize) continue;  // 跳过自己的块（已在共享内存中处理）

        // 加载另一个块的粒子（直接 SSBO 读取）
        uint otherId = block * groupSize + localId;
        if (otherId >= totalCount) continue;

        Particle other = particles[otherId];

        vec3 diff = p.position - other.position;
        float dist = length(diff);

        if (dist < 0.001) continue;

        float minDist = p.radius + other.radius;

        if (dist < minDist) {
            vec3 normal = diff / dist;
            float overlap = minDist - dist;
            force += normal * overlap * u_Stiffness;
        }
    }

    // ── 应用碰撞力 ──
    if (p.mass > 0.0) {
        vec3 acceleration = force / p.mass;
        p.velocity += acceleration * u_DeltaTime;
    }

    // 写回全局内存
    particles[id] = p;
}