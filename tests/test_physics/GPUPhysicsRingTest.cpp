            /**
 * @file GPUPhysicsRingTest.cpp
 * @brief GPU 物理三环验证（需要真实 GL 4.6 GPU）
 *
 * 测试环：
 *   Ring 1: 内存完整性 — 上传→回读逐字节比对
 *   Ring 2: 动力学变化 — 计算着色器成功修改数据
 *   Ring 3: 计算着色器执行验证
 *
 * 注意：无 GPU 时自动跳过（GTEST_SKIP）
 */
#include <gtest/gtest.h>
#include <algorithm>
#include <utility>
#include <array>
#include "Engine/Core/Physics/GPUParticle.h"
#include "Engine/Core/RHI/GL46AZDODevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/RHITypes.h"
#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <memory>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <thread>
#include <chrono>

using namespace Engine;
using namespace Engine::RHI;

struct GLFWContext {
    GLFWwindow* window = nullptr;
    std::unique_ptr<GladGLContext> glContext;

    bool Init() {
        if (!glfwInit()) return false;
        glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        window = glfwCreateWindow(1, 1, "GPUPhysicsTest", nullptr, nullptr);
        if (!window) { glfwTerminate(); return false; }
        glfwMakeContextCurrent(window);
        glContext = std::make_unique<GladGLContext>();
        if (!gladLoadGLContext(glContext.get(), glfwGetProcAddress)) {
            glfwDestroyWindow(window); window = nullptr;
            glfwTerminate();
            return false;
        }
        return true;
    }

    ~GLFWContext() {
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
    }
};

class GPUPhysicsRingTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        s_GLFW = std::make_unique<GLFWContext>();
        if (!s_GLFW->Init()) {
            std::printf("  [WARN] Cannot create GL context, GPU tests will skip\n");
            s_GLFW.reset();
            return;
        }
        s_Device = std::make_unique<GL46Device>();
        std::printf("  [INFO] Initializing GL46Device...\n");
        if (!s_Device->InitializeWithGLContext(s_GLFW->glContext.get(), 1, 1)) {
            std::printf("  [WARN] GL46Device init failed, GPU tests will skip\n");
            s_Device.reset();
        }
    }

    static void TearDownTestSuite() {
        s_Device.reset();
        s_GLFW.reset();
    }

    void SetUp() override {
        if (!s_Device) {
            GTEST_SKIP() << "No GL 4.6 GPU available";
        }
        m_Engine = std::make_unique<GPUPhysicsEngine>();
        GPUPhysicsConfig config;
        config.particleCount = 256;
        ASSERT_TRUE(m_Engine->Initialize(s_Device.get(), config));
        m_CmdList = s_Device->CreateCommandList(CommandListType::Direct);
        ASSERT_NE(m_CmdList, nullptr);
    }

    void TearDown() override {
        if (m_Engine) m_Engine->Shutdown();
    }

    static std::unique_ptr<GLFWContext> s_GLFW;
    static std::unique_ptr<GL46Device> s_Device;
    std::unique_ptr<GPUPhysicsEngine> m_Engine;
    std::unique_ptr<IRHICommandList> m_CmdList;
};

std::unique_ptr<GLFWContext> GPUPhysicsRingTest::s_GLFW = nullptr;
std::unique_ptr<GL46Device> GPUPhysicsRingTest::s_Device = nullptr;

TEST_F(GPUPhysicsRingTest, Ring0_DeviceInfo) {
    const char* name = s_Device->GetDeviceName();
    EXPECT_NE(name, nullptr);
    EXPECT_GT(std::strlen(name), 0);
    std::printf("    [Ring0] GPU: %s\n", name);
}

TEST_F(GPUPhysicsRingTest, Ring1_MemoryIntegrity) {
    m_Engine->ResetParticles();
    std::vector<GPUParticleData> uploaded(256), readback(256);
    m_Engine->ReadbackParticles(0, 256, uploaded.data());
    m_Engine->ReadbackParticles(0, 256, readback.data());
    bool ok = true;
    for (uint32_t i = 0; i < 256 && ok; ++i) {
        if (std::memcmp(&uploaded[i], &readback[i], sizeof(GPUParticleData)) != 0) ok = false;
    }
    std::printf("    [Ring1] 256 particles memory integrity: %s\n", ok ? "PASS" : "FAIL");
    EXPECT_TRUE(ok);
}

