#pragma once

#include "Engine/Core/RHI/GBuffer.h"
#include "Engine/Core/IRenderContext.h"

namespace Engine {

    class OpenGLGBuffer : public GBuffer {
    public:
        OpenGLGBuffer(IRenderContext& context);
        ~OpenGLGBuffer() override;

        bool Initialize(const GBufferConfig& cfg) override;
        void Shutdown() override;
        bool IsValid() const override;

        void BindForGeometryPass() override;
        void Unbind() override;
        void BindTexturesForLighting() const override;
        void Clear() override;

        uint32 GetFBO() const override;
        uint32 GetPositionTex() const override;
        uint32 GetNormalTex()   const override;
        uint32 GetAlbedoTex()   const override;
        uint32 GetPBRTex()      const override;
        uint32 GetDepthTex()    const override;
        uint32 GetWidth()  const override;
        uint32 GetHeight() const override;

    private:
        void* m_GLContext = nullptr;
        GBufferConfig m_Config;
        uint32 m_FBO       = 0;
        uint32 m_PositionTex = 0;
        uint32 m_NormalTex   = 0;
        uint32 m_AlbedoTex   = 0;
        uint32 m_PBRTex      = 0;
        uint32 m_DepthTex    = 0;
    };

// 工厂函数：在 OpenGLGBuffer.cpp 中实现
std::unique_ptr<GBuffer> CreateGBuffer(IRenderContext& context);

} // namespace Engine
