#include "Engine/Core/RHI/GBuffer.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include "Engine/Core/Log.h"
#include <glad/gl.h>

#define GL() (*static_cast<GladGLContext*>(m_GLContext))

namespace Engine {
Logger sLog("GBuffer");

GBuffer::GBuffer(IRenderContext& context)
    : m_GLContext(&static_cast<OpenGLContext&>(context).GetGL()) {}
GBuffer::~GBuffer() { Shutdown(); }

bool GBuffer::Initialize(const GBufferConfig& cfg) {
    Shutdown();
    m_Config = cfg;
    auto w = cfg.width, h = cfg.height;

    GL().GenTextures(1, &m_PositionTex);
    GL().BindTexture(GL_TEXTURE_2D, m_PositionTex);
    GL().TexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, nullptr);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GL().GenTextures(1, &m_NormalTex);
    GL().BindTexture(GL_TEXTURE_2D, m_NormalTex);
    GL().TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GL().GenTextures(1, &m_AlbedoTex);
    GL().BindTexture(GL_TEXTURE_2D, m_AlbedoTex);
    GL().TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GL().GenTextures(1, &m_PBRTex);
    GL().BindTexture(GL_TEXTURE_2D, m_PBRTex);
    GL().TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GL().GenTextures(1, &m_DepthTex);
    GL().BindTexture(GL_TEXTURE_2D, m_DepthTex);
    GL().TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GL().GenFramebuffers(1, &m_FBO);
    GL().BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    GL().FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_PositionTex, 0);
    GL().FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_NormalTex, 0);
    GL().FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, m_AlbedoTex, 0);
    GL().FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, m_PBRTex, 0);
    GL().FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_DepthTex, 0);

    GLenum att[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
    GL().DrawBuffers(4, att);

    auto s = GL().CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (s != GL_FRAMEBUFFER_COMPLETE) { sLog.Error("FBO:0x{:X}", s); Shutdown(); return false; }

    GL().BindFramebuffer(GL_FRAMEBUFFER, 0);
    sLog.Info("GBuffer {}x{}", w, h);
    return true;
}

void GBuffer::Shutdown() {
    if (m_FBO) GL().DeleteFramebuffers(1, &m_FBO);
    if (m_PositionTex) GL().DeleteTextures(1, &m_PositionTex);
    if (m_NormalTex) GL().DeleteTextures(1, &m_NormalTex);
    if (m_AlbedoTex) GL().DeleteTextures(1, &m_AlbedoTex);
    if (m_PBRTex) GL().DeleteTextures(1, &m_PBRTex);
    if (m_DepthTex) GL().DeleteTextures(1, &m_DepthTex);
    m_FBO = m_PositionTex = m_NormalTex = m_AlbedoTex = m_PBRTex = m_DepthTex = 0;
}

void GBuffer::BindForGeometryPass() {
    if (!m_FBO) return;
    GL().BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    GL().Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GBuffer::BindTexturesForLighting() const {
    GL().ActiveTexture(GL_TEXTURE0); GL().BindTexture(GL_TEXTURE_2D, m_PositionTex);
    GL().ActiveTexture(GL_TEXTURE1); GL().BindTexture(GL_TEXTURE_2D, m_NormalTex);
    GL().ActiveTexture(GL_TEXTURE2); GL().BindTexture(GL_TEXTURE_2D, m_AlbedoTex);
    GL().ActiveTexture(GL_TEXTURE3); GL().BindTexture(GL_TEXTURE_2D, m_PBRTex);
}

void GBuffer::Clear() {
    if (!m_FBO) return;
    GL().BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    GL().Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GBuffer::Unbind() { GL().BindFramebuffer(GL_FRAMEBUFFER, 0); }

#undef GL
} // namespace Engine
