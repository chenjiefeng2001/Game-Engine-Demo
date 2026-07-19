/**
 * @file GL46DeviceTest.cpp
 * @brief GL46 设备无头 GPU 测试
 *
 * 测试重点：
 * - 设备创建/缓冲分配
 * - 持久映射读写
 * - Compute Shader 执行
 *
 * 注意：无 GL 4.6 GPU 时自动跳过
 */
#include <gtest/gtest.h>
#include "Engine/Core/RHI/GL46AZDODevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/RHITypes.h"
#include <GLFW/glfw3.h>
#include <memory>
#include <cstring>
#include <cstdio>

using namespace Engine::RHI;

class GL46DeviceTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        if (!glfwInit()) { return; }
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
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
    EXPECT_EQ(buffer->GetDesc().size, 1024);
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
    auto* pso = s_Device->CreateComputePSO(psoDesc);
    ASSERT_NE(pso, nullptr);

    // Dispatch
    cmdList->Begin();
    cmdList->SetPipelineState(pso);
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