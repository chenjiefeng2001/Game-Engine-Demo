#version 430 core

/**
 * @file gpu_physics_render.vert
 * @brief GPU 物理引擎 — 实例化渲染 Vertex Shader
 *
 * 直接读取 Compute Shader 写入的 SSBO，
 * 使用 gl_InstanceID 索引粒子数据。
 * 实现 Zero-Copy 渲染（CPU 零介入）。
 */

// ── 球体网格顶点输入 ──
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;

// ── 粒子数据（直接读取 Compute Shader 的 SSBO） ──
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

// ── Uniforms ──
uniform mat4 u_ViewProj;
uniform vec3 u_CameraPos;

// ── 输出 ──
out vec3 v_Color;
out vec3 v_Normal;
out vec3 v_FragPos;

void main() {
    // 使用 InstanceID 索引到对应的粒子
    Particle p = particles[gl_InstanceID];

    // 球体网格顶点从局部空间变换到世界空间
    vec3 worldPos = a_Position * p.radius + p.position;

    gl_Position = u_ViewProj * vec4(worldPos, 1.0);

    // 输出颜色和法线
    v_Color = p.color.rgb;
    v_Normal = normalize(mat3(u_ViewProj) * a_Normal);
    v_FragPos = worldPos;
}