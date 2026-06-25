#include "Engine/Core/RHI/GBuffer.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include "Engine/Core/Log.h"
#include <glad/gl.h>

#define GL() (*static_cast<GladGLContext*>(m_GLContext))

namespace Engine {
Logger sLog("GBuffer");

class OpenGLGBuffer : public GBuffer {
public:
    OpenGLGBuffer(IRenderContext& context)
        : m_GLContext(&static_cast<OpenGLContext&>(context).GetGL()) {}
    ~OpenGLGBuffer() override { Shutdown(); }

    bool Initialize(const GBufferConfig& cfg) override;
    void Shutdown() override;
    bool IsValid() const override { return m_FBO != 0; }

    void BindForGeometryPass() override;
    void Unbind() override;
    void BindTexturesForLighting() const override;
    void Clear() override;

    uint32 GetFBO() const override { return m_FBO; }
    uint32 GetPositionTex() const override { return m_PositionTex; }
    uint32 GetNormalTex()   const override { return m_NormalTex; }
    uint32 GetAlbedoTex()   const override { return m_AlbedoTex; }
    uint32 GetPBRTex()      const override { return m_PBRTex; }
    uint32 GetDepthTex()    const override { return m_DepthTex; }
    uint32 GetWidth()  const override { return m_Config.width; }
    uint32 GetHeight() const override { return m_Config.height; }

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

bool OpenGLGBuffer::Initialize(const GBufferConfig& cfg) {
    Shutdown();
    m_Config = cfg;
    auto w = cfg.width, h = cfg.height;

    GL().GenFramebuffers(1, &m_FBO);
    GL().BindFramebuffer(GL_FRAMEBUFFER, m_FBO);

    // Position (RGB16F)
    GL().GenTextures(1, &m_PositionTex);
    GL().BindTexture(GL_TEXTURE_2D, m_PositionTex);
    GL().TexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, nullptr);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GL().FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_PositionTex, 0);

    // Normal (RGBA16F)
    GL().GenTextures(1, &m_NormalTex);
    GL().BindTexture(GL_TEXTURE_2D, m_NormalTex);
    GL().TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GL().FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_NormalTex, 0);

    // Albedo (RGBA8)
    GL().GenTextures(1, &m_AlbedoTex);
    GL().BindTexture(GL_TEXTURE_2D, m_AlbedoTex);
    GL().TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GL().FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, m_AlbedoTex, 0);

    // PBR (RGBA8)
    GL().GenTextures(1, &m_PBRTex);
    GL().BindTexture(GL_TEXTURE_2D, m_PBRTex);
    GL().TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GL().FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, m_PBRTex, 0);

    // Depth
    GL().GenTextures(1, &m_DepthTex);
    GL().BindTexture(GL_TEXTURE_2D, m_DepthTex);
    GL().TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GL().FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_DepthTex, 0);

    // Draw buffers
    GLenum attachments[4] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3
    };
    GL().DrawBuffers(4, attachments);

    GLenum status = GL().CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        sLog.Error("G-Buffer FBO not complete. Status={:04X}", status);
        Shutdown();
        return false;
    }

    GL().BindFramebuffer(GL_FRAMEBUFFER, 0);
    sLog.Info("G-Buffer created ({}x{})", w, h);
    return true;
}

void OpenGLGBuffer::Shutdown() {
    if (m_PositionTex) { GL().DeleteTextures(1, &m_PositionTex); m_PositionTex = 0; }
    if (m_NormalTex)   { GL().DeleteTextures(1, &m_NormalTex);   m_NormalTex = 0; }
    if (m_AlbedoTex)   { GL().DeleteTextures(1, &m_AlbedoTex);   m_AlbedoTex = 0; }
    if (m_PBRTex)      { GL().DeleteTextures(1, &m_PBRTex);      m_PBRTex = 0; }
    if (m_DepthTex)    { GL().DeleteTextures(1, &m_DepthTex);    m_DepthTex = 0; }
    if (m_FBO)         { GL().DeleteFramebuffers(1, &m_FBO);     m_FBO = 0; }
}

void OpenGLGBuffer::BindForGeometryPass() {
    GL().BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    GL().Viewport(0, 0, m_Config.width, m_Config.height);
}

void OpenGLGBuffer::Unbind() {
    GL().BindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLGBuffer::BindTexturesForLighting() const {
    GL().ActiveTexture(GL_TEXTURE0);
    GL().BindTexture(GL_TEXTURE_2D, m_PositionTex);
    GL().ActiveTexture(GL_TEXTURE1);
    GL().BindTexture(GL_TEXTURE_2D, m_NormalTex);
    GL().ActiveTexture(GL_TEXTURE2);
    GL().BindTexture(GL_TEXTURE_2D, m_AlbedoTex);
    GL().ActiveTexture(GL_TEXTURE3);
    GL().BindTexture(GL_TEXTURE_2D, m_PBRTex);
    GL().ActiveTexture(GL_TEXTURE4);
    GL().BindTexture(GL_TEXTURE_2D, m_DepthTex);
    GL().ActiveTexture(GL_TEXTURE0);
}

void OpenGLGBuffer::Clear() {
    GL().BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    GL().ClearColor(0, 0, 0, 0);
    GL().Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

// ── Factory method ──
std::unique_ptr<GBuffer> CreateGBuffer(IRenderContext& context) {
    return std::make_unique<OpenGLGBuffer>(context);
}

} // namespace Engine