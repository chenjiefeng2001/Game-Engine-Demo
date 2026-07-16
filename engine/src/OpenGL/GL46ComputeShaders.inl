/**
 * @file GL46ComputeShaders.inl
 * @brief GLSL 计算着色器源码 — GPU 物理引擎管线
 *
 * 布局：Particle 使用 std430 布局，与 GPUParticleData C++ 结构体一致
 * C++ 结构体：GPUParticleData = 64 bytes (包含 float padding[4])
 *
 * buffer binding 0: ParticleBuf (SSBO, readable/writable)
 * uniform:
 *   u_Dt         — 时间步长
 *   u_Gravity    — 重力 vec3
 *   u_Restitution — 弹性系数
 *   u_Damping    — 阻尼系数
 */

namespace Engine {
namespace RHI {

// ════════════════════════════════════════════════════════════════
// Integrate 计算着色器 — 重力 + 速度 + 位置 + 边界碰撞
// ════════════════════════════════════════════════════════════════
// 诊断着色器 — 只将 particle[0].velocity.y 设为 123.45 验证 SSBO 绑定
static const char* s_IntegrateCS = R"GLSL(
#version 460 core
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
struct Particle {
    vec3  position;
    float radius;
    vec3  velocity;
    float mass;
    vec4  color;
    float padding[4];
};
layout(std430, binding = 0) buffer ParticleBuf { Particle particles[]; } buf;
uniform uint u_ParticleCount;
void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_ParticleCount) return;
    Particle p = buf.particles[idx];
    p.velocity.y = 123.45; // 诊断：强制修改第一个粒子的速度
    p.velocity.x = float(idx);
    buf.particles[idx] = p;
}
)GLSL";

// ════════════════════════════════════════════════════════════════
// Collide 计算着色器 — 简单球体-球体碰撞
// ════════════════════════════════════════════════════════════════
static const char* s_CollideCS = R"GLSL(
#version 460 core
#extension GL_ARB_compute_shader : require

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

struct Particle {
    vec3  position;   // offset 0, 12 bytes
    float radius;     // offset 12, 4 bytes
    vec3  velocity;   // offset 16, 12 bytes
    float mass;       // offset 28, 4 bytes
    vec4  color;      // offset 32, 16 bytes
    float padding[4]; // offset 48, 16 bytes → total 64 bytes
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

    for (uint j = idx + 1; j < u_ParticleCount && j < idx + 4; ++j) {
        Particle other = buf.particles[j];
        vec3 diff = other.position - p.position;
        float dist = length(diff);
        float minDist = p.radius + other.radius;

        if (dist < minDist && dist > 0.0001) {
            vec3 normal = diff / dist;
            float overlap = minDist - dist;
            float totalMass = p.mass + other.mass;
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