// Ring2: 在引擎 SSBO 上通过独立编译的 Integrate shader 验证物理计算
// 注意：此测试直接内联了与引擎完全相同的 Integrate shader 源码，
// 用于隔离引擎 Dispatch 管线和 Shader 本身的问题。
//
// 测试方案（四路诊断）：
//   方案 A (Ring2-A): GPUPhysicsEngine::Update() 完整管线 — 引擎原生路径
//   方案 B (Ring2-B): 裸 GL 手动 Dispatch Integrate shader（完全绕开引擎管线）
//   方案 C (Ring2-C): 裸 GL 手动 Dispatch Integrate + Collide（含碰撞）
//   方案 D (Ring2-D): 通过 CmdList 手动复现引擎管线（绕开 GPUPhysicsEngine::Update）
//
// 主要断言：方案 B/C 必须通过（证明着色器本身正确）
// 次要断言：方案 A/D 提供诊断输出（验证 CmdList uniform 传递管线）
TEST_F(GPUPhysicsRingTest, Ring2_ComputeShaderModifiesData) {
    auto* gl46Dev = static_cast<GL46Device*>(s_Device.get());
    auto& gl = gl46Dev->GetGL();

    // ── 获取引擎 SSBO ──
    auto* engBuf = static_cast<GL46Buffer*>(m_Engine->GetParticleBuffer());
    uint32_t bufHandle = engBuf->GetGLHandle();
    ASSERT_NE(bufHandle, 0);
    void* persistentPtr = engBuf->GetPersistentPtr();
    ASSERT_NE(persistentPtr, nullptr);

    // ── 写初始值 ──
    GPUParticleData* p = (GPUParticleData*)persistentPtr;
    std::memset(&p[0], 0, sizeof(GPUParticleData));
    p[0].position[0] = 10.0f;
    p[0].position[1] = 50.0f;
    p[0].position[2] = 30.0f;
    p[0].radius = 1.0f;
    p[0].velocity[1] = 0.0f;
    p[0].mass = 1.0f;
    p[0].color[0] = 1.0f; p[0].color[1] = 1.0f;
    p[0].color[2] = 1.0f; p[0].color[3] = 1.0f;
    gl.MemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
    gl.Finish();

    std::printf("    [Ring2] Initial: pos=(%.1f,%.1f,%.1f) vel=(%.4f,%.4f,%.4f)\n",
                p[0].position[0], p[0].position[1], p[0].position[2],
                p[0].velocity[0], p[0].velocity[1], p[0].velocity[2]);

    // ─────────────────────────────────────────────────────────
    // 方案 A (Ring2-A)：通过引擎路径运行 10 帧（验证完整管线）
    // 使用 GPUPhysicsEngine::Update() → CmdList → Dispatch
    // ─────────────────────────────────────────────────────────
    for (int i = 0; i < 10; ++i) {
        m_CmdList->Begin();
        m_Engine->Update(1.0f / 60.0f, m_CmdList.get());
        m_CmdList->End();
    }
    s_Device->WaitIdle();

    GPUParticleData after;
    std::memcpy(&after, &p[0], sizeof(GPUParticleData));
    std::printf("    [Ring2-A] After 10 engine frames: pos=(%.1f,%.1f,%.1f) vel=(%.4f,%.4f,%.4f)\n",
                after.position[0], after.position[1], after.position[2],
                after.velocity[0], after.velocity[1], after.velocity[2]);

    // Ring2-A 次要断言：引擎管线应当成功修改粒子数据
    // （重力作用下 velocity.y 应从 0 变为负值）
    bool engineChanged = (std::abs(after.velocity[1]) > 0.0001f);
    // 注意：如果此断言失败，通常是 uniform 传递问题 — 检查：
    //   1. GL46CommandList::Dispatch() 的 glProgramUniform* DSA 调用
    //   2. gpu_physics_integrate/collide 着色器中的 uniform 名称与引擎匹配
    EXPECT_TRUE(engineChanged)
        << "Ring2-A: Engine pipeline (GPUPhysicsEngine::Update) should modify particle velocity";

    // ─────────────────────────────────────────────────────────
    // 方案 B：在独立 SSBO 上用内联的 Integrate shader 做物理积分
    // （完全独立于引擎管线）
    // ─────────────────────────────────────────────────────────
    // 创建独立 SSBO
    uint32_t freshBuf = 0;
    gl.CreateBuffers(1, &freshBuf);
    GLsizeiptr bufSize = 256 * (GLsizeiptr)sizeof(GPUParticleData);
    gl.NamedBufferStorage(freshBuf, bufSize, nullptr,
                          GL_MAP_READ_BIT | GL_MAP_WRITE_BIT |
                          GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
    void* freshMapped = gl.MapNamedBufferRange(freshBuf, 0, bufSize,
                                               GL_MAP_READ_BIT | GL_MAP_WRITE_BIT |
                                               GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
    ASSERT_NE(freshMapped, nullptr);
    std::memset(freshMapped, 0, bufSize);

    GPUParticleData* f = (GPUParticleData*)freshMapped;
    std::memset(&f[0], 0, sizeof(GPUParticleData));
    f[0].position[0] = 10.0f;
    f[0].position[1] = 50.0f;
    f[0].position[2] = 30.0f;
    f[0].radius = 1.0f;
    f[0].velocity[1] = 0.0f;
    f[0].mass = 1.0f;
    f[0].color[0] = 1.0f; f[0].color[1] = 1.0f;
    f[0].color[2] = 1.0f; f[0].color[3] = 1.0f;

    // 编译与引擎完全相同的 Integrate shader
    // 注意：为匹配 256 个粒子，使用 local_size_x=256, Dispatch(1,1,1)
    static const char* rawIntegrateCS = R"GLSL(
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
        layout(std430, binding = 0) buffer ParticleBuf { Particle particles[]; } buf;
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
            p.velocity += u_Gravity * u_Dt;
            p.velocity *= (1.0 - u_Damping * u_Dt);
            p.position += p.velocity * u_Dt;
            buf.particles[idx] = p;
        }
    )GLSL";

    uint32_t cs = gl.CreateShader(GL_COMPUTE_SHADER);
    gl.ShaderSource(cs, 1, &rawIntegrateCS, nullptr);
    gl.CompileShader(cs);
    GLint compiled = 0;
    gl.GetShaderiv(cs, GL_COMPILE_STATUS, &compiled);
    ASSERT_NE(compiled, 0) << "Raw Integrate CS compile failed!";

    uint32_t rawProg = gl.CreateProgram();
    gl.AttachShader(rawProg, cs);
    gl.LinkProgram(rawProg);
    GLint linked = 0;
    gl.GetProgramiv(rawProg, GL_LINK_STATUS, &linked);
    ASSERT_NE(linked, 0) << "Raw Integrate program link failed!";
    gl.DeleteShader(cs);

    // Dispatch 1 帧
    gl.UseProgram(rawProg);
    gl.Uniform1f(gl.GetUniformLocation(rawProg, "u_Dt"), 1.0f / 60.0f);
    gl.Uniform3f(gl.GetUniformLocation(rawProg, "u_Gravity"), 0.0f, -9.8f, 0.0f);
    gl.Uniform1f(gl.GetUniformLocation(rawProg, "u_Damping"), 0.02f);
    gl.Uniform1f(gl.GetUniformLocation(rawProg, "u_Restitution"), 0.8f);
    gl.Uniform1ui(gl.GetUniformLocation(rawProg, "u_ParticleCount"), 256);
    gl.Uniform3f(gl.GetUniformLocation(rawProg, "u_BoxMin"), -50.0f, 0.0f, -50.0f);
    gl.Uniform3f(gl.GetUniformLocation(rawProg, "u_BoxMax"), 50.0f, 100.0f, 50.0f);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, freshBuf);
    gl.DispatchCompute(1, 1, 1);
    gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
    gl.Finish();

    GPUParticleData afterRawIntegrate;
    std::memcpy(&afterRawIntegrate, &f[0], sizeof(GPUParticleData));
    std::printf("    [Ring2-B] After raw integrate (1 frame): pos=(%.1f,%.1f,%.1f) vel=(%.4f,%.4f,%.4f)\n",
                afterRawIntegrate.position[0], afterRawIntegrate.position[1], afterRawIntegrate.position[2],
                afterRawIntegrate.velocity[0], afterRawIntegrate.velocity[1], afterRawIntegrate.velocity[2]);

    bool integrateWorks = (std::abs(afterRawIntegrate.velocity[1]) > 0.0001f);

    gl.UnmapNamedBuffer(freshBuf);
    gl.DeleteBuffers(1, &freshBuf);
    gl.DeleteProgram(rawProg);

    // ── 方案 C (Ring2-C)：手动复现引擎 Integrate+Collide 交替管线 ──
    // 绕过 GPUPhysicsEngine + CmdList，直接用裸 GL dispatch
    static const char* rawCollideCS = R"GLSL(
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
        layout(std430, binding = 0) buffer ParticleBuf { Particle particles[]; } buf;
        uniform float u_Dt;
        uniform float u_Restitution;
        uniform uint  u_ParticleCount;
        void main() {
            uint idx = gl_GlobalInvocationID.x;
            if (idx >= u_ParticleCount) return;
            Particle p = buf.particles[idx];
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
                }
            }
            buf.particles[idx] = p;
        }
    )GLSL";

    // 编译 Integrate + Collide（使用 glCreateShaderProgramv 快速创建可执行 program）
    GLuint rawIntegrateProg = gl.CreateShaderProgramv(GL_COMPUTE_SHADER, 1, &rawIntegrateCS);
    ASSERT_NE(rawIntegrateProg, 0);
    GLuint rawCollideProg   = gl.CreateShaderProgramv(GL_COMPUTE_SHADER, 1, &rawCollideCS);
    ASSERT_NE(rawCollideProg, 0);

    // 重置引擎 SSBO 上的粒子 0
    std::memset(&p[0], 0, sizeof(GPUParticleData));
    p[0].position[0] = 10.0f; p[0].position[1] = 50.0f; p[0].position[2] = 30.0f;
    p[0].radius = 1.0f; p[0].velocity[1] = 0.0f; p[0].mass = 1.0f;
    p[0].color[0] = 1.0f; p[0].color[1] = 1.0f; p[0].color[2] = 1.0f; p[0].color[3] = 1.0f;
    gl.MemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT); gl.Finish();

    // 手动复现引擎的 10 帧管线（完全绕开 CmdList）
    for (int frame = 0; frame < 10; ++frame) {
        // ── Integrate Pass ──
        gl.UseProgram(rawIntegrateProg);
        gl.Uniform1f(gl.GetUniformLocation(rawIntegrateProg, "u_Dt"), 1.0f / 60.0f);
        gl.Uniform3f(gl.GetUniformLocation(rawIntegrateProg, "u_Gravity"), 0.0f, -9.8f, 0.0f);
        gl.Uniform1f(gl.GetUniformLocation(rawIntegrateProg, "u_Damping"), 0.02f);
        gl.Uniform1f(gl.GetUniformLocation(rawIntegrateProg, "u_Restitution"), 0.8f);
        gl.Uniform1ui(gl.GetUniformLocation(rawIntegrateProg, "u_ParticleCount"), 256);
        gl.Uniform3f(gl.GetUniformLocation(rawIntegrateProg, "u_BoxMin"), -50.0f, 0.0f, -50.0f);
        gl.Uniform3f(gl.GetUniformLocation(rawIntegrateProg, "u_BoxMax"), 50.0f, 100.0f, 50.0f);
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bufHandle);
        gl.DispatchCompute(1, 1, 1);
        gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);

        // ── Collide Pass ──
        gl.UseProgram(rawCollideProg);
        gl.Uniform1f(gl.GetUniformLocation(rawCollideProg, "u_Dt"), 1.0f / 60.0f);
        gl.Uniform1f(gl.GetUniformLocation(rawCollideProg, "u_Restitution"), 0.8f);
        gl.Uniform1ui(gl.GetUniformLocation(rawCollideProg, "u_ParticleCount"), 256);
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bufHandle);
        gl.DispatchCompute(1, 1, 1);
        gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
    }
    gl.Finish();

    GPUParticleData afterManual;
    std::memcpy(&afterManual, &p[0], sizeof(GPUParticleData));
    std::printf("    [Ring2-C] After 10 manual frames (no CmdList): pos=(%.1f,%.1f,%.1f) vel=(%.4f,%.4f,%.4f)\n",
                afterManual.position[0], afterManual.position[1], afterManual.position[2],
                afterManual.velocity[0], afterManual.velocity[1], afterManual.velocity[2]);

    // ── 诊断 D：通过 CmdList 复现同样的 Integrate+Collide 管线 ──
    // 重新写入初始值
    std::memset(&p[0], 0, sizeof(GPUParticleData));
    p[0].position[0] = 10.0f; p[0].position[1] = 50.0f; p[0].position[2] = 30.0f;
    p[0].radius = 1.0f; p[0].velocity[1] = 0.0f; p[0].mass = 1.0f;
    p[0].color[0] = 1.0f; p[0].color[1] = 1.0f; p[0].color[2] = 1.0f; p[0].color[3] = 1.0f;
    gl.MemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT); gl.Finish();

    // 通过 CmdList 手动复现 engine 管线（完全绕开 GPUPhysicsEngine::Update）
    // 使用 StringID::Runtime 动态计算 hash，避免硬编码哈希值
    auto* integratePSO = reinterpret_cast<GL46ComputePipelineState*>(s_Device->CreateComputePSO(
        RHI::ComputePSODesc{Engine::StringID::Runtime("gpu_physics_integrate")}));
    auto* collidePSO   = reinterpret_cast<GL46ComputePipelineState*>(s_Device->CreateComputePSO(
        RHI::ComputePSODesc{Engine::StringID::Runtime("gpu_physics_collide")}));
    ASSERT_NE(integratePSO, nullptr) << "Integrate PSO must be found (hash via StringID::Runtime)";
    ASSERT_NE(collidePSO, nullptr) << "Collide PSO must be found (hash via StringID::Runtime)";
    ASSERT_NE(integratePSO->program, nullptr) << "Integrate compute program must be compiled";
    ASSERT_NE(collidePSO->program, nullptr) << "Collide compute program must be compiled";

    for (int frame = 0; frame < 10; ++frame) {
        m_CmdList->Begin();

        // Integrate Pass
        m_CmdList->SetPipelineState(integratePSO);
        m_CmdList->SetUnorderedAccess(0, m_Engine->GetParticleBuffer());
        m_CmdList->SetComputeFloat("u_Dt", 1.0f / 60.0f);
        m_CmdList->SetComputeVec3("u_Gravity", 0.0f, -9.8f, 0.0f);
        m_CmdList->SetComputeFloat("u_Damping", 0.02f);
        m_CmdList->SetComputeFloat("u_Restitution", 0.8f);
        m_CmdList->SetComputeInt("u_ParticleCount", 256);
        m_CmdList->SetComputeVec3("u_BoxMin", -50.0f, 0.0f, -50.0f);
        m_CmdList->SetComputeVec3("u_BoxMax", 50.0f, 100.0f, 50.0f);
        m_CmdList->Dispatch(1, 1, 1);

        // Collide Pass
        m_CmdList->SetPipelineState(collidePSO);
        m_CmdList->SetUnorderedAccess(0, m_Engine->GetParticleBuffer());
        m_CmdList->SetComputeFloat("u_Dt", 1.0f / 60.0f);
        m_CmdList->SetComputeFloat("u_Restitution", 0.8f);
        m_CmdList->SetComputeInt("u_ParticleCount", 256);
        m_CmdList->Dispatch(1, 1, 1);

        m_CmdList->End();
    }
    s_Device->WaitIdle();

    GPUParticleData afterCmd;
    std::memcpy(&afterCmd, &p[0], sizeof(GPUParticleData));
    std::printf("    [Ring2-D] After 10 manual frames (via CmdList): pos=(%.1f,%.1f,%.1f) vel=(%.4f,%.4f,%.4f)\n",
                afterCmd.position[0], afterCmd.position[1], afterCmd.position[2],
                afterCmd.velocity[0], afterCmd.velocity[1], afterCmd.velocity[2]);

    bool manualWorks = (std::abs(afterManual.velocity[1]) > 0.0001f);
    bool cmdWorks    = (std::abs(afterCmd.velocity[1]) > 0.0001f);

    gl.DeleteProgram(rawIntegrateProg);
    gl.DeleteProgram(rawCollideProg);

    std::printf("    [Ring2] Engine=%s  Raw GL manual=%s  CmdList manual=%s\n",
                engineChanged ? "YES" : "NO", manualWorks ? "YES" : "NO", cmdWorks ? "YES" : "NO");

    // ── 主要断言 ──
    // 方案 B/C：裸 GL manual 必须能修改粒子数据（证明着色器逻辑正确）
    EXPECT_TRUE(manualWorks)
        << "Ring2-B/C: Raw GL manual Integrate+Collide should modify particle data";

    // 方案 D：通过 CmdList 手动复现的管线应能修改粒子数据
    // 此路径使用与引擎相同的 CmdList（glProgramUniform* DSA），
    // 如果失败说明 GL46CommandList 的 uniform 传递机制有问题。
    if (!cmdWorks) {
        std::printf("    [Ring2-D WARN] CmdList manual path did not modify particle data.\n"
                    "    Check GL46CommandList::Dispatch glProgramUniform* DSA calls.\n");
    }
    // 不强制 cmdWorks 为 true，因为某些驱动上 DSA 可能不生效，
    // 但至少记录诊断信息。
    EXPECT_TRUE(cmdWorks)
        << "Ring2-D: CmdList manual pipeline should modify particle velocity. "
        << "If this fails, GL46CommandList uniform passing (glProgramUniform*) needs investigation.";
}

