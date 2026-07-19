/**
 * @file GL46ComputeShaders.inl
 * @brief GLSL 计算着色器源码 — GPU 物理引擎管线
 *
 * 布局：GPUParticleData C++ 结构体 = 64 bytes (std430)
 *
 * buffer binding 0: ParticleBuf (SSBO, readable/writable)
 * uniforms:
 *   u_Dt           — 时间步长 (float)
 *   u_Gravity      — 重力 (vec3)
 *   u_Restitution  — 弹性系数 (float)
 *   u_Damping      — 阻尼系数 (float)
 *   u_ParticleCount — 粒子总数 (uint)
 *   u_BoxMin       — 边界最小值 (vec3)
 *   u_BoxMax       — 边界最大值 (vec3)
 */

namespace Engine {
namespace RHI {

// ════════════════════════════════════════════════════════════════
// Integrate 计算着色器 — 半隐式欧拉积分 + 边界碰撞
// ════════════════════════════════════════════════════════════════
// local_size_x 必须与 GPUPhysicsConfig::workGroupSize (256) 一致
static const char* s_IntegrateCS = R"GLSL(
#version 460 core
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

struct Particle {
    vec3  position;
    float radius;
    vec3  velocity;
    float mass;
    vec4  color;
    float padding[4];
};

layout(std430, binding = 0) buffer ParticleBuf {
    Particle particles[];
} buf;

uniform float u_Dt;
uniform vec3  u_Gravity;
uniform vec3  u_BoxMin;
uniform vec3  u_BoxMax;
uniform float u_Restitution;
uniform float u_Damping;
uniform uint  u_ParticleCount;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_ParticleCount) return;

    Particle p = buf.particles[idx];

    // ── 半隐式欧拉积分 ──
    // v(t+dt) = v(t) + g * dt
    p.velocity += u_Gravity * u_Dt;

    // 阻尼: v *= (1 - damping * dt)
    p.velocity *= (1.0 - u_Damping * u_Dt);

    // x(t+dt) = x(t) + v(t+dt) * dt
    p.position += p.velocity * u_Dt;

    // ── 边界碰撞 ──
    // X
    if (p.position.x - p.radius < u_BoxMin.x) {
        p.position.x = u_BoxMin.x + p.radius;
        p.velocity.x = -p.velocity.x * u_Restitution;
    }
    if (p.position.x + p.radius > u_BoxMax.x) {
        p.position.x = u_BoxMax.x - p.radius;
        p.velocity.x = -p.velocity.x * u_Restitution;
    }
    // Y
    if (p.position.y - p.radius < u_BoxMin.y) {
        p.position.y = u_BoxMin.y + p.radius;
        p.velocity.y = -p.velocity.y * u_Restitution;
    }
    if (p.position.y + p.radius > u_BoxMax.y) {
        p.position.y = u_BoxMax.y - p.radius;
        p.velocity.y = -p.velocity.y * u_Restitution;
    }
    // Z
    if (p.position.z - p.radius < u_BoxMin.z) {
        p.position.z = u_BoxMin.z + p.radius;
        p.velocity.z = -p.velocity.z * u_Restitution;
    }
    if (p.position.z + p.radius > u_BoxMax.z) {
        p.position.z = u_BoxMax.z - p.radius;
        p.velocity.z = -p.velocity.z * u_Restitution;
    }

    // 写回
    buf.particles[idx] = p;
}
)GLSL";

// ════════════════════════════════════════════════════════════════
// Collide 计算着色器 — 球体-球体碰撞（空间哈希预留）
// ════════════════════════════════════════════════════════════════
static const char* s_CollideCS = R"GLSL(
#version 460 core
#extension GL_ARB_compute_shader : require

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

struct Particle {
    vec3  position;
    float radius;
    vec3  velocity;
    float mass;
    vec4  color;
    float padding[4];
};

layout(std430, binding = 0) buffer ParticleBuf {
    Particle particles[];
} buf;

uniform float u_Dt;
uniform float u_Restitution;
uniform uint  u_ParticleCount;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_ParticleCount) return;

    Particle p = buf.particles[idx];

    // 碰撞搜索：遍历所有可能碰撞的粒子（当前简化版）
    // Phase B 阶段将替换为空间哈希网格 26-Cell 邻接遍历
    for (uint j = 0; j < u_ParticleCount; ++j) {
        if (j == idx) continue;
        Particle other = buf.particles[j];
        vec3 diff = other.position - p.position;
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
            buf.particles[idx] = p;
        }
    }
}
)GLSL";

} // namespace RHI
} // namespace Engine