/**
 * @file GL46DeviceTest.cpp
 * @brief GL46 设备无头 GPU 测试 + runner OpenGL 能力探针
 *
 * 测试重点：
 * - 设备创建/缓冲分配
 * - 持久映射读写
 * - Compute Shader 执行
 * - runner 实际能提供什么 GL 上下文（诊断，不是断言）
 *
 * 注意：无 GL 4.6 GPU 时自动跳过
 *
 * GPU 蒙皮像素验证**不在本文件**：它已改为按能力挑选后端（GL 4.6，否则 Vulkan），
 * 见 GPUSkinningPixelTest.cpp 与 SkinningPixelRig.h。
 */
#include <gtest/gtest.h>
#include "Engine/Core/RHI/GL46AZDODevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/RHITypes.h"
#include <GLFW/glfw3.h>
#include <Engine/Core/StringID.h>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <cstdio>
// Runner OpenGL capability probe. glad/gl.h (not glad/glad.h) is what this
// project vendors, and it provides the GladGLContext plus gladLoadGL.
#include <glad/gl.h>

using namespace Engine;
using namespace Engine::RHI;

class GL46DeviceTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        if (!glfwInit()) { return; }
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        // Deliberately requests 3.3 core rather than 4.6 with a fallback.
        // Nothing in this verification needs a 4.6 feature: the uniform array,
        // the four bone weighted blend, explicit attribute locations and the
        // offscreen draw are all available at 3.3. Asking for the minimum keeps
        // the local and CI execution paths identical, and the runner was
        // observed refusing a 4.6 core context outright.
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        s_Window = glfwCreateWindow(1, 1, "GL46Test", nullptr, nullptr);
        if (!s_Window) { glfwTerminate(); return; }
        glfwMakeContextCurrent(s_Window);

        s_Device = std::make_unique<GL46Device>();
        if (!s_Device->Initialize(nullptr, 1, 1)) {
            std::printf("  [WARN] GL46Device init failed\n");
            s_Device.reset();
        }
    }

    static void TearDownTestSuite() {
        // GL46Device::Shutdown() 是 private，依赖 unique_ptr 析构自动清理
        s_Device.reset();
        if (s_Window) { glfwDestroyWindow(s_Window); s_Window = nullptr; }
        glfwTerminate();
    }

    void SetUp() override {
        if (!s_Device) GTEST_SKIP() << "No GL 4.6 GPU available";
    }

    static std::unique_ptr<GL46Device> s_Device;
    static GLFWwindow* s_Window;
};

std::unique_ptr<GL46Device> GL46DeviceTest::s_Device = nullptr;
GLFWwindow* GL46DeviceTest::s_Window = nullptr;

TEST_F(GL46DeviceTest, DeviceName) {
    const char* name = s_Device->GetDeviceName();
    EXPECT_NE(name, nullptr);
    EXPECT_GT(std::strlen(name), 0);
    std::printf("  [INFO] GPU: %s\n", name);
}

TEST_F(GL46DeviceTest, CreateBuffer) {
    RHIBufferDesc desc;
    desc.size = 1024;
    desc.memoryUsage = MemoryUsage::GPU_Only;

    auto buffer = s_Device->CreateBuffer(desc);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->GetSize(), 1024);   // IRHIBuffer 契约接口（GetDesc 已移除）
}

TEST_F(GL46DeviceTest, BufferPersistentMapping) {
    RHIBufferDesc desc;
    desc.size = 64;
    desc.memoryUsage = MemoryUsage::GPU_Only;

    auto buffer = s_Device->CreateBuffer(desc);
    auto* gl46Buf = dynamic_cast<GL46Buffer*>(buffer.get());
    ASSERT_NE(gl46Buf, nullptr);

    void* mapped = gl46Buf->GetPersistentPtr();
    ASSERT_NE(mapped, nullptr);

    const char* testData = "Hello GPU!";
    std::memcpy(mapped, testData, 11);

    char readback[11];
    std::memcpy(readback, mapped, 11);
    EXPECT_EQ(std::memcmp(readback, testData, 11), 0);
}

TEST_F(GL46DeviceTest, CreateCommandList) {
    auto cmdList = s_Device->CreateCommandList(CommandListType::Direct);
    ASSERT_NE(cmdList, nullptr);
}

TEST_F(GL46DeviceTest, CreateAndDispatchComputeShader) {
    auto cmdList = s_Device->CreateCommandList(CommandListType::Direct);
    ASSERT_NE(cmdList, nullptr);

    // 创建 SSBO
    RHIBufferDesc bufDesc;
    bufDesc.size = 64;
    bufDesc.memoryUsage = MemoryUsage::GPU_Only;
    auto buffer = s_Device->CreateBuffer(bufDesc);

    // 创建 Compute PSO
    ComputePSODesc psoDesc;
    psoDesc.computeShader = StringID::Runtime("gpu_physics_integrate");
      // CreateComputePSO 现在返回 shared_ptr，auto* 无法推导。
      auto pso = s_Device->CreateComputePSO(psoDesc);
    ASSERT_NE(pso, nullptr);

    // Dispatch
    cmdList->Begin();
    cmdList->SetPipelineState(pso.get());
    cmdList->SetUnorderedAccess(0, buffer.get());
    cmdList->SetComputeFloat("u_Dt", 1.0f / 60.0f);
    cmdList->Dispatch(1, 1, 1);
    cmdList->End();
}