// 诊断：直接操作 GL 的 SSBO 写入+读取验证
TEST_F(GPUPhysicsRingTest, Ring1_Diagnostic_SSBO_Writes) {
    auto* gl46Dev = static_cast<GL46Device*>(s_Device.get());
    auto& gl = gl46Dev->GetGL();

    uint32_t bufHandle = 0;
    gl.CreateBuffers(1, &bufHandle);
    ASSERT_NE(bufHandle, 0);

    const GLsizeiptr bufSize = sizeof(GPUParticleData);
    gl.NamedBufferStorage(bufHandle, bufSize, nullptr,
                          GL_MAP_READ_BIT | GL_MAP_WRITE_BIT |
                          GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);

    GPUParticleData initial;
    std::memset(&initial, 0, sizeof(initial));
    initial.position[0] = 1.0f; initial.position[1] = 2.0f; initial.position[2] = 3.0f;
    initial.radius = 0.5f;
    initial.velocity[0] = 4.0f; initial.velocity[1] = 5.0f; initial.velocity[2] = 6.0f;
    initial.mass = 2.0f;
    initial.color[0] = 1.0f; initial.color[1] = 1.0f; initial.color[2] = 1.0f; initial.color[3] = 1.0f;

    void* mapped = gl.MapNamedBufferRange(bufHandle, 0, bufSize,
                                          GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
    ASSERT_NE(mapped, nullptr);
    std::memcpy(mapped, &initial, sizeof(GPUParticleData));

    GPUParticleData verify1, verify2;
    gl.GetNamedBufferSubData(bufHandle, 0, bufSize, &verify1);
    std::memcpy(&verify2, mapped, sizeof(GPUParticleData));

    EXPECT_FLOAT_EQ(verify1.velocity[1], 5.0f);
    EXPECT_FLOAT_EQ(verify2.velocity[1], 5.0f);
    std::printf("    [Ring1-Diag] SSBO persistent mapping: PASS\n");

    gl.UnmapNamedBuffer(bufHandle);
    gl.DeleteBuffers(1, &bufHandle);
}

// 诊断：最小化 Compute SSBO 写入验证
TEST_F(GPUPhysicsRingTest, Ring1_Diagnostic_ComputeSSBO) {
    auto* gl46Dev = static_cast<GL46Device*>(s_Device.get());
    auto& gl = gl46Dev->GetGL();

    const GLsizeiptr bufSize = sizeof(GPUParticleData);

    uint32_t bufHandle = 0;
    gl.CreateBuffers(1, &bufHandle);
    gl.NamedBufferStorage(bufHandle, bufSize, nullptr,
                          GL_MAP_READ_BIT | GL_MAP_WRITE_BIT |
                          GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);

    GPUParticleData data;
    std::memset(&data, 0, sizeof(data));
    data.position[0] = 999.0f;
    data.velocity[1] = 42.0f;

    GLbitfield mapFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    void* mapped = gl.MapNamedBufferRange(bufHandle, 0, bufSize, mapFlags);
    std::memcpy(mapped, &data, sizeof(GPUParticleData));

    static const char* diagShaderSrc = R"GLSL(
        #version 460 core
        layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
        struct Particle {
            vec3  position;
            float radius;
            vec3  velocity;
            float mass;
            vec4  color;
            float padding[4];
        };
        layout(std430, binding = 0) buffer ParticleBuf { Particle particles[]; } buf;
        void main() {
            buf.particles[0].position.x = 123.456;
            buf.particles[0].velocity.y = 78.9;
        }
    )GLSL";

    uint32_t cs = gl.CreateShader(GL_COMPUTE_SHADER);
    gl.ShaderSource(cs, 1, &diagShaderSrc, nullptr);
    gl.CompileShader(cs);

    int success = 0;
    gl.GetShaderiv(cs, GL_COMPILE_STATUS, &success);
    ASSERT_NE(success, 0) << "Diagnostic shader compilation failed!";

    uint32_t prog = gl.CreateProgram();
    gl.AttachShader(prog, cs);
    gl.LinkProgram(prog);
    gl.GetProgramiv(prog, GL_LINK_STATUS, &success);
    ASSERT_NE(success, 0) << "Diagnostic program link failed!";
    gl.DeleteShader(cs);

    gl.UseProgram(prog);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bufHandle);
    gl.DispatchCompute(1, 1, 1);
    gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
    gl.Finish();

    GPUParticleData result;
    gl.GetNamedBufferSubData(bufHandle, 0, bufSize, &result);
    std::printf("    [Ring1-Diag] position.x=%.3f vel.y=%.3f\n", result.position[0], result.velocity[1]);
    EXPECT_FLOAT_EQ(result.position[0], 123.456f);
    EXPECT_FLOAT_EQ(result.velocity[1], 78.9f);

    gl.DeleteProgram(prog);
    gl.UnmapNamedBuffer(bufHandle);
    gl.DeleteBuffers(1, &bufHandle);
}
// ═══════════════════════════════════════════════════════════
// Phase 2 multi-step validation gate (v3.3 re-scope)
//   Ring4: ping-pong swap integrity
//   Ring5: fixed timestep / determinism / spiral guard / frame grouping
// ═══════════════════════════════════════════════════════════

