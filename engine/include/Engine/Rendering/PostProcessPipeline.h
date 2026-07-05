#pragma once
#include "Engine/Types.h"
#include "Engine/Core/StringID.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include <memory>
#include <cstdint>

namespace Engine { namespace Rendering {

    struct alignas(16) PostProcessUBO {
        float bloomIntensity  = 1.0f;
        float bloomThreshold  = 1.0f;
        float bloomRadius     = 0.05f;
        float _pad0           = 0.0f;
        float exposure        = 1.0f;
        float gamma           = 2.2f;
        float _pad1[2]        = {};
        float brightness      = 1.0f;
        float contrast        = 1.0f;
        float saturation      = 1.0f;
        float colorGradingStrength = 1.0f;
    };
    static_assert(sizeof(PostProcessUBO) == 48, "");
    static_assert(alignof(PostProcessUBO) == 16, "");

    enum class PostProcessEffect : uint8 {
        Bloom_Downsample, Bloom_Upsample, Bloom_Composite,
        ToneMapping, FXAA, DepthOfField, ColorGrading, COUNT
    };

    class PostProcessPipeline {
    public:
        bool Initialize(::Engine::RHI::IRHIDevice& device);
        void Shutdown();
        ::Engine::RHI::IRHIPipelineState* GetPSO(PostProcessEffect e) const noexcept { return m_PSOs[static_cast<uint8>(e)]; }
        void UploadParams(::Engine::RHI::IRHICommandList& cmd, const PostProcessUBO& params);
        bool IsInitialized() const noexcept { return m_Initialized; }
    private:
        bool m_Initialized = false;
        ::Engine::RHI::IRHIPipelineState* m_PSOs[static_cast<uint8>(PostProcessEffect::COUNT)] = {};
    };

    struct BloomConfig { uint32_t downsampleLevels = 4; float filterRadius = 0.005f; bool useKawase = true; };
    enum class ToneMapMethod : uint8 { None, Reinhard, ACES, Unreal, Filmic };

    // ════════════════════════════════════════════════════════
    // 后处理独立执行函数（供 RenderGraph Pass 直接调用）
    // ════════════════════════════════════════════════════════

    /**
     * @brief 执行 Dual Kawase Bloom
     *
     * @param cmd          命令列表
     * @param extractPSO   亮度提取 PSO
     * @param downPSO      Kawase Downsample PSO
     * @param upPSO        Kawase Upsample PSO
     * @param compositePSO Bloom Composite PSO
     * @param mipCount     下采样层数（default=4）
     * @param intensity    泛光强度
     */
    void ExecuteBloom(::Engine::RHI::IRHICommandList& cmd,
                      ::Engine::RHI::IRHIPipelineState* extractPSO,
                      ::Engine::RHI::IRHIPipelineState* downPSO,
                      ::Engine::RHI::IRHIPipelineState* upPSO,
                      ::Engine::RHI::IRHIPipelineState* compositePSO,
                      uint32 mipCount = 4,
                      float intensity = 1.0f);

    /**
     * @brief 执行 ACES Filmic ToneMapping
     *
     * @param cmd      命令列表
     * @param pso      ToneMapping PSO
     * @param exposure 曝光值（默认 1.0）
     * @param gamma    Gamma 校正值（默认 2.2）
     */
    void ExecuteToneMapping(::Engine::RHI::IRHICommandList& cmd,
                             ::Engine::RHI::IRHIPipelineState* pso,
                             float exposure = 1.0f,
                             float gamma = 2.2f);

    /**
     * @brief 执行 TAA Resolve
     *
     * @param cmd          命令列表
     * @param pso          TAA PSO
     * @param blendFactor  历史帧混合比例（默认 0.95 = 95% 历史）
     */
    void ExecuteTAAResolve(::Engine::RHI::IRHICommandList& cmd,
                            ::Engine::RHI::IRHIPipelineState* pso,
                            float blendFactor = 0.95f);

    // ════════════════════════════════════════════════════════
    // Halton 序列工具函数
    // ════════════════════════════════════════════════════════

    /**
     * @brief 生成 Halton 序列值（用于 TAA 亚像素抖动）
     *
     * @param index 序列索引
     * @param base  基数（典型值 2 和 3）
     * @return [0, 1) 范围内的值
     */
    float HaltonSequence(uint32 index, uint32 base);

    /**
     * @brief 获取 TAA 抖动偏移量（像素空间）
     *
     * @param frameIndex  帧索引
     * @param width       视口宽度
     * @param height      视口高度
     * @param outJitterX  输出 X 抖动偏移
     * @param outJitterY  输出 Y 抖动偏移
     */
    void GetJitterOffset(uint32 frameIndex, uint32 width, uint32 height,
                          float& outJitterX, float& outJitterY);

}} // Engine::Rendering
