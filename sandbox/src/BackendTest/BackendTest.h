#pragma once

/**
 * @file BackendTest.h
 * @brief 多后端功能验证测试 — 测试 Vulkan / D3D12 后端的 IRHI 接口完整性
 *
 * 验证内容：
 *   1. 后端检测：验证 Vulkan/D3D12 运行时是否可用
 *   2. 设备创建：IRHIDevice::Create 调用
 *   3. 缓冲管理：IRHIBuffer 上传/读取（CPU→GPU→Readback）
 *   4. 命令录制：IRHICommandList 录制/提交/执行
 *   5. 交换链：IRHISwapChain 创建与 Present
 *   6. PSO 缓存：PSOCache 命中/未命中测试
 *   7. ECS ↔ RHI 集成：ECS 驱动渲染资源
 */

#include <Engine/Core/ECS/ECS.h>
#include <Engine/Core/ECS/ComponentRegistry.h>
#include <Engine/Core/RHI/IRHIDevice.h>
#include <Engine/Core/RHI/IRHICommandList.h>
#include <Engine/Core/RHI/PSOCache.h>
#include <Engine/Core/RHI/GL46AZDODevice.h>
#include <Engine/Core/RHI/D3D12IRHIDevice.h>
#include <Engine/Core/RHI/VulkanIRHIDevice.h>
#include <Engine/Core/Log.h>
#include <cstdio>
#include <memory>
#include <vector>

namespace Engine {

// ═══════════════════════════════════════════════════════
// ECS 组件（用于集成测试）
// ═══════════════════════════════════════════════════════
struct Transform3D {
    Vec3 position{0,0,0};
    Vec3 rotation{0,0,0};
    Vec3 scale{1,1,1};
};

struct RenderProxy {
    RHI::IRHIBuffer* vertexBuffer = nullptr;
    RHI::IRHITexture* texture = nullptr;
    uint32 vertexCount = 0;
};

// ═══════════════════════════════════════════════════════
// 后端测试类
// ═══════════════════════════════════════════════════════
class BackendTest {
public:
    BackendTest();
    ~BackendTest();

    void Run();

private:
    // ── 核心测试 ──
    bool TestBackendAvailability();
    bool TestDeviceCreation();
    bool TestBufferUpload();
    bool TestCommandRecording();
    bool TestSwapChainCreation();
    bool TestPSOCache();

    // ── ECS 集成测试 ──
    bool TestECSComponentStorage();
    bool TestECSQueryWithRHI();

    void ReportResult(const char* name, bool passed);

    Logger m_Log = Logger("BackendTest");
    int m_Passed = 0;
    int m_Total  = 0;

    // ── RHI 设备 ──
    RHI::RHIDevicePtr m_VulkanDevice;
    RHI::RHIDevicePtr m_D3D12Device;

    // ── ECS ──
    EntityManager m_EntityMgr;
};

} // namespace Engine