#pragma once
/**
 * @file ShadowMapper.h
 * @brief 阴影映射器 — 纯抽象接口
 *
 * 不包含任何后端特定类型或实现。
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <cstdint>

namespace Engine {

    class IRenderContext;

    struct ShadowMapperConfig {
        uint32 shadowMapSize = 1024;
        uint32 numCascades   = 4;
        float  cascadeSplitLambda = 0.95f;
        float  shadowBias    = 0.005f;
    };

    class Shader;

    class ShadowMapper {
    public:
        ShadowMapper() = default;
        virtual ~ShadowMapper() = default;

        ShadowMapper(const ShadowMapper&) = delete;
        ShadowMapper& operator=(const ShadowMapper&) = delete;

        virtual bool Initialize(const ShadowMapperConfig& cfg) = 0;
        virtual void Shutdown() = 0;
        virtual bool IsValid() const = 0;

        virtual void BindForShadowPass() = 0;
        virtual void Unbind() = 0;
        virtual void BindShadowTexture(int slot) = 0;
        virtual void Clear() = 0;

        virtual uint32 GetDepthTexture() const = 0;
        virtual uint32 GetShadowMapSize() const = 0;
        virtual uint32 GetShadowTexture() const { return GetDepthTexture(); }

        // ── 阴影渲染所需额外方法 ──
        virtual void EndShadowPass() = 0;
        virtual void BindShadowMap(int slot) = 0;
        virtual void SetShaderUniforms(Shader* shader) = 0;
        virtual const Mat4& GetLightVP() const = 0;

        // ── 配置访问 ──
        virtual const ShadowMapperConfig& GetConfig() const = 0;

        // ── 光源控制 ──
        virtual void SetLightPosition(const Vec3& pos) = 0;
        virtual void SetLightDirection(const Vec3& dir) = 0;
        virtual void SetLightDistance(float dist) = 0;

        // ── 参数 ──
        virtual void SetBias(float bias) = 0;
    };

} // namespace Engine