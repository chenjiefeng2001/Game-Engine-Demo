#pragma once

/**
 * @file ShadowMapper.h
 * @brief 阴影映射 — 从光源视角渲染深度图，主渲染时采样做阴影判定
 *
 * 支持：
 *   - 方向光/点光源阴影
 *   - PCF (Percentage Closer Filtering) 软阴影
 *   - 级联阴影映射 (CSM) 预留接口
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <memory>
#include <cstdint>

namespace Engine {

    class IRenderContext;
    class Shader;
    class MeshRenderer;

    struct ShadowMapConfig {
        uint32 resolution = 2048;
        float  bias       = 0.005f;
        float  pcfRadius  = 1.0f;     // PCF 采样半径
        int    pcfSamples = 4;         // PCF 采样次数
        bool   enabled    = true;
        // 级联参数（预留）
        int    cascadeCount = 1;
        float  cascadeSplit = 0.1f;
    };

    class ShadowMapper {
    public:
        ShadowMapper(IRenderContext& context);
        ~ShadowMapper();

        ShadowMapper(const ShadowMapper&) = delete;
        ShadowMapper& operator=(const ShadowMapper&) = delete;

        // ── 配置 ──
        void SetConfig(const ShadowMapConfig& cfg);
        const ShadowMapConfig& GetConfig() const { return m_Config; }

        // ── 初始化 ──
        bool Initialize(uint32 resolution);
        void Shutdown();

        bool IsValid() const { return m_FBO != 0; }

        // ── 渲染阴影深度 ──
        /** 绑定阴影深度 FBO */
        void BindForShadowPass();

        /** 解绑并准备采样 */
        void EndShadowPass();

        /** 获取光空间的 VP 矩阵 */
        const class Mat4& GetLightVP() const { return m_LightVP; }

        /** 设置光源位置（用于计算光空间矩阵） */
        void SetLightPosition(const Vec3& pos);
        void SetLightDirection(const Vec3& dir);

        // ── 采样 ──
        /** 绑定阴影贴图到纹理单元 */
        void BindShadowMap(uint32 slot = 3) const;

        /** 获取阴影贴图纹理 ID */
        uint32 GetShadowTexture() const { return m_DepthTex; }

        // ── Uniform 设置 ──
        void SetShaderUniforms(class Shader* shader) const;

    private:
        bool CreateShadowResources();
        void DestroyShadowResources();
        Mat4 ComputeLightMatrix();

        ShadowMapConfig m_Config;
        uint32 m_FBO      = 0;
        uint32 m_DepthTex = 0;
        uint32 m_Resolution = 0;

        Vec3 m_LightPos    = {5.0f, 10.0f, 5.0f};
        Vec3 m_LightDir    = {-0.3f, -0.8f, -0.5f};
        Mat4 m_LightVP;
        float m_LightDistance = 30.0f;
        void* m_GLContext = nullptr;
    };

} // namespace Engine
