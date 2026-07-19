#version 430 core

/**
 * @file gpu_physics_integrate.glsl
 * @brief GPU 物理引擎 Pass 1: 半隐式欧拉积分 + 边界碰撞
 *
 * 每个工作组处理 256 个粒子。
 * 使用 SSBO 直接读写，无 CPU 交互。
 */

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

// ── 粒子结构（必须与 GPUParticleData 完全一致） ──
struct Particle {
    vec3  position;
    float radius;
    vec3  velocity;
    float mass;
    vec4  color;
    vec4  _padding;  // 填充至 80 bytes
};

// ── SSBO（读写） ──
layout(std430, binding = 0) buffer ParticleBuffer {
    Particle particles[];
};

// ── Uniform 参数 ──
// 注意：uniform 名称必须与引擎 GPUPhysicsEngine::Update() 中的命名一致
// 引擎使用 u_Dt（与 GL46ComputeShaders.inl 中的内联着色器匹配）
uniform float u_Dt;
uniform vec3  u_Gravity;
uniform vec3  u_BoxMin;
uniform vec3  u_BoxMax;
uniform float u_Restitution;
uniform float u_Damping;

void main() {
    uint id = gl_GlobalInvocationID.x;
    if (id >= particles.length()) return;

    Particle p = particles[id];

    // ── 半隐式欧拉积分 ──
    // v(t+Δt) = v(t) + a * Δt
    // x(t+Δt) = x(t) + v(t+Δt) * Δt
    p.velocity += u_Gravity * u_Dt;
    
    // 速度阻尼（空气阻力）
    p.velocity *= (1.0 - u_Damping * u_Dt);
    
    p.position += p.velocity * u_Dt;

    // ── 边界碰撞处理（带能量损耗） ──
    // X 轴边界
    if (p.position.x - p.radius < u_BoxMin.x) {
        p.position.x = u_BoxMin.x + p.radius;
        p.velocity.x = -p.velocity.x * u_Restitution;
    }
    if (p.position.x + p.radius > u_BoxMax.x) {
        p.position.x = u_BoxMax.x - p.radius;
        p.velocity.x = -p.velocity.x * u_Restitution;
    }

    // Y 轴边界（地面 & 天花板）
    if (p.position.y - p.radius < u_BoxMin.y) {
        p.position.y = u_BoxMin.y + p.radius;
        p.velocity.y = -p.velocity.y * u_Restitution;
    }
    if (p.position.y + p.radius > u_BoxMax.y) {
        p.position.y = u_BoxMax.y - p.radius;
        p.velocity.y = -p.velocity.y * u_Restitution;
    }

    // Z 轴边界
    if (p.position.z - p.radius < u_BoxMin.z) {
        p.position.z = u_BoxMin.z + p.radius;
        p.velocity.z = -p.velocity.z * u_Restitution;
    }
    if (p.position.z + p.radius > u_BoxMax.z) {
        p.position.z = u_BoxMax.z - p.radius;
        p.velocity.z = -p.velocity.z * u_Restitution;
    }

    // 写回
    particles[id] = p;
}