TEST_F(GL46DeviceTest, BufferSizeZero) {
    RHIBufferDesc desc;
    desc.size = 0;
    desc.memoryUsage = MemoryUsage::GPU_Only;
    auto buffer = s_Device->CreateBuffer(desc);
    // 大小为零的缓冲可能返回 nullptr 或有效缓冲（取决于实现）
    // 只要不崩溃即可
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Runner OpenGL capability probe.
//
// Reports what the runner can actually do, so the GLAD loader version question
// is decided by measurement instead of by inference. Two facts are recorded
// separately and must not be conflated:
//
//   1. what a default (no version hint) context reports, and
//   2. whether an explicit 4.5 core context can be created at all.
//
// A default context reporting 4.5 does NOT prove a 4.5 core context can be
// created. The probe never fails the build on capability grounds: it is a
// measurement, and it must stay runnable on machines without a usable driver.
// ---------------------------------------------------------------------------
namespace glcap {

static const char* SafeGetString(GladGLContext& gl, GLenum name)
{
    if (!gl.GetString) return "<no glGetString>";
    const GLubyte* s = gl.GetString(name);
    return s ? reinterpret_cast<const char*>(s) : "<null>";
}

// Returns true only if a core context of the requested version was created.
static bool TryCreateCoreContext(int major, int minor, GLFWwindow** outWindow)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* w = glfwCreateWindow(64, 64, "GL capability probe", nullptr, nullptr);
    if (!w) return false;
    if (outWindow) *outWindow = w;
    return true;
}

} // namespace glcap

TEST(GLRunnerCapabilityProbe, ReportRunnerOpenGLCapability)
{
    if (!glfwInit()) {
        std::printf("[GLPROBE] glfwInit failed; cannot probe\n");
        GTEST_SKIP() << "glfwInit failed";
    }

    // --- Fact 1: what a default context reports -------------------------
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* defWindow = glfwCreateWindow(64, 64, "GL default probe", nullptr, nullptr);
    if (!defWindow) {
        std::printf("[GLPROBE] default context: CREATE FAILED\n");
    } else {
        glfwMakeContextCurrent(defWindow);
        // Load into a local table the same way the engine does; glad/gl.h here
        // is GLAD2, which exposes GladGLContext rather than global symbols.
        GladGLContext probeGl;
        std::memset(&probeGl, 0, sizeof(probeGl));
        if (gladLoadGLContext(&probeGl, glfwGetProcAddress) == 0) {
            std::printf("[GLPROBE] gladLoadGLContext returned 0 on the default context\n");
        }
        std::printf("[GLPROBE] === default context report ===\n");
        std::printf("[GLPROBE] GL_VERSION                  = %s\n", glcap::SafeGetString(probeGl, GL_VERSION));
        std::printf("[GLPROBE] GL_VENDOR                   = %s\n", glcap::SafeGetString(probeGl, GL_VENDOR));
        std::printf("[GLPROBE] GL_RENDERER                 = %s\n", glcap::SafeGetString(probeGl, GL_RENDERER));
        std::printf("[GLPROBE] GL_SHADING_LANGUAGE_VERSION = %s\n", glcap::SafeGetString(probeGl, GL_SHADING_LANGUAGE_VERSION));

        GLint profileMask = 0;
        // GetIntegerv returns void; a zeroed mask means the query did not
        // produce a profile bit, which covers both "not queryable" and
        // "reported no profile".
        if (probeGl.GetIntegerv) {
            probeGl.GetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
        }
        if (profileMask != 0) {
            const bool core   = (profileMask & GL_CONTEXT_CORE_PROFILE_BIT) != 0;
            const bool compat = (profileMask & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) != 0;
            std::printf("[GLPROBE] GL_CONTEXT_PROFILE_MASK     = 0x%x (%s%s)\n",
                        profileMask,
                        core ? "core" : "",
                        compat ? " compatibility" : "");
        } else {
            std::printf("[GLPROBE] GL_CONTEXT_PROFILE_MASK     = <not queryable>\n");
        }

        // --- Fact 2: can an explicit 4.5 core context be created? --------
        // The engine's GL46Device.cpp calls GL 4.5 DSA entry points
        // (CreateBuffers, NamedBufferStorage, MapNamedBufferRange,
        // NamedBufferSubData), so a 4.5 loader is the real lower bound and
        // 4.5 creation is the fact that decides the handoff.
        GLFWwindow* w45 = nullptr;
        if (glcap::TryCreateCoreContext(4, 5, &w45)) {
            glfwMakeContextCurrent(w45);
            std::printf("[GLPROBE] 4.5 core context: CREATED, reports GL_VERSION = %s\n",
                        glcap::SafeGetString(probeGl, GL_VERSION));
            glfwDestroyWindow(w45);
        } else {
            std::printf("[GLPROBE] 4.5 core context: CREATE FAILED\n");
        }
        GLFWwindow* w46 = nullptr;
        if (glcap::TryCreateCoreContext(4, 6, &w46)) {
            std::printf("[GLPROBE] 4.6 core context: CREATED\n");
            glfwDestroyWindow(w46);
        } else {
            std::printf("[GLPROBE] 4.6 core context: CREATE FAILED\n");
        }

        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(defWindow);
    }

    std::fflush(stdout);
    glfwTerminate();
}
