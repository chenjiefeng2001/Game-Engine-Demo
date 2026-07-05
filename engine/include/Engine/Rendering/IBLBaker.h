#pragma once

/**
 * @file IBLBaker.h
 * @brief IBL (Image-Based Lighting) 环境光预计算器
 *
 * 核心算法：
 *   1. Irradiance Map: 对环境贴图做半球卷积，生成 32×32 立方体贴图（用于 PBR 漫反射）
 *   2. Prefiltered Environment Map: 对多个 roughness 级别做 specular lobe 卷积
 *   3. BRDF LUT: 2D 查找表，预积分 Cook-Torrance BRDF 的镜面响应
 */

#include "Engine/Types.h"
#include <memory>
#include <string>

namespace Engine {

    class IGraphicsFactory;
    class Shader;
    class Texture;

    namespace Rendering {

        struct IBLData {
            std::shared_ptr<Texture> irradianceMap;
            std::shared_ptr<Texture> prefilteredMap;
            std::shared_ptr<Texture> brdfLUT;
            std::string sourcePath;

            bool IsValid() const noexcept {
                return irradianceMap != nullptr &&
                       prefilteredMap != nullptr &&
                       brdfLUT != nullptr;
            }
        };

        struct IBLBakerConfig {
            uint32_t irradianceMapSize   = 32;
            uint32_t prefilteredMapSize  = 256;
            uint32_t prefilterMipLevels  = 5;
            uint32_t brdfLUTSize         = 512;
            uint32_t sampleCount         = 512;
        };

        class IBLBaker {
        public:
            IBLBaker() = default;
            ~IBLBaker() = default;

            bool Initialize(IGraphicsFactory& factory,
                            const std::string& hdrPath,
                            const IBLBakerConfig& config = {});

            bool ComputeAll();
            bool ComputeIrradianceMap();
            bool ComputePrefilteredMap();
            bool ComputeBRDFLUT();

            const IBLData& GetResult() const noexcept { return m_Result; }
            const IBLBakerConfig& GetConfig() const noexcept { return m_Config; }
            bool IsInitialized() const noexcept { return m_EnvMap != nullptr; }
            bool IsComputed() const noexcept { return m_Result.IsValid(); }

        private:
            IBLBakerConfig m_Config;
            IBLData m_Result;
            IGraphicsFactory* m_Factory = nullptr;
            std::shared_ptr<Texture> m_EnvMap;
            std::shared_ptr<Shader> m_IrradianceVS;
            std::shared_ptr<Shader> m_IrradianceFS;
            std::shared_ptr<Shader> m_PrefilterVS;
            std::shared_ptr<Shader> m_PrefilterFS;
            std::shared_ptr<Shader> m_BRDFVS;
            std::shared_ptr<Shader> m_BRDFFS;
        };

    } // namespace Rendering
} // namespace Engine