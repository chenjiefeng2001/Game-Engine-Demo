#include "Engine/OpenGL/OpenGLShadowMapper.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/Log.h"
#include <glad/gl.h>
#include <cmath>
#include <algorithm>

namespace Engine {
namespace {
    Logger s_Log("OpenGLShadowMapper");
}

OpenGLShadowMapper::OpenGLShadowMapper(GladGLContext& gl)
    : m_GL(gl) {
    // 使用 LightConstants 的默认值
    m_Config.shadowMapSize = Rendering::LightConstants::kDefaultShadowMapSize;
    m_Config.numCascades   = Rendering::LightConstants::kDefaultCascadeCount;
    m_Config.cascadeSplitLambda = Rendering::LightConstants::kDefaultSplitLambda;
    m_Config.shadowBias    = Rendering::LightConstants::kDefaultShadowBias;
}

OpenGLShadowMapper::~OpenGLShadowMapper() {
    Shutdown();
}

bool OpenGLShadowMapper::Initialize(const ShadowMapperConfig& cfg) {
    Shutdown();
    m_Config = cfg;

    uint32 size = m_Config.shadowMapSize;

    // ── 创建深度纹理 ──
    m_GL.GenTextures(1, &m_DepthTex);
    m_GL.BindTexture(GL_TEXTURE_2D, m_DepthTex);
    m_GL.TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
                    static_cast<GLint>(size), static_cast<GLint>(size),
                    0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    // 采样超出范围时返回白色（无阴影），而非黑色
    float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    m_GL.TexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    // PCF 软阴影：启用深度比较
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    // ── 创建 FBO（只附加深度） ──
    m_GL.GenFramebuffers(1, &m_FBO);
    m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    m_GL.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_TEXTURE_2D, m_DepthTex, 0);
    // 禁用颜色写入
    m_GL.DrawBuffer(GL_NONE);
    m_GL.ReadBuffer(GL_NONE);

    GLenum status = m_GL.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        s_Log.Error("Shadow FBO not complete. Status={:04X}", status);
        Shutdown();
        return false;
    }

    m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
    s_Log.Info("ShadowMapper initialized ({}x{}, {} cascades)",
               size, size, m_Config.numCascades);
    return true;
}

void OpenGLShadowMapper::Shutdown() {
    if (m_DepthTex) { m_GL.DeleteTextures(1, &m_DepthTex); m_DepthTex = 0; }
    if (m_FBO)      { m_GL.DeleteFramebuffers(1, &m_FBO);  m_FBO = 0; }
}

bool OpenGLShadowMapper::IsValid() const {
    return m_FBO != 0 && m_DepthTex != 0;
}

void OpenGLShadowMapper::BindForShadowPass() {
    if (!m_FBO) return;
    m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    m_GL.Viewport(0, 0,
                  static_cast<GLint>(m_Config.shadowMapSize),
                  static_cast<GLint>(m_Config.shadowMapSize));
    m_GL.Clear(GL_DEPTH_BUFFER_BIT);
    // 深度测试+背面剔除，减少阴影 acne
    m_GL.Enable(GL_DEPTH_TEST);
    m_GL.DepthMask(GL_TRUE);
    m_GL.DepthFunc(GL_LESS);
    m_GL.Enable(GL_CULL_FACE);
    m_GL.CullFace(GL_BACK);
    m_GL.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
}

void OpenGLShadowMapper::Unbind() {
    m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
    // 恢复颜色写入
    m_GL.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void OpenGLShadowMapper::BindShadowTexture(int slot) {
    m_GL.ActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + slot));
    m_GL.BindTexture(GL_TEXTURE_2D, m_DepthTex);
}

