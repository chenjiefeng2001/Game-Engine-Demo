#pragma once

/**
 * @file DeferredLightingPass.h
 * @brief 延迟渲染光照 Pass — 全屏 Quad + PBR Cook-Torrance BRDF + CSM
 *
 * 集成方式：
 * @code
 *   DeferredLightingPass lighting;
 *   lighting.Initialize(device, shadowMap);
 *
 *   rg.AddPass("Lighting", [&](RenderPassBuilder& builder) {
 *       builder.ReadTexture(SID("GBuffer_AlbedoAO"));
 *       builder.ReadTexture(SID("GBuffer_NormalRgh"));
 *       builder.WriteTexture(SID("HDRSceneColor"));
 *       builder.ReadTexture(SID("ShadowMap"));
 *   }, [&](IRHICommandList& cmd) {
 *       lighting.Execute(cmd, lights, csmData);
 *   });
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Rendering/LightTypes.h"

namespace Engine {
    namespace RHI {
        class IRHIDevice;
        class IRHICommandList;
        class IRHIPipelineState;
    }

    namespace Rendering {

        // ============================================================
        // DeferredLightingPass — 延迟光照 Pass
        // ============================================================
        class DeferredLightingPass {
        public:
            DeferredLightingPass() = default;
            ~DeferredLightingPass() = default;

            // 禁用拷贝
            DeferredLightingPass(const DeferredLightingPass&) = delete;
            DeferredLightingPass& operator=(const DeferredLightingPass&) = delete;

            /**
             * @brief 初始化延迟光照管线
             *
             * 编译全屏四边形 PSO：
             *   - Vertex: fullscreen_quad_vs
             *   - Fragment: deferred_lighting_ps (含 PBR BRDF + CSM shadow)
             *
             * @param device RHI 设备
             * @return true 表示成功
             */
            bool Initialize(RHI::IRHIDevice& device);

            /** 是否已初始化 */
            bool IsInitialized() const noexcept { return m_Initialized; }

            /**
             * @brief 执行延迟光照 Pass
             *
             * @param cmd          命令列表
             * @param lights       光源数组
             * @param lightCount   光源数量
             * @param csmData      级联阴影数据（可为 nullptr）
             */
            void Execute(RHI::IRHICommandList& cmd,
                         const GPULight* lights,
                         uint32 lightCount,
                         const CSMUBOData* csmData = nullptr);

        private:
            bool m_Initialized = false;

            // 全屏四边形 PSO（占位符，需要链接到 ShaderCompiler 编译的着色器）
            RHI::IRHIPipelineState* m_DeferredLightingPSO = nullptr;
        };

    } // namespace Rendering
} // namespace Engine