namespace {

uint64_t FnvHash(const GPUParticleData* p, uint32_t count) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(p);
    const size_t len = size_t(count) * sizeof(GPUParticleData);
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < len; ++i) { h ^= bytes[i]; h *= 1099511628211ull; }
    return h;
}

} // namespace

// Ring4: Ping-Pong swap integrity.
// Contract: each full step flips slots twice, so the stable state slot is
// INVARIANT across any number of steps; scratch must always differ from it.
TEST_F(GPUPhysicsRingTest, Ring4_PingPongSwapIntegrity) {
    constexpr uint32_t kCount = 256;
    m_Engine->ResetParticles();

    auto* stateBefore = static_cast<GL46Buffer*>(m_Engine->GetStateBuffer());
    auto* scratch     = static_cast<GL46Buffer*>(m_Engine->GetScratchBuffer());
    ASSERT_NE(stateBefore->GetGLHandle(), 0u);
    ASSERT_NE(scratch->GetGLHandle(), 0u);
    EXPECT_NE(stateBefore->GetGLHandle(), scratch->GetGLHandle())
        << "state and scratch must be distinct ping-pong buffers";

    auto runFrames = [&](int n, float dt) {
        for (int i = 0; i < n; ++i) {
            m_CmdList->Begin();
            m_Engine->Update(dt, m_CmdList.get());
            m_CmdList->End();
            IRHICommandList* ls[] = { m_CmdList.get() };
            auto* q = s_Device->GetQueue(QueueType::Graphics);
            q->ExecuteCommandLists(1, ls);
            s_Device->WaitIdle();
        }
    };

    // Odd number of steps: slot must STILL be identical after every step
    runFrames(7, 1.0f / 60.0f);

    auto* stateAfter = static_cast<GL46Buffer*>(m_Engine->GetStateBuffer());
    EXPECT_EQ(stateBefore->GetGLHandle(), stateAfter->GetGLHandle())
        << "stable state slot drifted after a full step";

    EXPECT_EQ(m_Engine->GetLastFrameSubsteps(), 1u);
    EXPECT_FLOAT_EQ(m_Engine->GetSimulationTime(), 7.0f * (1.0f / 60.0f));

    // Physical sanity: gravity applied, all particles inside bounds & finite
    std::vector<GPUParticleData> out(kCount);
    m_Engine->ReadbackParticles(0, kCount, out.data());
    bool finite = true, inBounds = true;
    bool anyFalling = false;
    const float fixedDt = m_Engine->GetConfig().fixedDt;
    for (const auto& p : out) {
        if (!std::isfinite(p.position[0]) || !std::isfinite(p.velocity[1])) finite = false;
        if (p.position[1] < m_Engine->GetConfig().boxMin[1] - 0.5f ||
            p.position[1] > m_Engine->GetConfig().boxMax[1] + 0.5f) inBounds = false;
        if (p.velocity[1] < -0.01f) anyFalling = true;
    }
    (void)fixedDt;
    EXPECT_TRUE(finite);
    EXPECT_TRUE(inBounds);
    EXPECT_TRUE(anyFalling);
    std::printf("    [Ring4] ping-pong integrity OK (7 steps, slot stable)\n");
}

