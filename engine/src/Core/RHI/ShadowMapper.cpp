#include "Engine/Core/RHI/ShadowMapper.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include "Engine/Core/Log.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glad/gl.h>
#include <cstring>

namespace Engine {
Logger sL("ShadowMapper");

// 获取设备 GL 上下文的辅助宏
#define GL() (*static_cast<GladGLContext*>(m_GLContext))

ShadowMapper::ShadowMapper(IRenderContext& context)
    : m_GLContext(&static_cast<OpenGLContext&>(context).GetGL()) {}
ShadowMapper::~ShadowMapper() { Shutdown(); }

void ShadowMapper::SetConfig(const ShadowMapConfig& cfg) {
    m_Config = cfg;
    if (cfg.resolution != m_Resolution) {
        Shutdown();
        Initialize(cfg.resolution);
    }
}

bool ShadowMapper::Initialize(uint32 r) {
    Shutdown();
    m_Resolution = r;
    GL().GenTextures(1, &m_DepthTex);
    GL().BindTexture(GL_TEXTURE_2D, m_DepthTex);
    GL().TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, r, r, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float bc[] = {1, 1, 1, 1};
    GL().TexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, bc);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                        GL_COMPARE_REF_TO_TEXTURE);
    GL().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    GL().GenFramebuffers(1, &m_FBO);
    GL().BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    GL().FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, m_DepthTex, 0);
    GL().DrawBuffer(GL_NONE);
    GL().ReadBuffer(GL_NONE);
    auto s = GL().CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (s != GL_FRAMEBUFFER_COMPLETE) {
        sL.Error("Shadow FBO incomplete: 0x{:X}", s);
        Shutdown();
        return false;
    }
    GL().BindFramebuffer(GL_FRAMEBUFFER, 0);
    sL.Info("Shadow {}x{}", r, r);
    ComputeLightMatrix();
    return true;
}

void ShadowMapper::Shutdown() {
    if (m_FBO) GL().DeleteFramebuffers(1, &m_FBO);
    if (m_DepthTex) GL().DeleteTextures(1, &m_DepthTex);
    m_FBO = m_DepthTex = 0;
    m_Resolution = 0;
}

void ShadowMapper::BindForShadowPass() {
    if (!m_FBO) return;
    GL().BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    GL().Viewport(0, 0, m_Resolution, m_Resolution);
    GL().Clear(GL_DEPTH_BUFFER_BIT);
    GL().CullFace(GL_FRONT);
    ComputeLightMatrix();
}

void ShadowMapper::EndShadowPass() {
    GL().CullFace(GL_BACK);
    GL().BindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ShadowMapper::BindShadowMap(uint32 slot) const {
    if (!m_DepthTex) return;
    GL().ActiveTexture(GL_TEXTURE0 + slot);
    GL().BindTexture(GL_TEXTURE_2D, m_DepthTex);
}

void ShadowMapper::SetLightPosition(const Vec3& p) { m_LightPos = p; }
void ShadowMapper::SetLightDirection(const Vec3& d) {
    float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (l > 0.001f)
        m_LightDir = Vec3(d.x / l, d.y / l, d.z / l);
}

Mat4 ShadowMapper::ComputeLightMatrix() {
    glm::vec3 p(m_LightPos.x, m_LightPos.y, m_LightPos.z);
    glm::vec3 d(m_LightDir.x, m_LightDir.y, m_LightDir.z);
    glm::vec3 t = p + d * 10.0f;
    glm::vec3 up(0, 1, 0);
    if (std::abs(glm::dot(d, up)) > 0.99f)
        up = glm::vec3(0, 0, 1);
    float D = m_LightDistance;
    auto lp = glm::ortho(-D, D, -D, D, 0.1f, D * 2);
    auto lv = glm::lookAt(p, t, up);
    Mat4 r;
    auto vp = lp * lv;
    std::memcpy(r.Data(), &vp, sizeof(float) * 16);
    m_LightVP = r;
    return r;
}

void ShadowMapper::SetShaderUniforms(Shader* sh) const {
    if (!sh) return;
    sh->SetInt("u_ShadowMap", 3);
    sh->SetMat4("u_LightSpaceMatrix", m_LightVP.Data());
    sh->SetFloat("u_ShadowBias", m_Config.bias);
    sh->SetInt("u_ShadowEnabled", m_Config.enabled ? 1 : 0);
}

#undef GL
} // namespace Engine
