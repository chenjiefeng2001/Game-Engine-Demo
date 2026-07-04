/**
 * @file PostProcessPipeline.cpp
 * @brief 后处理管线 — PSO 初始化 + UBO 上传实现
 */

#include "Engine/Rendering/PostProcessPipeline.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/PSODesc.h"
#include "Engine/Core/RHI/PSOCache.h"
#include "Engine/Core/Log.h"
#include <cstring>

namespace {
    Engine::Logger s_Log("PostProcess");
}

namespace Engine {
namespace Rendering {

    // ============================================================
    // 后处理着色器名称（由 ShaderCompiler 注册）
    // ============================================================
    static const StringID k_EffectShaderNames[] = {
        SID("post_bloom_downsample"),  // Bloom_Downsample
        SID("post_bloom_upsample"),    // Bloom_Upsample
        SID("post_bloom_composite"),   // Bloom_Composite
        SID("post_tonemap"),           // ToneMapping
        SID("post_fxaa"),              // FXAA
        SID("post_dof"),               // DepthOfField
        SID("post_color_grading"),     // ColorGrading
    };

    // ============================================================
    // Initialize — 编译所有后处理 PSO
    // ============================================================
    bool PostProcessPipeline::Initialize(::Engine::RHI::IRHIDevice& device) {
        if (m_Initialized) return true;

        ::Engine::RHI::PSOCache& cache = ::Engine::RHI::PSOCache::Get();

        for (int i = 0; i < static_cast<int>(PostProcessEffect::COUNT); ++i) {
            auto effect = static_cast<PostProcessEffect>(i);

            // 构建全屏四边形 PSO（Vertex = fullscreen_quad，Pixel = 后处理效果）
            ::Engine::RHI::GraphicsPSODesc desc = ::Engine::RHI::GraphicsPSODesc::DefaultUI();
            desc.vertexShader = SID("post_fullscreen_vs");
            desc.pixelShader  = k_EffectShaderNames[i];
            desc.depthStencil.depthTest = false;
            desc.depthStencil.depthWrite = false;
            desc.blend.enable = false;
            desc.rasterizer.cullMode = ::Engine::RHI::CullMode::None;

            m_PSOs[i] = cache.GetOrCreate(device, desc);
            if (!m_PSOs[i]) {
                s_Log.Error("Failed to create PSO for effect {}", static_cast<int>(effect));
                return false;
            }
        }

        m_Initialized = true;
        s_Log.Info("PostProcess pipeline initialized with {} PSOs",
                   static_cast<int>(PostProcessEffect::COUNT));
        return true;
    }

    // ============================================================
    // Shutdown
    // ============================================================
    void PostProcessPipeline::Shutdown() {
        for (auto& pso : m_PSOs) pso = nullptr;
        m_Initialized = false;
    }

    // ============================================================
    // UploadParams — 将 UBO 上传到命令列表
    // ============================================================
    void PostProcessPipeline::UploadParams(::Engine::RHI::IRHICommandList& cmd, const PostProcessUBO& params) {
        // 在完整 RHI 实现中，此方法应：
        //   1. 通过 IGPUMemoryAllocator::AllocateDynamic() 分配 CPU-visible 缓冲
        //   2. memcpy 到映射指针
        //   3. 绑定到着色器的 PostProcessUBO 槽位（binding 0）
        //
        // 当前 OpenGL 后端的过渡实现：
        //   glBindBufferBase(GL_UNIFORM_BUFFER, 0, uboID);
        //   glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(PostProcessUBO), &params);
        //
        // 待设备 + 命令列表完全接管后，此处调用 cmd.SetConstantBuffer(0, ...)
        (void)cmd;
        (void)params;
    }

} // namespace Rendering
} // namespace Engine