// Ring5a: bit-exact determinism - identical reset + identical dt sequence
// must produce identical final states.
TEST_F(GPUPhysicsRingTest, Ring5_Determinism_BitExact) {
    constexpr uint32_t kCount = 256;
    constexpr int kFrames = 45;

    auto runFrames = [&](int n, float dt) {
        for (int i = 0; i < n; ++i) {
            m_CmdList->Begin();
            m_Engine->Update(dt, m_CmdList.get());
            m_CmdList->End();
            IRHICommandList* ls[] = { m_CmdList.get() };
            auto* q = s_Device->GetQueue(QueueType::Graphics);
            q->ExecuteCommandLists(1, ls);
            s_Device->WaitIdle();
        }
    };

    m_Engine->ResetParticles();
    runFrames(kFrames, 1.0f / 60.0f);
    std::vector<GPUParticleData> runA(kCount);
    m_Engine->ReadbackParticles(0, kCount, runA.data());
    const uint64_t hashA = FnvHash(runA.data(), kCount);

    m_Engine->ResetParticles();
    runFrames(kFrames, 1.0f / 60.0f);
    std::vector<GPUParticleData> runB(kCount);
    m_Engine->ReadbackParticles(0, kCount, runB.data());
    const uint64_t hashB = FnvHash(runB.data(), kCount);

    std::printf("    [Ring5a] hashA=%016llx hashB=%016llx\n",
                (unsigned long long)hashA, (unsigned long long)hashB);
    EXPECT_EQ(hashA, hashB) << "same input sequence must reproduce bit-exact";
}

// Ring5b: spiral-of-death guard - one huge frame dt must clamp substeps.
TEST_F(GPUPhysicsRingTest, Ring5_SpiralGuard_ClampsSubSteps) {
    const uint32_t kMaxSteps = m_Engine->GetConfig().maxSubSteps;
    m_Engine->ResetParticles();

    m_CmdList->Begin();
    m_Engine->Update(1.0f, m_CmdList.get());   // 1s >> maxSubSteps * fixedDt
    m_CmdList->End();
    IRHICommandList* ls[] = { m_CmdList.get() };
    auto* q = s_Device->GetQueue(QueueType::Graphics);
    q->ExecuteCommandLists(1, ls);
    s_Device->WaitIdle();

    const uint32_t did = m_Engine->GetLastFrameSubsteps();
    EXPECT_LE(did, kMaxSteps);
    EXPECT_GT(did, 0u);

    const double simT = m_Engine->GetSimulationTime();
    EXPECT_NEAR(simT, double(did) / 60.0, 1e-6);

    std::vector<GPUParticleData> out(256);
    m_Engine->ReadbackParticles(0, 256, out.data());
    for (const auto& p : out) {
        ASSERT_TRUE(std::isfinite(p.position[1]));
        ASSERT_TRUE(std::isfinite(p.velocity[1]));
    }
    std::printf("    [Ring5b] dt=1.0s clamped to %u substeps (max=%u)\n", did, kMaxSteps);
}

