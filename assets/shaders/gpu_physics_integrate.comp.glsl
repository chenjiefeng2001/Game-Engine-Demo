#version 460 core
// ════════════════════════════════════════════════════════════════
// gpu_physics_integrate.comp.glsl — GPU 物理 Integrate Pass（单一真理源）
//
// 本文件是运行时着色器的唯一来源：
//   构建期由 tools/embed_shaders.cmake 嵌入至
//   engine/src/OpenGL/GL46ShaderSources.g.h（GENERATED, DO NOT EDIT）
// 修改本文件 → 重新构建 → 自动生效。禁止在 C++ 中手写 GLSL。
//
// ── 绑定契约（Phase 2 Ping-Pong）────────────────────────────
// PSO 注册名     : "gpu_physics_integrate"   （Nsight 标签: CS_Integrate）
// SSBO binding 0 : InBuf  — std430 readonly，上一步状态（源）
// SSBO binding 1 : OutBuf — std430，本步结果（目标）
//   数据布局与 C++ GPUParticleData 对齐：
//   engine/include/Engine/Core/Physics/GPUParticle.h（64B/粒子，static_assert）
// Workgroup      : local_size_x = 256 = GPUPhysicsConfig::workGroupSize
//
// ── Uniform 契约（与 GPUPhysicsEngine::RecordIntegratePass 对应）──
//   u_Dt            float  时间步长
//   u_Gravity       vec3   重力加速度
//   u_Restitution   float  边界弹性系数
//   u_Damping       float  线性阻尼系数
//   u_BoxMin        vec3   仿真包围盒最小点
//   u_BoxMax        vec3   仿真包围盒最大点
//   u_ParticleCount uint   粒子总数（越界守卫）
//
// ── 语义 ────────────────────────────────────────────────────
// State(cur) → 半隐式欧拉积分 + 边界反弹 → State(next)
// 纯函数：读 src 写 dst，无跨线程依赖，无同 buffer 读 写竞争。
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

    Particle p = inBuf.particles[idx];

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

    // 无条件写回（Ping-Pong 目标必须每槽位完整）
    outBuf.particles[idx] = p;
}
