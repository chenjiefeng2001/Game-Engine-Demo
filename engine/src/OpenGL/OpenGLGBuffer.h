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
    };

} // namespace Engine