void OpenGLShadowMapper::Clear() {
    if (m_FBO) {
        m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        m_GL.Clear(GL_DEPTH_BUFFER_BIT);
        m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

void OpenGLShadowMapper::SetShaderUniforms(Shader* shader) {
    if (!shader) return;
    // 上传级联数据到着色器
    char buf[64];
    for (uint32 i = 0; i < m_CSMData.cascadeCount; ++i) {
        snprintf(buf, sizeof(buf), "u_CSMData.cascades[%u].lightViewProj", i);
        shader->SetMat4(buf, m_CSMData.cascades[i].lightViewProj);
        snprintf(buf, sizeof(buf), "u_CSMData.cascades[%u].splitDepth", i);
        shader->SetFloat(buf, m_CSMData.cascades[i].splitDepth);
    }
    shader->SetInt("u_CSMData.cascadeCount", static_cast<int>(m_CSMData.cascadeCount));
    shader->SetFloat("u_CSMData.shadowMapSize", static_cast<float>(m_Config.shadowMapSize));
    shader->SetFloat("u_CSMData.shadowBias", m_Config.shadowBias);
    // 绑定深度纹理到 slot 4（约定）
    BindShadowTexture(4);
    shader->SetInt("u_ShadowMap", 4);
}

void OpenGLShadowMapper::SetLightPosition(const Vec3& pos) {
    m_LightPos = pos;
    RecalculateCascades();
}

void OpenGLShadowMapper::SetLightDirection(const Vec3& dir) {
    m_LightDir = dir;
    RecalculateCascades();
}

// ── CSM 分割计算（引用自 CSMShadowMapper 的同名数学逻辑） ──
void OpenGLShadowMapper::RecalculateCascades() {
    // 获取相机参数 — 当前通过 m_LightVP 间接传入
    // 实际使用时应在每帧调用 SetShaderUniforms 前 UpdateCamera
    // 这里只做基础计算，实际数据由外部每帧传入再调用本函数

    // 计算光源空间变换矩阵
    Vec3 lightUp(0, 0, 1);
    Vec3 lightRight = Vec3::Cross(m_LightDir, lightUp);
    if (Vec3::Length(lightRight) < 0.001f) {
        lightUp = Vec3(0, 0, 1);
        lightRight = Vec3::Cross(m_LightDir, lightUp);
    }
    lightRight = Vec3::Normalize(lightRight);
    lightUp    = Vec3::Normalize(Vec3::Cross(lightRight, m_LightDir));

    Mat4 lightView;
    lightView.data[0]  = lightRight.x; lightView.data[4]  = lightRight.y;
    lightView.data[8]  = lightRight.z; lightView.data[12] = -Vec3::Dot(lightRight, m_LightPos);
    lightView.data[1]  = lightUp.x;    lightView.data[5]  = lightUp.y;
    lightView.data[9]  = lightUp.z;    lightView.data[13] = -Vec3::Dot(lightUp, m_LightPos);
    lightView.data[2]  = m_LightDir.x; lightView.data[6]  = m_LightDir.y;
    lightView.data[10] = m_LightDir.z; lightView.data[14] = -Vec3::Dot(m_LightDir, m_LightPos);
    lightView.data[3]  = 0;            lightView.data[7]  = 0;
    lightView.data[11] = 0;            lightView.data[15] = 1;

    // 简单正交投影（单级联模式）
    float halfSize = 25.0f;  // 场景半范围
    float nearZ = -50.0f;
    float farZ  = 50.0f;

    Mat4 orthoProj;
    orthoProj.Identity();
    orthoProj.data[0]  = 1.0f / halfSize;
    orthoProj.data[5]  = 1.0f / halfSize;
    orthoProj.data[10] = -2.0f / (farZ - nearZ);
    orthoProj.data[12] = 0.0f;
    orthoProj.data[13] = 0.0f;
    orthoProj.data[14] = -(farZ + nearZ) / (farZ - nearZ);

    Mat4Multiply(orthoProj, lightView, m_LightVP);

    // 填充 CSM 数据（单级联）
    m_CSMData.cascadeCount = 1;
    m_CSMData.cascades[0].lightViewProj = m_LightVP;
    m_CSMData.cascades[0].splitDepth = 200.0f;
    m_CSMData.shadowMapSize = static_cast<float>(m_Config.shadowMapSize);
    m_CSMData.shadowBias    = m_Config.shadowBias;
}

} // namespace Engine