/**
 * @file Rendering3DTest.cpp
 * @brief 精简版 RHI 验证测试 — 后端设备创建 + GPU 命令提交 + ECS 性能
 *
 * 重构说明（2025-07-07）：
 *   - 移除 VisualRenderDemo（OpenGL 可视化样例与本测试无关）
 *   - 移除依赖 Dynamic Rendering 的离屏渲染测试（需要完整交换链/Shader 编译/扩展加载）
 *   - Vulkan: GPU Fill + Readback 使用 CmdFillBuffer + CopyBuffer 验证 GPU 执行
 *   - D3D12: 命令提交 + WaitIdle 验证 GPU 管线
 *   - ECS: 10,000 实体创建 + 查询迭代性能测试（与后端无关）
 */

#include "Rendering3DTest.h"

#include <cstring>
#include <iostream>
#include <chrono>
#include <cstdio>

namespace Engine {

Rendering3DTest::Rendering3DTest() = default;
Rendering3DTest::~Rendering3DTest() = default;

void Rendering3DTest::ReportResult(const char* name, bool passed) {
    m_Total++;
    if (passed) m_Passed++;
    std::cout << "  [" << (passed ? "PASS" : "FAIL") << "] " << name << std::endl;
}

// ═══════════════════════════════════════════════════════════════════
// InitBackend — 初始化 RHI 后端并运行基础管线验证
// ═══════════════════════════════════════════════════════════════════

bool Rendering3DTest::InitBackend(BackendRenderTest& test,
                                   std::unique_ptr<RHI::IRHIDevice> device,
                                   const char* backendName) {
    if (!device) {
        m_Log.Warn("[{}] Device creation failed, skipping", backendName);
        return false;
    }

    // Headless 初始化 — 不创建窗口/交换链
    if (!device->Initialize(nullptr, WINDOW_WIDTH, WINDOW_HEIGHT)) {
        m_Log.Error("[{}] Device Initialize() failed", backendName);
        return false;
    }

    test.device = std::move(device);
    m_Log.Info("[{}] Device created and initialized: {}", backendName, test.device->GetDeviceName());

    // ── Test: Buffer 创建 ──
    {
        RHI::RHIBufferDesc bufDesc;
        bufDesc.memoryUsage = RHI::MemoryUsage::CPU_To_GPU;
        constexpr int numFloats = 1024;
        float initData[numFloats] = {};
        for (int i = 0; i < numFloats; ++i) initData[i] = (float)i;
        bufDesc.size = sizeof(initData);
        bufDesc.initialData = initData;

        auto buffer = test.device->CreateBuffer(bufDesc);
        if (!buffer) {
            m_Log.Error("[{}] Buffer creation failed", backendName);
            return false;
        }
        m_Log.Info("[{}] Buffer: {} bytes, alloc valid={}",
                   backendName, buffer->GetSize(), buffer->GetAllocation().IsValid());
    }

    // ── Test: CommandList 录制 ──
    {
        auto cmdList = test.device->CreateCommandList(RHI::CommandListType::Direct);
        if (!cmdList) {
            m_Log.Error("[{}] CommandList creation failed", backendName);
            return false;
        }
        cmdList->Begin();
        cmdList->SetViewport(RHI::Viewport{0, 0, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT, 0, 1});
        cmdList->SetScissorRect(RHI::Rect{0, 0, (int32)WINDOW_WIDTH, (int32)WINDOW_HEIGHT});
        cmdList->SetPrimitiveTopology(RHI::PrimitiveTopology::TriangleList);
        cmdList->End();
        m_Log.Info("[{}] CommandList recording OK", backendName);
    }

    // ── Test: PSO 缓存 ──
    {
        RHI::GraphicsPSODesc psoDesc;
        psoDesc.rtvFormats[0] = RHI::Format::BGRA8_UNorm;
        psoDesc.rtvCount = 1;
        psoDesc.dsvFormat = RHI::Format::D32_Float;

        auto pso1 = test.device->CreateGraphicsPSO(psoDesc);
        auto pso2 = test.device->CreateGraphicsPSO(psoDesc);
        (void)pso1; (void)pso2;

        m_Log.Info("[{}] PSO cache: hits={}, misses={}",
                   backendName,
                   RHI::PSOCache::Get().GetHitCount(),
                   RHI::PSOCache::Get().GetMissCount());
    }

    test.initialized = true;
    return true;
}

// ═══════════════════════════════════════════════════════════════════
// 主入口：依次运行所有后端
// ═══════════════════════════════════════════════════════════════════

void Rendering3DTest::RunAll() {
    std::cout << "\n=== Rendering3DTest: RHI Backend Verification ===" << std::endl;

    int backendsTested = 0;

    // ── Vulkan ──
#if defined(ENGINE_HAS_VULKAN)
    {
        bool hasVulkan = RHI::HasVulkanSupport();
        ReportResult("Vulkan Runtime Available", hasVulkan);

        if (hasVulkan) {
            auto device = RHI::CreateVulkanDevice();
            bool ok = InitBackend(m_VulkanTest, std::move(device), "Vulkan");
            ReportResult("Vulkan Backend Init", ok);
            if (ok) {
                backendsTested++;
                const char* name = m_VulkanTest.device->GetDeviceName();
                ReportResult("Vulkan Device Name", name != nullptr && strlen(name) > 0);

                // GPU Fill + Readback（Vulkan CmdFillBuffer 验证）
                bool gpuOk = TestOffscreenClearAndReadback(m_VulkanTest.device.get(), "Vulkan");
                ReportResult("Vulkan GPU Fill+Readback", gpuOk);

                // ECS 10,000 实体 + 查询性能
                bool ecsOk = TestECSOnly(m_VulkanTest.device.get(), "Vulkan");
                ReportResult("Vulkan ECS 10K Performance", ecsOk);
            }
        }
    }
#else
    ReportResult("Vulkan Support (ENGINE_HAS_VULKAN)", false);
#endif

    // ── D3D12 ──
#if defined(ENGINE_HAS_D3D12)
    {
        bool hasD3D12 = RHI::HasD3D12Support();
        ReportResult("D3D12 Runtime Available", hasD3D12);

        if (hasD3D12) {
            auto device = RHI::CreateD3D12Device();
            bool ok = InitBackend(m_D3D12Test, std::move(device), "D3D12");
            ReportResult("D3D12 Backend Init", ok);
            if (ok) {
                backendsTested++;
                const char* name = m_D3D12Test.device->GetDeviceName();
                ReportResult("D3D12 Device Name", name != nullptr && strlen(name) > 0);

                // GPU 命令提交验证
                bool gpuOk = TestOffscreenClearAndReadback(m_D3D12Test.device.get(), "D3D12");
                ReportResult("D3D12 GPU Command Submission", gpuOk);

                // ECS 10,000 实体 + 查询性能
                bool ecsOk = TestECSOnly(m_D3D12Test.device.get(), "D3D12");
                ReportResult("D3D12 ECS 10K Performance", ecsOk);
            }
        }
    }
#else
    ReportResult("D3D12 Support (ENGINE_HAS_D3D12)", false);
#endif

    // ── Summary ──
    std::cout << "\n=== Rendering3DTest Results ===" << std::endl;
    std::cout << "  Backends tested: " << backendsTested << std::endl;
    std::cout << "  Tests: " << m_Passed << "/" << m_Total << " passed" << std::endl;
    std::printf("Rendering3DTest: %d/%d passed (%d backends)\n",
                m_Passed, m_Total, backendsTested);
}

} // namespace Engine