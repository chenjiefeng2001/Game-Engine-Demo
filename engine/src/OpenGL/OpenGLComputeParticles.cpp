#include "Engine/OpenGL/OpenGLComputeParticles.h"
#include "Engine/Core/Log.h"
#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>
#include <sstream>
#include <cstring>

namespace Engine {

    // ... compute shader source strings remain the same ...
    static const char* s_UpdateCS = R"(
#version 460 core
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
struct Particle { vec3 position; float pad0; vec3 velocity; float pad1; vec4 color; float size; float lifetime; float age; uint alive; float pad2[2]; };
layout(std430, binding = 0) readonly buffer EmitterBuf { vec3 emitterPos; float emitRate; float gravity; float drag; float lifetime; float startSpeed; float startSize; vec4 startColor; vec3 wind; float noiseStrength; } emitter;
layout(std430, binding = 1) buffer ParticleBuf { Particle particles[]; } particleBuf;
layout(std430, binding = 2) buffer CounterBuf { uint aliveCount; uint totalEmitted; } counter;
uniform float u_DeltaTime; uniform float u_Time;
uint hash(uint x) { x = (x ^ 61) ^ (x >> 16); x = x + (x << 3); x = x ^ (x >> 4); x = x * 0x27d4eb2d; x = x ^ (x >> 15); return x; }
float rand(uint seed) { return float(hash(seed)) / 4294967295.0; }
void main() {
    uint idx = gl_GlobalInvocationID.x; if (idx >= 65536) return;
    Particle p = particleBuf.particles[idx]; if (p.alive == 0) return;
    p.age += u_DeltaTime;
    if (p.age >= p.lifetime) { p.alive = 0; particleBuf.particles[idx] = p; return; }
    p.velocity.y += emitter.gravity * u_DeltaTime;
    p.velocity *= (1.0 - emitter.drag * u_DeltaTime);
    p.velocity += emitter.wind * u_DeltaTime;
    float noise = sin(p.position.x * 12.9898 + p.position.z * 78.233 + u_Time * 2.0) * 0.5 + 0.5;
    p.velocity.x += (noise - 0.5) * emitter.noiseStrength * u_DeltaTime;
    p.velocity.z += (noise - 0.5) * emitter.noiseStrength * u_DeltaTime;
    p.position += p.velocity * u_DeltaTime;
    if (p.position.y < 0.0) { p.position.y = 0.0; p.velocity.y *= -0.3; }
    float t = p.age / p.lifetime; p.color.a = 1.0 - t;
    particleBuf.particles[idx] = p;
}
)";

    static const char* s_BillboardVS = R"(
#version 460 core
layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;
struct Particle { vec3 position; float pad0; vec3 velocity; float pad1; vec4 color; float size; float lifetime; float age; uint alive; float pad2[2]; };
layout(std430, binding = 1) buffer ParticleBuf { Particle particles[]; } particleBuf;
uniform mat4 u_ViewProj;
out vec2 v_TexCoord; out vec4 v_Color;
void main() {
    uint idx = gl_InstanceID;
    Particle p = particleBuf.particles[idx];
    if (p.alive == 0) { gl_Position = vec4(0,0,0,1); return; }
    vec3 cameraRight = vec3(u_ViewProj[0][0], u_ViewProj[1][0], u_ViewProj[2][0]);
    vec3 cameraUp = vec3(u_ViewProj[0][1], u_ViewProj[1][1], u_ViewProj[2][1]);
    vec3 worldPos = p.position + a_Position.x * p.size * cameraRight + a_Position.y * p.size * cameraUp;
    gl_Position = u_ViewProj * vec4(worldPos, 1.0);
    v_TexCoord = a_TexCoord; v_Color = p.color;
}
)";

    static const char* s_BillboardFS = R"(
