/**
 * @file GPUParticleSystem.cpp
 * @brief GPU 粒子系统实现 — Compute Shader + 深度图碰撞
 *
 * 管线流程（每帧）:
 *   1. CPU: Spawn — 生成新粒子，写入 SSBO
 *   2. GPU: Update — Compute Shader 执行物理积分 + 深度图碰撞
 *   3. GPU: Render — 实例化渲染
 */

#include "Engine/Rendering/GPUParticleSystem.h"
#include "Engine/Core/Log.h"
#include <cstring>
#include <cstdio>

#ifdef ENGINE_HAS_OPENGL
#include <glad/gl.h>
#endif

namespace Engine {

// ── 粒子结构（CPU/GPU 布局一致） ──
// 使用 std140 布局，确保 CPU/GPU 内存对齐
#pragma pack(push, 4)
struct GPUParticle {
    float position[4];  // xyz + pad
    float velocity[4];  // xyz + pad
    float color[4];     // rgba
    float size;         // 粒子大小
    float lifetime;     // 最大生命周期
    float age;          // 当前年龄
    uint32_t alive;     // 1=活跃, 0=死亡
    float pad[2];       // std140 16B 对齐
};
#pragma pack(pop)

static_assert(sizeof(GPUParticle) == 64, "GPUParticle must be 64 bytes");

// ── Compute Shader 源码（带深度图碰撞） ──
static const char* s_ComputeShaderSrc = R"(
#version 460 core
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// 粒子 Buffer
struct Particle { vec4 position; vec4 velocity; vec4 color; float size; float lifetime; float age; uint alive; float pad0; float pad1; };
layout(std430, binding = 0) buffer ParticleBuf { Particle particles[]; } buf;

// 配置 Uniform
uniform float u_DeltaTime;
uniform float u_Gravity;
uniform float u_Bounce;
uniform float u_Time;

// 场景深度纹理（用于碰撞检测）
layout(binding = 1) uniform sampler2D u_DepthTexture;
uniform mat4 u_ViewProj;
uniform vec2 u_ScreenSize;

void main() {
    uint id = gl_GlobalInvocationID.x;
    if (id >= 65536) return;

    Particle p = buf.particles[id];
    if (p.alive == 0u) return;

    // 衰老
    p.age += u_DeltaTime;
    if (p.age >= p.lifetime) {
        p.alive = 0u;
        buf.particles[id] = p;
        return;
    }

    // 物理积分
    p.velocity.y += u_Gravity * u_DeltaTime;
    p.position.xyz += p.velocity.xyz * u_DeltaTime;

    // 屏幕空间深度碰撞
    vec4 clipPos = u_ViewProj * vec4(p.position.xyz, 1.0);
    vec3 ndc = clipPos.xyz / clipPos.w;
    vec2 screenUV = ndc.xy * 0.5 + 0.5;

    // 防止超出屏幕边缘的粒子被错误碰撞
    if (all(greaterThanEqual(screenUV, vec2(0.0))) && all(lessThanEqual(screenUV, vec2(1.0)))) {
        float sceneDepth = texture(u_DepthTexture, screenUV).r;
        float particleDepth = ndc.z;

        // 如果粒子深度大于场景深度（粒子在场景物体后面），发生碰撞
        if (particleDepth > sceneDepth + 0.001) {
            // 从深度重建世界坐标
            vec4 worldPos = inverse(u_ViewProj) * vec4(ndc.xy, sceneDepth * 2.0 - 1.0, 1.0);
            worldPos.xyz /= worldPos.w;
            
            // 反弹 + 能量损失
            p.velocity.y = abs(p.velocity.y) * u_Bounce;
            
            // 将粒子挤出地面
            p.position.xyz = mix(p.position.xyz, worldPos.xyz, 0.5);
            p.position.y += 0.05;
        }
    }

    buf.particles[id] = p;
}
)";

// ── 渲染着色器源码 ──
static const char* s_RenderVertSrc = R"(
#version 460 core
layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;

