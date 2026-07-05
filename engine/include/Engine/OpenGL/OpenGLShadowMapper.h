#pragma once

#include "Engine/Core/RHI/ShadowMapper.h"
#include "Engine/Rendering/LightTypes.h"
#include <glad/gl.h>

struct GladGLContext;

namespace Engine {

    /**
     * @brief OpenGL 阴影映射器实现
     *
     * 使用级联阴影贴图 (CSM) 算法，管理深度 FBO + 纹理，
     * 复用 CSMShadowMapper 的数学逻辑（本文件中内联实现）。
     */
    class OpenGLShadowMapper final : public ShadowMapper {
    public:
        explicit OpenGLShadowMapper(GladGLContext& gl);
        ~OpenGLShadowMapper() override;

        OpenGLShadowMapper(const OpenGLShadowMapper&) = delete;
        OpenGLShadowMapper& operator=(const OpenGLShadowMapper&) = delete;

        // ── ShadowMapper 接口实现 ──
        bool Initialize(const ShadowMapperConfig& cfg) override;
        void Shutdown() override;
        bool IsValid() const override;

        void BindForShadowPass() override;
        void Unbind() override;
        void BindShadowTexture(int slot) override;
        void Clear() override;

        uint32 GetDepthTexture() const override { return m_DepthTex; }
        uint32 GetShadowMapSize() const override { return m_Config.shadowMapSize; }

        void EndShadowPass() override {}
        void BindShadowMap(int slot) override { BindShadowTexture(slot); }
        void SetShaderUniforms(Shader* shader) override;
        const Mat4& GetLightVP() const override { return m_LightVP; }

        const ShadowMapperConfig& GetConfig() const override { return m_Config; }

        void SetLightPosition(const Vec3& pos) override;
        void SetLightDirection(const Vec3& dir) override;
        void SetLightDistance(float dist) override { m_LightDistance = dist; }
        void SetBias(float bias) override { m_Config.shadowBias = bias; }

    private:
        /** 重新计算级联分割矩阵 */
        void RecalculateCascades();

        GladGLContext& m_GL;

        ShadowMapperConfig m_Config;

        // ── GPU 资源 ──
        uint32 m_FBO      = 0;
        uint32 m_DepthTex = 0;

        // ── 光源参数 ──
        Vec3  m_LightDir  = {0.577f, -0.577f, 0.577f};
        Vec3  m_LightPos  = {20.0f, 30.0f, 20.0f};
        float m_LightDistance = 100.0f;

        // ── 缓存结果 ──
        Mat4 m_LightVP;       // 最近级联的 LightViewProj（用于简单单级联）
        Rendering::CSMUBOData m_CSMData;
    };

} // namespace Engine