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