struct Particle { vec4 position; vec4 velocity; vec4 color; float size; float lifetime; float age; uint alive; float pad0; float pad1; };
layout(std430, binding = 1) buffer ParticleBuf { Particle particles[]; } buf;

uniform mat4 u_ViewProj;

out vec4 v_Color;
out vec2 v_TexCoord;

void main() {
    Particle p = buf.particles[gl_InstanceID];
    if (p.alive == 0u) { gl_Position = vec4(0,0,-1000,1); return; }

    vec3 camRight = vec3(u_ViewProj[0][0], u_ViewProj[1][0], u_ViewProj[2][0]);
    vec3 camUp = vec3(u_ViewProj[0][1], u_ViewProj[1][1], u_ViewProj[2][1]);

    vec3 billboardPos = p.position.xyz + (a_Position.x * camRight + a_Position.y * camUp) * p.size;
    gl_Position = u_ViewProj * vec4(billboardPos, 1.0);

    v_Color = p.color;
    v_TexCoord = a_TexCoord;
}
)";

static const char* s_RenderFragSrc = R"(
#version 460 core
in vec4 v_Color;
in vec2 v_TexCoord;
out vec4 FragColor;
void main() {
    float dist = length(v_TexCoord - vec2(0.5));
    if (dist > 0.5) discard;
    float alpha = smoothstep(0.5, 0.0, dist) * v_Color.a;
    FragColor = vec4(v_Color.rgb, alpha);
}
)";

// ═══════════════════════════════════════════════════════════
// GPUParticleSystem 实现
// ═══════════════════════════════════════════════════════════

GPUParticleSystem::GPUParticleSystem() = default;

GPUParticleSystem::~GPUParticleSystem() { Shutdown(); }

bool GPUParticleSystem::Initialize(const GPUParticleConfig& config) {
    if (m_Initialized) Shutdown();
    m_Config = config;

#ifdef ENGINE_HAS_OPENGL
    // 创建 SSBO
    glGenBuffers(1, &m_SSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_SSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GPUParticle) * config.maxParticles, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // 初始化所有粒子为死亡状态
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_SSBO);
    GPUParticle* data = (GPUParticle*)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY);
    if (data) {
        memset(data, 0, sizeof(GPUParticle) * config.maxParticles);
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }

    // 编译 Compute Shader
    auto CompileShader = [](GLuint type, const char* src) -> GLuint {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint success = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char log[1024];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            Log::Error("[GPU Particle] Shader compile error: {}", log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    };

    // Compute program
    GLuint cs = CompileShader(GL_COMPUTE_SHADER, s_ComputeShaderSrc);
    if (!cs) return false;
    m_ComputeProgram = glCreateProgram();
    glAttachShader(m_ComputeProgram, cs);
    glLinkProgram(m_ComputeProgram);
    glDeleteShader(cs);

    // Render program
    GLuint vs = CompileShader(GL_VERTEX_SHADER, s_RenderVertSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, s_RenderFragSrc);
    if (!vs || !fs) return false;
    m_RenderProgram = glCreateProgram();
    glAttachShader(m_RenderProgram, vs);
    glAttachShader(m_RenderProgram, fs);
    glLinkProgram(m_RenderProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    // 粒子渲染用 VAO（全屏四边形）
    float quadVertices[] = {
        -0.5f, -0.5f, 0.0f, 0.0f,
         0.5f, -0.5f, 1.0f, 0.0f,
         0.5f,  0.5f, 1.0f, 1.0f,
        -0.5f,  0.5f, 0.0f, 1.0f,
    };
    unsigned int quadIndices[] = { 0, 1, 2, 0, 2, 3 };

    glGenVertexArrays(1, &m_VAO);
    glBindVertexArray(m_VAO);

    GLuint vbo, ibo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices, GL_STATIC_DRAW);

    glBindVertexArray(0);

    Log::Info("[GPU Particle] Initialized with {} particles", config.maxParticles);
#endif

    m_Initialized = true;
    return true;
}

