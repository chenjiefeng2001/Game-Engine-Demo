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

}} // Engine::Rendering