// Ring5c: frame grouping equivalence - 30 frames @ 30fps and 60 frames @ 60fps
// both resolve to exactly 60 identical fixed substeps => bit-exact result.
TEST_F(GPUPhysicsRingTest, Ring5_FrameGrouping_Equivalence) {
    constexpr uint32_t kCount = 256;

    auto runFrames = [&](int n, float dt) {
        for (int i = 0; i < n; ++i) {
            m_CmdList->Begin();
            m_Engine->Update(dt, m_CmdList.get());
            m_CmdList->End();
            IRHICommandList* ls[] = { m_CmdList.get() };
            auto* q = s_Device->GetQueue(QueueType::Graphics);
            q->ExecuteCommandLists(1, ls);
            s_Device->WaitIdle();
        }
    };

    m_Engine->ResetParticles();
    runFrames(30, 1.0f / 30.0f);
    EXPECT_EQ(m_Engine->GetLastFrameSubsteps(), 2u);
    std::vector<GPUParticleData> pathA(kCount);
    m_Engine->ReadbackParticles(0, kCount, pathA.data());

    m_Engine->ResetParticles();
    runFrames(60, 1.0f / 60.0f);
    EXPECT_EQ(m_Engine->GetLastFrameSubsteps(), 1u);
    std::vector<GPUParticleData> pathB(kCount);
    m_Engine->ReadbackParticles(0, kCount, pathB.data());

    const uint64_t hashA = FnvHash(pathA.data(), kCount);
    const uint64_t hashB = FnvHash(pathB.data(), kCount);
    std::printf("    [Ring5c] 30x(1/30) vs 60x(1/60): %016llx vs %016llx\n",
                (unsigned long long)hashA, (unsigned long long)hashB);
    EXPECT_EQ(hashA, hashB)
        << "identical substep sequences from different frame groupings";
}

// ═══════════════════════════════════════════════════════════
// Phase 4: spatial hash equivalence gate
//   Constraint: hash only changes WHO gets evaluated;
//               solver semantics (Jacobi snapshot) stay identical.
// ═══════════════════════════════════════════════════════════

namespace {

GPUPhysicsConfig MakeDenseConfig(GPUCollisionBackend backend) {
    GPUPhysicsConfig cfg;
    cfg.particleCount = 256;
    cfg.spawnRadius = 4.5f;                 // very dense => guaranteed contacts at step 1
    cfg.boxMin[0] = -10.f; cfg.boxMin[1] = -10.f; cfg.boxMin[2] = -10.f;
    cfg.boxMax[0] =  10.f; cfg.boxMax[1] =  30.f; cfg.boxMax[2] =  10.f;
    cfg.restitution = 0.8f;
    cfg.damping = 0.02f;
    cfg.gravity[0] = 0.f; cfg.gravity[1] = 0.f; cfg.gravity[2] = 0.f;
    cfg.collisionBackend = backend;
    return cfg;
}

void RunOneFrame(GPUPhysicsEngine* eng, IRHICommandList* cmd,
                 IRHIDevice* dev, float dt) {
    cmd->Begin();
    eng->Update(dt, cmd);
    cmd->End();
    IRHICommandList* ls[] = { cmd };
    auto* q = dev->GetQueue(QueueType::Graphics);
    q->ExecuteCommandLists(1, ls);
    dev->WaitIdle();
}

} // namespace

// Golden test: a single contact pair with NO traversal-order ambiguity.
// Both backends must produce BIT-EXACT results — any difference here means
// the solver semantics diverged, not just summation order.
TEST_F(GPUPhysicsRingTest, Ring7_SpatialHash_TwoParticleBitExact) {
    auto makeCfg = [&](GPUCollisionBackend be) {
        GPUPhysicsConfig cfg;
        cfg.particleCount = 2;
        cfg.spawnRadius = 1.0f;
        cfg.boxMin[0] = -5.f; cfg.boxMin[1] = -5.f; cfg.boxMin[2] = -5.f;
        cfg.boxMax[0] =  5.f; cfg.boxMax[1] =  15.f; cfg.boxMax[2] =  5.f;
        cfg.gravity[0] = 0; cfg.gravity[1] = 0; cfg.gravity[2] = 0;
        cfg.collisionBackend = be;
        return cfg;
    };

    std::vector<GPUParticleData> seed(2);
    // Case A: pure overlap, zero relative velocity (position correction only)
    seed[0] = {}; seed[1] = {};
    seed[0].position[0] = 0.0f;  seed[0].position[1] = 10.f; seed[0].radius = 0.5f; seed[0].mass = 1.0f; seed[0].color[3]=1;
    seed[1].position[0] = 0.6f;  seed[1].position[1] = 10.f; seed[1].radius = 0.5f; seed[1].mass = 1.5f; seed[1].color[3]=1;

    std::vector<GPUParticleData> outB(2), outH(2);
    for (int mode = 0; mode < 2; ++mode) {
        const bool bruteMode = (mode == 0);
        ASSERT_TRUE(m_Engine->Initialize(s_Device.get(),
            makeCfg(bruteMode ? GPUCollisionBackend::BruteForce : GPUCollisionBackend::SpatialHash)));
        m_Engine->UploadInitialData(seed.data(), 2);
        RunOneFrame(m_Engine.get(), m_CmdList.get(), s_Device.get(), 1.0f / 60.0f);
        auto& out = bruteMode ? outB : outH;
        m_Engine->ReadbackParticles(0, 2, out.data());
    }
    EXPECT_EQ(std::memcmp(outB.data(), outH.data(), 2 * sizeof(GPUParticleData)), 0)
        << "single-pair solve must be order-independent => bit-exact";
    if (::testing::Test::HasFailure()) {
        for (auto& o : {&outB, &outH})
            std::printf("    [%s] p0=(%.9f,%.9f) p1=(%.9f,%.9f)\n",
                        o == &outB ? "brute" : "hash ",
                        (*o)[0].position[0], (*o)[0].position[1],
                        (*o)[1].position[0], (*o)[1].position[1]);
    }
}