void GPUParticleSystem::Render(const Mat4& viewProj) {
#ifdef ENGINE_HAS_OPENGL
    if (!m_Initialized || m_ActiveCount == 0 || !m_RenderProgram) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glUseProgram(m_RenderProgram);
    glUniformMatrix4fv(glGetUniformLocation(m_RenderProgram, "u_ViewProj"), 1, GL_FALSE, viewProj.Data());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_SSBO);
    glBindVertexArray(m_VAO);
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0, m_ActiveCount);
    glBindVertexArray(0);

    glUseProgram(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
#endif
}

void GPUParticleSystem::Shutdown() {
#ifdef ENGINE_HAS_OPENGL
    if (m_SSBO) glDeleteBuffers(1, &m_SSBO);
    if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
    if (m_ComputeProgram) glDeleteProgram(m_ComputeProgram);
    if (m_RenderProgram) glDeleteProgram(m_RenderProgram);
    m_SSBO = 0; m_VAO = 0; m_ComputeProgram = 0; m_RenderProgram = 0;
#endif
    m_Initialized = false;
    m_ActiveCount = 0;
}

void GPUParticleSystem::Update(float32 dt) {
    m_ElapsedTime += dt;

    // CPU：生成新粒子
    m_SpawnAccumulator += m_Config.spawnRate * dt;
    while (m_SpawnAccumulator >= 1.0f) {
        Spawn(static_cast<uint32_t>(m_SpawnAccumulator));
        m_SpawnAccumulator -= 1.0f;
    }

#ifdef ENGINE_HAS_OPENGL
    if (!m_Initialized || !m_ComputeProgram) return;

    // GPU：Compute Shader 更新
    glUseProgram(m_ComputeProgram);
    glUniform1f(glGetUniformLocation(m_ComputeProgram, "u_DeltaTime"), dt);
    glUniform1f(glGetUniformLocation(m_ComputeProgram, "u_Gravity"), m_Config.gravity.y);
    glUniform1f(glGetUniformLocation(m_ComputeProgram, "u_Bounce"), m_Config.bounce);
    glUniform1f(glGetUniformLocation(m_ComputeProgram, "u_Time"), m_ElapsedTime);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_SSBO);
    glDispatchCompute((m_Config.maxParticles + 63) / 64, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    glUseProgram(0);
#endif
}

void GPUParticleSystem::Spawn(uint32_t count) {
#ifdef ENGINE_HAS_OPENGL
    if (!m_Initialized || !m_SSBO) return;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_SSBO);
    GPUParticle* data = (GPUParticle*)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_WRITE);
    if (!data) return;

    uint32_t emitted = 0;
    for (uint32_t i = 0; i < m_Config.maxParticles && emitted < count; ++i) {
        if (data[i].alive == 0) {
            // 在 spawnArea 范围内随机生成
            float theta = ((float)(rand() % 1000) / 1000.0f) * 6.2831853f;
            float phi   = ((float)(rand() % 1000) / 1000.0f) * 1.5707963f;
            float speed = 3.0f + ((float)(rand() % 1000) / 1000.0f) * 3.0f;

            data[i].position[0] = ((float)(rand() % 1000) / 1000.0f - 0.5f) * m_Config.spawnArea.x;
            data[i].position[1] = ((float)(rand() % 1000) / 1000.0f) * m_Config.spawnArea.y;
            data[i].position[2] = ((float)(rand() % 1000) / 1000.0f - 0.5f) * m_Config.spawnArea.z;

            data[i].velocity[0] = sin(theta) * cos(phi) * speed + m_Config.initialVelocity.x;
            data[i].velocity[1] = sin(phi) * speed + m_Config.initialVelocity.y;
            data[i].velocity[2] = cos(theta) * cos(phi) * speed + m_Config.initialVelocity.z;

            data[i].color[0] = 1.0f;
            data[i].color[1] = 0.5f + (float)(rand() % 1000) / 2000.0f;
            data[i].color[2] = 0.2f;
            data[i].color[3] = 1.0f;

            data[i].size = m_Config.particleSize;
            data[i].lifetime = m_Config.lifetime;
            data[i].age = 0.0f;
            data[i].alive = 1;

            emitted++;
        }
    }

    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    m_ActiveCount += emitted;
#endif
}

} // namespace Engine