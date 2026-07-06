#pragma once

/**
 * @file Rendering3DTest.h
 * @brief Vulkan + D3D12 双后端 RHI 验证测试
 *
 * 重构说明（2025-07-07）：
 *   - 移除 VisualRenderDemo 可视化入口（与 RHI 测试无关）
 *   - 移除 TestTriangleRender（依赖 Dynamic Rendering，需要完整交换链/Shader）
 *   - 新增 TestECSOnly：纯 ECS 10K 实体性能测试
 *   - 保留 GPU Fill+Readback（Vulkan CmdFillBuffer 验证 GPU 执行）
 *   - D3D12 使用空命令提交 + WaitIdle 验证 GPU 管线
 *
 * 验证内容：
 *   1. Vulkan 后端设备创建
 *   2. D3D12 后端设备创建
 *   3. PSO 缓存命中/未命中
 *   4. 离屏 GPU Fill + 像素读回验证（Vulkan）
 *   5. D3D12 GPU 命令提交验证
 *   6. ECS 10,000 实体创建 + 查询迭代性能
 */

#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/IWindow.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/InputManager.h>
#include <Engine/Core/Renderer/PerspectiveCamera.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/Core/RHI/MeshRenderer.h>
#include <Engine/Core/RHI/IRHIDevice.h>
#include <Engine/Core/RHI/IRHICommandList.h>
#include <Engine/Core/RHI/PSOCache.h>
#include <Engine/Core/RHI/VulkanIRHIDevice.h>
#include <Engine/Core/RHI/D3D12IRHIDevice.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/GameObject/MeshComponent.h>
#include <Engine/Core/Renderer/Mesh.h>
#include <Engine/Core/Log.h>
#include <Engine/Types.h>

#include <memory>
#include <vector>
#include <string>
#include <cstdio>

namespace Engine {

class Rendering3DTest {
public:
    Rendering3DTest();
    ~Rendering3DTest();

    /** 运行所有后端测试 */
    void RunAll();

private:
    /** 单个后端测试 */
    struct BackendRenderTest {
        std::unique_ptr<RHI::IRHIDevice> device;
        bool initialized = false;
    };

    /** 初始化一个后端测试 */
    bool InitBackend(BackendRenderTest& test,
                     std::unique_ptr<RHI::IRHIDevice> device,
                     const char* backendName);

    /** 离屏渲染到纹理并读回验证（无 shader，仅颜色清除） */
    bool TestOffscreenClearAndReadback(RHI::IRHIDevice* device, const char* backendName);

    /**
     * @brief ECS 10,000 实体创建 + 查询迭代性能测试
     *
     * 不依赖任何渲染后端：
     *   1. 创建 10,000 个实体并添加 Position3D + SphereData 组件
     *   2. 注册组件类型，使用 EntityManager
     *   3. 查询迭代 100 帧更新位置（正弦摆动）
     *   4. 打印创建时间、迭代时间、总时间
     */
    bool TestECSOnly(RHI::IRHIDevice* device, const char* backendName);

    /** 报告测试结果 */
    void ReportResult(const char* name, bool passed);

    // ── 单个后端实例 ──
    BackendRenderTest m_VulkanTest;
    BackendRenderTest m_D3D12Test;

    // ── 统计 ──
    int m_Passed = 0;
    int m_Total  = 0;
    Logger m_Log = Logger("Rendering3DTest");

    // ── 固定参数 ──
    static constexpr int32 WINDOW_WIDTH  = 1024;
    static constexpr int32 WINDOW_HEIGHT = 768;
};

} // namespace Engine