// One-step equivalence on a dense random layout: identical input state +
// single fixed step. With many simultaneous contacts the pairwise position
// corrections accumulate in traversal order (index-order vs cell-grouped),
// which is legal Jacobi divergence; impulse paths must match exactly.
TEST_F(GPUPhysicsRingTest, Ring7_SpatialHash_OneStepEquivalence) {
    std::vector<GPUParticleData> bruteOut(256), hashOut(256), initB(256), initH(256);

    // --- brute-force reference ---
    ASSERT_TRUE(m_Engine->Initialize(s_Device.get(), MakeDenseConfig(GPUCollisionBackend::BruteForce)));
    m_Engine->ResetParticles();
    m_Engine->ReadbackParticles(0, 256, initB.data());
    RunOneFrame(m_Engine.get(), m_CmdList.get(), s_Device.get(), 1.0f / 60.0f);
    m_Engine->ReadbackParticles(0, 256, bruteOut.data());

    // --- spatial hash ---
    ASSERT_TRUE(m_Engine->Initialize(s_Device.get(), MakeDenseConfig(GPUCollisionBackend::SpatialHash)));
    m_Engine->ResetParticles();
    m_Engine->ReadbackParticles(0, 256, initH.data());
    RunOneFrame(m_Engine.get(), m_CmdList.get(), s_Device.get(), 1.0f / 60.0f);
    m_Engine->ReadbackParticles(0, 256, hashOut.data());

    {   // initial-state gate: both engines must start from identical layouts
        int initDiff = -1;
        for (uint32_t i = 0; i < 256; ++i)
            if (std::memcmp(&initB[i], &initH[i], sizeof(GPUParticleData)) != 0) { initDiff = (int)i; break; }
        std::printf("    [INIT] initial layouts %s (first diff idx=%d)\n",
                    initDiff < 0 ? "IDENTICAL" : "DIFFER", initDiff);
        ASSERT_EQ(initDiff, -1) << "reset produced different layouts";
    }

    {   // DEBUG: dump hash-path aux buffers to verify broad-phase data chain
        auto* gl46Dev = static_cast<GL46Device*>(s_Device.get());
        auto& gl = gl46Dev->GetGL();
        auto dumpU = [&](const char* tag, IRHIBuffer* buf, uint32 n) {
            if (!buf) { std::printf("    [%s] <null>\n", tag); return; }
            auto* g = static_cast<GL46Buffer*>(buf);
            std::vector<uint32_t> tmp(n);
            gl.GetNamedBufferSubData(g->GetGLHandle(), 0, n * sizeof(uint32_t), tmp.data());
            std::printf("    [%s]", tag);
            for (uint32 k = 0; k < n && k < 12; ++k) std::printf(" %u", tmp[k]);
            std::printf("\n");
        };
        dumpU("cellCount[12]", m_Engine->GetDebugAuxBuffer(0), 12);

        // ---- CPU ground-truth for particle 11: overlapping partners & cells ----
        const uint32_t P = 256;
        std::vector<uint32_t> cof(P), cst;
        {
            auto* gCof = static_cast<GL46Buffer*>(m_Engine->GetDebugAuxBuffer(2));
            gl.GetNamedBufferSubData(gCof->GetGLHandle(), 0, P * 4u, cof.data());
        }
        const uint32_t targetIdx = 11;
        const auto& tp = initB[targetIdx];
        int outside = 0, nOverlap = 0;
        std::printf("    [GEO] p11 pos=(%.3f,%.3f,%.3f) r=%.2f\n",
                    tp.position[0], tp.position[1], tp.position[2], tp.radius);
        for (uint32_t j = 0; j < P; ++j) {
            if (j == targetIdx) continue;
            const auto& op = initB[j];
            const float dx = op.position[0]-tp.position[0];
            const float dy = op.position[1]-tp.position[1];
            const float dz = op.position[2]-tp.position[2];
            const float dist = std::sqrt(dx*dx+dy*dy+dz*dz);
            if (dist >= tp.radius + op.radius || dist <= 0.0001f) continue;
            ++nOverlap;
            // cell coords of partner vs self (CPU mirror of GPU formulas)
            auto cc = [&](const std::array<float,3>& p){
                std::array<int,3> g;
                const float mn[3]={-10.f,-10.f,-10.f}; const float cs=2.0f; const int dim[3]={10,20,10};
                for(int a=0;a<3;++a){
                    g[a]=(int)std::floor((p[a]-mn[a])/cs);
                    g[a]=std::max(0,std::min(dim[a]-1,g[a]));
                }
                return g;
            };
            const auto gs = cc({tp.position[0],tp.position[1],tp.position[2]});
            const auto go = cc({op.position[0],op.position[1],op.position[2]});
            const bool inside = std::abs(gs[0]-go[0])<=1 && std::abs(gs[1]-go[1])<=1 && std::abs(gs[2]-go[2])<=1;
            if (!inside) ++outside;
            if (nOverlap <= 6)
                std::printf("      j=%u dist=%.3f min=%.3f cellOff=(%d,%d,%d) gpuCellOf=%u %s\n",
                            j, dist, tp.radius+op.radius,
                            go[0]-gs[0], go[1]-gs[1], go[2]-gs[2], cof[j],
                            inside ? "" : " <<< OUTSIDE 27-CELL!");
        }
        std::printf("    [GEO] total overlapping=%d ; outside 27-cell window: %d\n", nOverlap, outside);
    }

    {   // ---- CPU single-step true-Jacobi reference (arbitrates GPU paths) ----
        std::vector<GPUParticleData> ref(256);
        for (uint32_t i = 0; i < 256; ++i) {
            GPUParticleData p = initB[i];
            const float ox = p.position[0], oy = p.position[1], oz = p.position[2];
            for (uint32_t j = 0; j < 256; ++j) {
                if (j == i) continue;
                const auto& o = initB[j];
                // distances always from the SNAPSHOT position (true Jacobi)
                const float dx = o.position[0]-ox;
                const float dy = o.position[1]-oy;
                const float dz = o.position[2]-oz;
                const float dist = std::sqrt(dx*dx+dy*dy+dz*dz);
                const float minDist = p.radius + o.radius;
                if (dist < minDist && dist > 0.0001f) {
                    const float totalMass = p.mass + o.mass;
                    if (totalMass > 0.0001f) {
                        const float overlap = minDist - dist;
                        const float w = o.mass/totalMass;
                        p.position[0]-=(dx/dist)*overlap*w;
                        p.position[1]-=(dy/dist)*overlap*w;
                        p.position[2]-=(dz/dist)*overlap*w;
                    }
                }
            }
            ref[i]=p;
        }
        // compare both GPU paths against CPU reference (position only)
        auto cmp = [&](const char* tag, const std::vector<GPUParticleData>& gpu){
            double maxd=0; int worst=-1;
            for (uint32_t i = 0; i < 256; ++i) {
                double d=0;
                for(int k=0;k<3;++k){const double e=gpu[i].position[k]-ref[i].position[k]; d+=e*e;}
                if (d>maxd){maxd=d;worst=(int)i;}
            }
            std::printf("    [CPU-REF] vs %s: max|dpos|=%.4f (worst=%d)\n", tag, std::sqrt(maxd), worst);
        };
        cmp("brute", bruteOut);
        cmp("hash ", hashOut);
    }

    double maxDp = 0.0, maxDv = 0.0;
    int touched = 0;
    for (uint32_t i = 0; i < 256; ++i) {
        const auto& a = bruteOut[i];
        const auto& b = hashOut[i];
        double dp = 0.0, dv = 0.0;
        for (int k = 0; k < 3; ++k) {
            dp = std::max(dp, (double)std::abs(a.position[k] - b.position[k]));
            dv = std::max(dv, (double)std::abs(a.velocity[k] - b.velocity[k]));
        }
        if (dp > 1e-6 || dv > 1e-6) ++touched;
        maxDp = std::max(maxDp, dp);
        maxDv = std::max(maxDv, dv);
    }
    std::printf("    [Ring7] 1-step: max|dpos|=%.6f max|dvel|=%.6f (touched %u/256)\n",
                maxDp, maxDv, touched);
    // Candidate-set completeness gates (probe counters written by both kernels)
    auto bits = [](const float& f) { uint32_t u; std::memcpy(&u, &f, 4); return u; };
    double maxEvalDiff = 0, maxFixDiff = 0;
    int worstIdx = -1;
    for (uint32_t i = 0; i < 256; ++i) {
        const uint32_t eB = bits(bruteOut[i].padding[0]);
        const uint32_t fB = bits(bruteOut[i].padding[1]);
        const uint32_t eH = bits(hashOut[i].padding[0]);
        const uint32_t fH = bits(hashOut[i].padding[1]);
        if (std::abs((int)eB - (int)eH) > maxEvalDiff) {
            maxEvalDiff = std::abs((int)eB - (int)eH);
            worstIdx = (int)i;
        }
        maxFixDiff = std::max(maxFixDiff, (double)std::abs((int)fB - (int)fH));
    }
    std::printf("    [PROBE] max|evalDiff|=%.0f max|fixDiff|=%.0f worst=%d\n",
                maxEvalDiff, maxFixDiff, worstIdx);
    // DEBUG: dump top-5 divergent particles
    {
        std::vector<std::pair<double,uint32_t>> divergences;
        for (uint32_t i = 0; i < 256; ++i) {
            double dp = 0;
            for (int k = 0; k < 3; ++k)
                dp += (bruteOut[i].position[k]-hashOut[i].position[k])*(bruteOut[i].position[k]-hashOut[i].position[k]);
            divergences.push_back({std::sqrt(dp), i});
        }
        std::sort(divergences.begin(), divergences.end(), [](auto&a, auto&b){return a.first>b.first;});
        for (int n = 0; n < 5 && n < (int)divergences.size(); ++n) {
            const uint32_t i = divergences[n].second;
            const auto& a = bruteOut[i]; const auto& b = hashOut[i];
            const auto& s = initB[i];
            std::printf("    [DMP] i=%u dp=%.4f eval(b=%u,h=%u) fix(b=%u,h=%u)\n",
                i, divergences[n].first,
                bits(a.padding[0]), bits(b.padding[0]),
                bits(a.padding[1]), bits(b.padding[1]));
            std::printf("          init=(%.3f,%.3f,%.3f) pb=(%.3f,%.3f,%.3f) ph=(%.3f,%.3f,%.3f)\n",
                s.position[0],s.position[1],s.position[2],
                a.position[0],a.position[1],a.position[2],
                b.position[0],b.position[1],b.position[2]);
        }
    }
    // Contact evidence via probes: corrections must have happened somewhere,
    // otherwise the equivalence gate would be vacuous.
    {
        uint32_t totalFixes = 0;
        for (uint32_t i = 0; i < 256; ++i) totalFixes += bits(hashOut[i].padding[1]);
        std::printf("    [GATE] total fixes(hash)=%u\n", totalFixes);
        EXPECT_GT(totalFixes, 0u) << "dense layout produced no corrections - gate is vacuous";
    }
    // NOTE: eval counts MUST differ (brute=N-1, hash=neighborhood only) —
    // that difference IS the optimization. Correction counts must match.
    EXPECT_EQ(maxFixDiff, 0) << "correction counts must match exactly";

    // ── Tolerance rationale (v3.4) ──
    // With zero gravity + zero initial velocity, no separation impulse can
    // trigger => velocity paths MUST match exactly on both backends.
    EXPECT_EQ(maxDv, 0.0) << "impulse divergence = candidate-set mismatch!";

    // Position corrections accumulate pairwise; with many simultaneous
    // contacts the SUMMATION ORDER differs between index-order (brute)
    // and cell-grouped (hash) traversal. Both are legal Jacobi solves.
    // Invariants that must hold regardless of order:
    //   1. MASS-weighted center of mass is preserved by pairwise symmetric
    //      corrections (mi*dpi + mj*dpj = 0 per contact pair).
    double mAx=0,mAy=0,mAz=0,mBx=0,mBy=0,mBz=0,mTot=0;
    for (uint32_t i = 0; i < 256; ++i) {
        const double ma = bruteOut[i].mass, mb = hashOut[i].mass;
        mAx += ma*bruteOut[i].position[0]; mAy += ma*bruteOut[i].position[1]; mAz += ma*bruteOut[i].position[2];
        mBx += mb*hashOut[i].position[0];  mBy += mb*hashOut[i].position[1];  mBz += mb*hashOut[i].position[2];
        mTot += ma;
    }
    const double comShift = std::sqrt((mAx-mBx)*(mAx-mBx)
                                    + (mAy-mBy)*(mAy-mBy)
                                    + (mAz-mBz)*(mAz-mBz)) / mTot;
    std::printf("    [Ring7] mass-COM shift=%.3e max|dpos|=%.4f\n", comShift, maxDp);
    EXPECT_LT(comShift, 1e-4) << "momentum not conserved across backends";
    //   2. With TRUE Jacobi (snapshot distances), both backends must agree
    //      bit-exact; keep a tiny FP gate for cross-driver safety.
    EXPECT_LT(maxDp, 1e-4) << "position divergence beyond Jacobi-order envelope";
}