#version 460 core
in vec2 v_TexCoord; in vec4 v_Color; out vec4 FragColor;
void main() {
    vec2 centered = v_TexCoord - 0.5;
    float dist = dot(centered, centered);
    if (dist > 0.25) discard;
    float alpha = smoothstep(0.25, 0.15, dist);
    FragColor = v_Color * alpha;
}
)";

    static uint32 CompileGLShader(GladGLContext& gl, uint32 type, const char* source) {
        uint32 id = gl.CreateShader(type);
        gl.ShaderSource(id, 1, &source, nullptr);
        gl.CompileShader(id);
        int success;
        gl.GetShaderiv(id, GL_COMPILE_STATUS, &success);
        if (!success) {
            char log[1024];
            gl.GetShaderInfoLog(id, 1024, nullptr, log);
            Log::Error("[ComputeParticles] Shader compile error: {}", log);
            gl.DeleteShader(id);
            return 0;
        }
        return id;
    }

    static uint32 LinkProgram(GladGLContext& gl, uint32 vs, uint32 fs) {
        uint32 prog = gl.CreateProgram();
        gl.AttachShader(prog, vs);
        gl.AttachShader(prog, fs);
        gl.LinkProgram(prog);
        int success;
        gl.GetProgramiv(prog, GL_LINK_STATUS, &success);
        if (!success) {
            char log[1024];
            gl.GetProgramInfoLog(prog, 1024, nullptr, log);
            Log::Error("[ComputeParticles] Link error: {}", log);
            gl.DeleteProgram(prog);
            return 0;
        }
        return prog;
    }

    static uint32 LinkComputeProgram(GladGLContext& gl, uint32 cs) {
        uint32 prog = gl.CreateProgram();
        gl.AttachShader(prog, cs);
        gl.LinkProgram(prog);
        int success;
        gl.GetProgramiv(prog, GL_LINK_STATUS, &success);
        if (!success) {
            char log[1024];
            gl.GetProgramInfoLog(prog, 1024, nullptr, log);
            gl.DeleteProgram(prog);
            return 0;
        }
        return prog;
    }

    OpenGLComputeParticles::OpenGLComputeParticles(GladGLContext& gl)
        : m_GL(gl) {}

    OpenGLComputeParticles::~OpenGLComputeParticles() {
        if (m_ParticleSSBO) m_GL.DeleteBuffers(1, &m_ParticleSSBO);
        if (m_CountSSBO) m_GL.DeleteBuffers(1, &m_CountSSBO);
        if (m_VAO) m_GL.DeleteVertexArrays(1, &m_VAO);
        if (m_VBO) m_GL.DeleteBuffers(1, &m_VBO);
        if (m_UpdateProgram) m_GL.DeleteProgram(m_UpdateProgram);
        if (m_RenderProgram) m_GL.DeleteProgram(m_RenderProgram);
    }

    bool OpenGLComputeParticles::Init() {
        if (!CreateShaders()) return false;
        if (!CreateBuffers()) return false;
        if (!CreateBillboardQuad()) return false;
        m_Initialized = true;
        Log::Info("[ComputeParticles] Initialized ({} particles)", m_Capacity);
        return true;
    }

    bool OpenGLComputeParticles::CreateShaders() {
        uint32 cs = CompileGLShader(m_GL, GL_COMPUTE_SHADER, s_UpdateCS);
        if (!cs) return false;
        m_UpdateProgram = LinkComputeProgram(m_GL, cs);
        m_GL.DeleteShader(cs);
        if (!m_UpdateProgram) return false;
        m_DeltaTimeLoc = m_GL.GetUniformLocation(m_UpdateProgram, "u_DeltaTime");
        m_TimeLoc = m_GL.GetUniformLocation(m_UpdateProgram, "u_Time");

        uint32 vs = CompileGLShader(m_GL, GL_VERTEX_SHADER, s_BillboardVS);
        uint32 fs = CompileGLShader(m_GL, GL_FRAGMENT_SHADER, s_BillboardFS);
        if (!vs || !fs) return false;
        m_RenderProgram = LinkProgram(m_GL, vs, fs);
        m_GL.DeleteShader(vs); m_GL.DeleteShader(fs);
        if (!m_RenderProgram) return false;
        m_ViewProjLoc = m_GL.GetUniformLocation(m_RenderProgram, "u_ViewProj");
        return true;
    }

    bool OpenGLComputeParticles::CreateBuffers() {
        struct EmitterGPU { float pos[3], r0, rate, gravity, drag, lifetime, speed, size, col[4], wind[3], noise; };
        EmitterGPU cfg = {{0,2,0}, 0, 100, -9.8f, 0.1f, 3.0f, 5.0f, 0.5f, {1,0.5f,0.2f,1}, {1,0,0}, 2.0f};
        uint32 esbo = 0;
        m_GL.GenBuffers(1, &esbo);
        m_GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, esbo);
        m_GL.BufferData(GL_SHADER_STORAGE_BUFFER, sizeof(cfg), &cfg, GL_STATIC_DRAW);
        m_GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, esbo);
        m_GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        size_t bufSz = 64 * (size_t)m_Capacity;
        m_GL.GenBuffers(1, &m_ParticleSSBO);
        m_GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, m_ParticleSSBO);
        m_GL.BufferData(GL_SHADER_STORAGE_BUFFER, bufSz, nullptr, GL_DYNAMIC_DRAW);
        m_GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_ParticleSSBO);
        m_GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        uint32 zero[2] = {0,0};
        m_GL.GenBuffers(1, &m_CountSSBO);
        m_GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, m_CountSSBO);
        m_GL.BufferData(GL_SHADER_STORAGE_BUFFER, sizeof(zero), zero, GL_DYNAMIC_DRAW);
        m_GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_CountSSBO);
        m_GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        return true;
    }

    bool OpenGLComputeParticles::CreateBillboardQuad() {
        float verts[] = { -1,-1,0,0,  1,-1,1,0,  1,1,1,1,  -1,1,0,1 };
        uint32 idx[] = {0,1,2,2,3,0};
        m_GL.GenVertexArrays(1, &m_VAO);
        m_GL.GenBuffers(1, &m_VBO);
        m_GL.BindVertexArray(m_VAO);
        m_GL.BindBuffer(GL_ARRAY_BUFFER, m_VBO);
        m_GL.BufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        m_GL.EnableVertexAttribArray(0);
        m_GL.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
        m_GL.EnableVertexAttribArray(1);
        m_GL.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8);
        m_GL.BindVertexArray(0);
        return true;
    }

    void OpenGLComputeParticles::OnUpdate(float dt) {
        if (!m_Initialized || !IsPlaying()) return;
        m_TotalTime += dt;
        VFX::EmitterConfig cfg = GetConfig();
        m_EmitAccumulator += cfg.emitRate * dt;
        if (m_EmitAccumulator >= 1.0f) {
            uint32 n = (uint32)m_EmitAccumulator;
            m_EmitAccumulator -= (float)n;
            UploadEmitData(n, glm::vec3(0,2,0));
        }
        m_GL.UseProgram(m_UpdateProgram);
        m_GL.Uniform1f(m_DeltaTimeLoc, dt);
        m_GL.Uniform1f(m_TimeLoc, m_TotalTime);
        m_GL.DispatchCompute((m_Capacity + 63) / 64, 1, 1);
        m_GL.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
        m_AliveCount = (uint32)std::min((size_t)m_Capacity, (size_t)(m_AliveCount + cfg.emitRate * dt));
    }

    void OpenGLComputeParticles::UploadEmitData(uint32 count, const glm::vec3& pos) {
        m_GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, m_ParticleSSBO);
        uint8_t* data = (uint8_t*)m_GL.MapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_WRITE);
        if (!data) return;
        uint32 emitted = 0;
        for (uint32 i = 0; i < m_Capacity && emitted < count; ++i) {
            uint32* alive = (uint32*)(data + i * 64 + 48);
            if (*alive == 0) {
                float* p = (float*)(data + i * 64);
                p[0]=pos.x; p[1]=pos.y; p[2]=pos.z;
                float* v = (float*)(data + i * 64 + 16);
                float th = ((float)(rand()%1000)/1000.0f)*6.2831853f;
                float ph = ((float)(rand()%1000)/1000.0f)*1.5707963f;
                float sp = 3.0f+((float)(rand()%1000)/1000.0f)*3.0f;
                v[0]=sin(th)*cos(ph)*sp; v[1]=sin(ph)*sp; v[2]=cos(th)*cos(ph)*sp;
                float* c = (float*)(data + i * 64 + 32);
                c[0]=1; c[1]=0.5f+(float)(rand()%1000)/2000.0f; c[2]=0.2f; c[3]=1;
                *(float*)(data+i*64+44)=0.5f; *(float*)(data+i*64+48)=3.0f; // size, lifetime
                *(float*)(data+i*64+52)=0; *alive=1; // age, alive
                emitted++;
            }
        }
        m_GL.UnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        m_GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void OpenGLComputeParticles::Burst(uint32 count) { UploadEmitData(count, glm::vec3(0,2,0)); }
    void OpenGLComputeParticles::Clear() {
        m_GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, m_ParticleSSBO);
        m_GL.BufferData(GL_SHADER_STORAGE_BUFFER, 64 * m_Capacity, nullptr, GL_DYNAMIC_DRAW);
        m_GL.BindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        m_AliveCount = 0; m_EmitAccumulator = 0.0f;
    }

    void OpenGLComputeParticles::Render(const glm::mat4& view, const glm::mat4& proj) {
        if (!m_Initialized || m_AliveCount == 0) return;
        m_GL.Enable(GL_BLEND);
        m_GL.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_GL.DepthMask(GL_FALSE);
        m_GL.UseProgram(m_RenderProgram);
        glm::mat4 vp = proj * view;
        m_GL.UniformMatrix4fv(m_ViewProjLoc, 1, GL_FALSE, glm::value_ptr(vp));
        m_GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_ParticleSSBO);
        m_GL.BindVertexArray(m_VAO);
        m_GL.DrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0, m_AliveCount);
        m_GL.BindVertexArray(0);
        m_GL.UseProgram(0);
        m_GL.DepthMask(GL_TRUE);
        m_GL.Disable(GL_BLEND);
    }

} // namespace Engine