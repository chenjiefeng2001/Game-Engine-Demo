/**
 * @file DeferredLightingPass.cpp
 * @brief 延迟渲染光照 Pass 实现 — PSO 创建 + 全屏 Quad 绘制
 */

#include "Engine/Rendering/DeferredLightingPass.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/PSODesc.h"
#include "Engine/Core/RHI/PSOCache.h"
#include "Engine/Core/Log.h"

namespace {
    Engine::Logger s_Log("DeferredLight");
}

namespace Engine {
namespace Rendering {

    // ════════════════════════════════════════════════════════
    // Initialize — 编译延迟渲染 PSO
    // ════════════════════════════════════════════════════════

    bool DeferredLightingPass::Initialize(RHI::IRHIDevice& device) {
        if (m_Initialized) return true;

        // 构建延迟渲染全屏四边形 PSO
        RHI::GraphicsPSODesc desc = RHI::GraphicsPSODesc::DefaultUI();
        desc.vertexShader = SID("fullscreen_quad_vs");
        desc.pixelShader  = SID("deferred_lighting_ps");
        desc.blend.enable = false;          // 不混合，直接覆盖
        desc.depthStencil.depthTest  = false;  // GBuffer 已完成深度测试
        desc.depthStencil.depthWrite = false;
        desc.rasterizer.cullMode = RHI::CullMode::None;

        auto& cache = RHI::PSOCache::Get();
        m_DeferredLightingPSO = cache.GetOrCreate(device, desc);

        if (!m_DeferredLightingPSO) {
            s_Log.Error("Failed to create deferred lighting PSO");
            return false;
        }

        m_Initialized = true;
        s_Log.Info("DeferredLightingPass initialized");
        return true;
    }

    // ════════════════════════════════════════════════════════
    // Execute — 执行延迟光照
    // ════════════════════════════════════════════════════════

    void DeferredLightingPass::Execute(RHI::IRHICommandList& cmd,
                                        const GPULight* lights,
                                        uint32 lightCount,
                                        const CSMUBOData* csmData)
    {
        if (!m_Initialized || !m_DeferredLightingPSO) {
            s_Log.Error("DeferredLightingPass not initialized");
            return;
        }

        cmd.SetPipelineState(m_DeferredLightingPSO);
        cmd.SetPrimitiveTopology(RHI::PrimitiveTopology::TriangleList);

        // 绑定光源数据 UBO
        // TODO: 通过 DynamicUBOAllocator 上传 GPULight 数组
        // cmd.SetConstantBuffer(0, 0, lightUBO, 0, sizeof(GPULight) * lightCount);
        (void)lights;
        (void)lightCount;

        // 绑定级联阴影数据
        if (csmData) {
            // cmd.SetConstantBuffer(0, 1, csmUBO, 0, sizeof(CSMUBOData));
        }
        (void)csmData;

        // 绑定 GBuffer 纹理（假设已由 RenderGraph 的 barrier 管理状态）
        // 着色器期望：
        //   set=1, binding=0: GBuffer_AlbedoAO  (RGB10A2)
        //   set=1, binding=1: GBuffer_NormalRgh (RGB10A2)
        //   set=1, binding=2: GBuffer_Emissive  (RGBA8)
        //   set=1, binding=3: GBuffer_Depth     (D32)
        //   set=1, binding=4: ShadowMap         (D32 array)

        // 绘制全屏四边形（3 个顶点，三角形列表）
        cmd.Draw(3, 0);
    }

} // namespace Rendering
} // namespace Engine