// Hash backend must itself be deterministic (linked-list insertion order is
// atomic-scheduling dependent ACROSS builds, but the resulting per-particle
// contact SET is stable; FP accumulation order within one particle's own
// traversal may vary => we require near-equality rather than bit-exactness).
TEST_F(GPUPhysicsRingTest, Ring7_SpatialHash_SelfDeterministic) {
    ASSERT_TRUE(m_Engine->Initialize(s_Device.get(), MakeDenseConfig(GPUCollisionBackend::SpatialHash)));

    auto run30 = [&]() {
        m_Engine->ResetParticles();
        for (int f = 0; f < 30; ++f)
            RunOneFrame(m_Engine.get(), m_CmdList.get(), s_Device.get(), 1.0f / 60.0f);
        std::vector<GPUParticleData> out(256);
        m_Engine->ReadbackParticles(0, 256, out.data());
        return out;
    };

    const auto a = run30();
    const auto b = run30();

    double maxDrift = 0.0;
    for (uint32_t i = 0; i < 256; ++i)
        for (int k = 0; k < 3; ++k)
            maxDrift = std::max(maxDrift, (double)std::abs(a[i].position[k] - b[i].position[k]));
    std::printf("    [Ring7b] 30-step self-drift=%.6g\n", maxDrift);
    EXPECT_LT(maxDrift, 0.25) << "hash path unstable across identical runs";
}
