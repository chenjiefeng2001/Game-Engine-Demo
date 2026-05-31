#include "Engine/OpenGL/OpenGLAntiAliasing.h"

#include <string>
#include <vector>
#include <sstream>

namespace Engine {

// ============================================================
// 内联着色器源码
// ============================================================

// ── 全屏四边形顶点着色器 ──
static const char* kFullscreenQuadVS = R"(
#version 460 core
layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;
out vec2 v_TexCoord;
void main() {
    gl_Position = vec4(a_Position, 0.0, 1.0);
    v_TexCoord = a_TexCoord;
}
)";

// ── SSAA 下采样着色器 ──
static const char* kSSAADownsampleFS = R"(
#version 460 core
in vec2 v_TexCoord;
out vec4 FragColor;
uniform sampler2D u_Texture;
uniform vec2 u_TexelSize;
void main() {
    // 2x2 box 下采样 — 用硬件双线性插值等效
    FragColor = texture(u_Texture, v_TexCoord);
}
)";

// ── MLAA 边缘检测着色器（基于亮度梯度） ──
static const char* kMLAAEdgeDetectFS = R"(
#version 460 core
in vec2 v_TexCoord;
out vec4 FragColor;
uniform sampler2D u_Texture;
uniform vec2 u_TexelSize;
uniform float u_Threshold;

// 计算亮度
float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// 检测是否为边缘像素，输出边缘方向和距离
void main() {
    vec3 c = texture(u_Texture, v_TexCoord).rgb;
    float L = luma(c);

    // 计算水平/垂直梯度 (Sobel-like)
    float Lleft  = luma(texture(u_Texture, v_TexCoord - vec2(u_TexelSize.x, 0.0)).rgb);
    float Lright = luma(texture(u_Texture, v_TexCoord + vec2(u_TexelSize.x, 0.0)).rgb);
    float Lup    = luma(texture(u_Texture, v_TexCoord + vec2(0.0, u_TexelSize.y)).rgb);
    float Ldown  = luma(texture(u_Texture, v_TexCoord - vec2(0.0, u_TexelSize.y)).rgb);

    float gradX = abs(Lleft - Lright);
    float gradY = abs(Lup - Ldown);

    // 边缘强度
    float edgeStrength = max(gradX, gradY);
    float isEdge = step(u_Threshold, edgeStrength);

    // 输出: RG = 归一化梯度方向, B = 边缘标记, A = 边缘强度
    vec2 dir = normalize(vec2(gradX, gradY));
    FragColor = vec4(dir * isEdge, isEdge, edgeStrength);
}
)";

// ── MLAA 混合着色器（沿边缘方向模糊） ──
static const char* kMLAABlendFS = R"(
#version 460 core
in vec2 v_TexCoord;
out vec4 FragColor;
uniform sampler2D u_Texture;       // 原始场景颜色
uniform sampler2D u_EdgeTex;       // 边缘检测结果
uniform vec2 u_TexelSize;
uniform float u_MaxSteps;

void main() {
    vec4 edge = texture(u_EdgeTex, v_TexCoord);
    float isEdge = edge.b;

    if (isEdge < 0.5) {
        // 非边缘像素保持不变
        FragColor = texture(u_Texture, v_TexCoord);
        return;
    }

    // 边缘方向（从边缘图获取）
    vec2 edgeDir = edge.rg;

    // 沿边缘方向采样并混合
    vec4 color = texture(u_Texture, v_TexCoord);
    float totalWeight = 1.0;
    vec2 pos = v_TexCoord;

    // 沿边缘方向正向采样
    for (float i = 1.0; i <= u_MaxSteps; i += 1.0) {
        vec2 offset = edgeDir * u_TexelSize * i;
        vec4 sampleColor = texture(u_Texture, pos + offset);
        float w = 1.0 / (1.0 + i * 0.5);
        color += sampleColor * w;
        totalWeight += w;
    }

    // 沿边缘方向反向采样
    for (float i = 1.0; i <= u_MaxSteps; i += 1.0) {
        vec2 offset = edgeDir * u_TexelSize * i;
        vec4 sampleColor = texture(u_Texture, pos - offset);
        float w = 1.0 / (1.0 + i * 0.5);
        color += sampleColor * w;
        totalWeight += w;
    }

    FragColor = color / totalWeight;
}
)";

// ============================================================
// 构造 / 析构
// ============================================================

OpenGLAntiAliasing::OpenGLAntiAliasing(GladGLContext& gl)
    : m_GL(gl)
    , m_Log("OpenGLAntiAliasing")
{
    QueryCaps();
    m_Log.Info("OpenGLAntiAliasing created. Max samples: {}, CSAA: {}",
               m_Caps.maxSamples,
               m_Caps.hasCoverageExtension ? "supported" : "not supported");
}

OpenGLAntiAliasing::~OpenGLAntiAliasing()
{
    DestroyFBOs();
    DestroyMLAAResources();

    if (m_SSAADownsampleProgram) { m_GL.DeleteProgram(m_SSAADownsampleProgram); m_SSAADownsampleProgram = 0; }
    if (m_MLAAEdgeDetectProgram) { m_GL.DeleteProgram(m_MLAAEdgeDetectProgram); m_MLAAEdgeDetectProgram = 0; }
    if (m_MLAABlendProgram)     { m_GL.DeleteProgram(m_MLAABlendProgram);     m_MLAABlendProgram = 0; }
    if (m_FullscreenVS)         { m_GL.DeleteShader(m_FullscreenVS);          m_FullscreenVS = 0; }
    if (m_FullscreenVAO)        { m_GL.DeleteVertexArrays(1, &m_FullscreenVAO); m_FullscreenVAO = 0; }
    if (m_FullscreenVBO)        { m_GL.DeleteBuffers(1, &m_FullscreenVBO);     m_FullscreenVBO = 0; }
}

// ============================================================
// 能力查询
// ============================================================

void OpenGLAntiAliasing::QueryCaps()
{
    // 查询最大样本数
    m_GL.GetIntegerv(GL_MAX_SAMPLES, &m_Caps.maxSamples);

    // 检查覆盖采样扩展 — NamedRenderbufferStorageMultisampleCoverageEXT
    // 来自 GL_EXT_direct_state_access，支持 coverageSamples > colorSamples 即 CSAA
    m_Caps.hasCoverageExtension = (m_GL.NamedRenderbufferStorageMultisampleCoverageEXT != nullptr);
    m_Caps.supportsCSAA = m_Caps.hasCoverageExtension && m_Caps.maxSamples > 0;
}

// ============================================================
// 配置
// ============================================================

void OpenGLAntiAliasing::SetConfig(const AntiAliasingConfig& config)
{
    // 验证样本数
    AntiAliasingConfig validated = config;
    if (validated.sampleCount < 1)  validated.sampleCount = 1;
    if (validated.sampleCount > m_Caps.maxSamples && m_Caps.maxSamples > 0)
        validated.sampleCount = m_Caps.maxSamples;

    if (validated.coverageSamples < validated.sampleCount)
        validated.coverageSamples = validated.sampleCount * 2;

    bool modeChanged = (m_Config.mode != validated.mode) ||
                       (m_Config.sampleCount != validated.sampleCount) ||
                       (m_Config.coverageSamples != validated.coverageSamples) ||
                       (m_Config.superSamplingScale != validated.superSamplingScale);

    m_Config = validated;

    // ⚡ 延迟重建：仅标记脏位，不销毁 FBO。
    // 销毁操作在下一帧 BindForRender() 开头执行，避免在当前帧
    // ImGui 渲染中途删除仍绑定中的 FBO 导致驱动崩溃。
    if (modeChanged && m_Width > 0 && m_Height > 0) {
        m_NeedsRecreate = true;
        m_ResourcesCreated = false;
        m_NeedsResize = true;
    }

    if (m_Config.mode == AntiAliasingMode::MLAA && !m_MLAAEdgeProgramValid) {
        CreateMLAAShaders();
    }

    m_Log.Info("Anti-aliasing set to: {} (samples={})", GetModeName(), m_Config.sampleCount);
}

// ============================================================
//  resize
// ============================================================

void OpenGLAntiAliasing::OnResize(int width, int height)
{
    if (width == m_Width && height == m_Height && m_ResourcesCreated)
        return;

    m_Width  = width;
    m_Height = height;
    m_NeedsResize = true;
    m_ResourcesCreated = false;

    DestroyFBOs();
    DestroyMLAAResources();
}

// ============================================================
// 绑定渲染目标
// ============================================================

void OpenGLAntiAliasing::BindForRender()
{
    if (m_Width <= 0 || m_Height <= 0)
        return;

    if (!m_ResourcesCreated || m_NeedsResize) {
        QueryCaps();

        // ⚡ 延迟销毁：在重建前才销毁旧的 FBO，确保当前帧的渲染不会中途失效
        if (m_NeedsRecreate) {
            DestroyFBOs();
            DestroyMLAAResources();
            m_NeedsRecreate = false;
        }

        // 缓存默认 FBO
        m_GL.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_DefaultFBO);

        switch (m_Config.mode) {
            case AntiAliasingMode::MSAA: CreateMSAAFBO(m_Width, m_Height); break;
            case AntiAliasingMode::SSAA: CreateSSAAFBO(m_Width, m_Height); break;
            case AntiAliasingMode::CSAA: CreateCSAAFBO(m_Width, m_Height); break;
            case AntiAliasingMode::MLAA: CreateMLAAFBO(m_Width, m_Height); break;
            default: break;
        }

        m_ResourcesCreated = true;
        m_NeedsResize = false;
    }

    // 绑定对应的 FBO
    switch (m_Config.mode) {
        case AntiAliasingMode::MSAA:
        case AntiAliasingMode::CSAA:
            if (m_MultiSampleFBO) {
                m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_MultiSampleFBO);
            }
            break;
        case AntiAliasingMode::SSAA:
            if (m_SSAAFBO) {
                m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_SSAAFBO);
            }
            break;
        case AntiAliasingMode::MLAA:
            if (m_MLAAFBO) {
                m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_MLAAFBO);
            }
            break;
        default:
            m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_DefaultFBO);
            break;
    }
}

// ============================================================
// Resolve → 默认帧缓冲
// ============================================================

void OpenGLAntiAliasing::ResolveToDefault()
{
    if (!IsActive() || m_Width <= 0 || m_Height <= 0)
        return;
    // 资源尚未就绪（模式刚切换，下一帧才重建），跳过此次 resolve
    if (!m_ResourcesCreated)
        return;

    m_GL.BindFramebuffer(GL_DRAW_FRAMEBUFFER, m_DefaultFBO);

    switch (m_Config.mode) {
        case AntiAliasingMode::MSAA: ResolveMSAA(); break;
        case AntiAliasingMode::SSAA: ResolveSSAA(); break;
        case AntiAliasingMode::CSAA: ResolveCSAA(); break;
        case AntiAliasingMode::MLAA: ResolveMLAA(); break;
        default: break;
    }
}

// ============================================================
// 模式名称
// ============================================================

const char* OpenGLAntiAliasing::GetModeName() const
{
    switch (m_Config.mode) {
        case AntiAliasingMode::None: return "None";
        case AntiAliasingMode::MSAA: return "MSAA";
        case AntiAliasingMode::SSAA: return "SSAA";
        case AntiAliasingMode::CSAA: return "CSAA";
        case AntiAliasingMode::MLAA: return "MLAA";
        default: return "Unknown";
    }
}

// ============================================================
// FBO 销毁
// ============================================================

void OpenGLAntiAliasing::DestroyFBOs()
{
    if (m_MultiSampleFBO) {
        m_GL.DeleteFramebuffers(1, &m_MultiSampleFBO);
        m_MultiSampleFBO = 0;
    }
    if (m_MultiSampleColorRB) {
        m_GL.DeleteRenderbuffers(1, &m_MultiSampleColorRB);
        m_MultiSampleColorRB = 0;
    }
    if (m_MultiSampleDepthRB) {
        m_GL.DeleteRenderbuffers(1, &m_MultiSampleDepthRB);
        m_MultiSampleDepthRB = 0;
    }

    if (m_ResolveFBO) {
        m_GL.DeleteFramebuffers(1, &m_ResolveFBO);
        m_ResolveFBO = 0;
    }
    if (m_ResolveTex) {
        m_GL.DeleteTextures(1, &m_ResolveTex);
        m_ResolveTex = 0;
    }

    if (m_SSAAFBO) {
        m_GL.DeleteFramebuffers(1, &m_SSAAFBO);
        m_SSAAFBO = 0;
    }
    if (m_SSAAColorTex) {
        m_GL.DeleteTextures(1, &m_SSAAColorTex);
        m_SSAAColorTex = 0;
    }
    if (m_SSAADepthRB) {
        m_GL.DeleteRenderbuffers(1, &m_SSAADepthRB);
        m_SSAADepthRB = 0;
    }
}

void OpenGLAntiAliasing::DestroyMLAAResources()
{
    if (m_MLAAFBO) {
        m_GL.DeleteFramebuffers(1, &m_MLAAFBO);
        m_MLAAFBO = 0;
    }
    if (m_MLAATex) {
        m_GL.DeleteTextures(1, &m_MLAATex);
        m_MLAATex = 0;
    }
    if (m_MLAADepthRB) {
        m_GL.DeleteRenderbuffers(1, &m_MLAADepthRB);
        m_MLAADepthRB = 0;
    }
    if (m_MLAATempFBO) {
        m_GL.DeleteFramebuffers(1, &m_MLAATempFBO);
        m_MLAATempFBO = 0;
    }
    if (m_MLAATempTex) {
        m_GL.DeleteTextures(1, &m_MLAATempTex);
        m_MLAATempTex = 0;
    }
}

// ============================================================
// 创建 MSAA FBO
// ============================================================

void OpenGLAntiAliasing::CreateMSAAFBO(int width, int height)
{
    // 创建多重采样颜色 Renderbuffer
    m_GL.GenRenderbuffers(1, &m_MultiSampleColorRB);
    m_GL.BindRenderbuffer(GL_RENDERBUFFER, m_MultiSampleColorRB);
    m_GL.RenderbufferStorageMultisample(
        GL_RENDERBUFFER, m_Config.sampleCount, GL_RGBA8, width, height);

    // 创建多重采样深度 Renderbuffer
    m_GL.GenRenderbuffers(1, &m_MultiSampleDepthRB);
    m_GL.BindRenderbuffer(GL_RENDERBUFFER, m_MultiSampleDepthRB);
    m_GL.RenderbufferStorageMultisample(
        GL_RENDERBUFFER, m_Config.sampleCount, GL_DEPTH_COMPONENT24, width, height);

    // 创建 FBO 并附加
    m_GL.GenFramebuffers(1, &m_MultiSampleFBO);
    m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_MultiSampleFBO);
    m_GL.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, m_MultiSampleColorRB);
    m_GL.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, m_MultiSampleDepthRB);

    GLenum status = m_GL.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        m_Log.Error("MSAA FBO incomplete! Status: 0x{:X}", status);
    } else {
        m_Log.Info("MSAA FBO created ({}x{} samples={})", width, height, m_Config.sampleCount);
    }

    // 创建 resolve 纹理（用于最终输出到屏幕）
    m_GL.GenTextures(1, &m_ResolveTex);
    m_GL.BindTexture(GL_TEXTURE_2D, m_ResolveTex);
    m_GL.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_GL.GenFramebuffers(1, &m_ResolveFBO);
    m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_ResolveFBO);
    m_GL.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_ResolveTex, 0);
    status = m_GL.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        m_Log.Error("Resolve FBO incomplete! Status: 0x{:X}", status);
    }

    m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
    CheckGLError("CreateMSAAFBO");
}

// ============================================================
// 创建 SSAA FBO（高分辨率渲染 → 下采样）
// ============================================================

void OpenGLAntiAliasing::CreateSSAAFBO(int width, int height)
{
    float scale = m_Config.superSamplingScale;
    int ssWidth  = static_cast<int>(width  * scale);
    int ssHeight = static_cast<int>(height * scale);

    // 高分辨率颜色纹理
    m_GL.GenTextures(1, &m_SSAAColorTex);
    m_GL.BindTexture(GL_TEXTURE_2D, m_SSAAColorTex);
    m_GL.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ssWidth, ssHeight, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    // 深度 Renderbuffer
    m_GL.GenRenderbuffers(1, &m_SSAADepthRB);
    m_GL.BindRenderbuffer(GL_RENDERBUFFER, m_SSAADepthRB);
    m_GL.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, ssWidth, ssHeight);

    // 创建 FBO
    m_GL.GenFramebuffers(1, &m_SSAAFBO);
    m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_SSAAFBO);
    m_GL.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_SSAAColorTex, 0);
    m_GL.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, m_SSAADepthRB);

    GLenum status = m_GL.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        m_Log.Error("SSAA FBO incomplete! Status: 0x{:X}", status);
    } else {
        m_Log.Info("SSAA FBO created ({}x{} → {}x{} scale={})",
                   ssWidth, ssHeight, width, height, scale);
    }

    // 创建 SSAA 下采样着色器（若尚未创建）
    if (!m_SSAAProgramValid) {
        m_SSAADownsampleProgram = CreateProgram(kFullscreenQuadVS, kSSAADownsampleFS);
        if (m_SSAADownsampleProgram) {
            m_SSAAProgramValid = true;
        }
    }

    InitFullscreenQuad();
    m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
    CheckGLError("CreateSSAAFBO");
}

// ============================================================
// 创建 CSAA FBO（NVIDIA 覆盖采样）
// ============================================================

void OpenGLAntiAliasing::CreateCSAAFBO(int width, int height)
{
    if (!m_Caps.hasCoverageExtension) {
        m_Log.Warn("CSAA requested but coverage extension not available, falling back to MSAA");
        m_Config.mode = AntiAliasingMode::MSAA;
        CreateMSAAFBO(width, height);
        return;
    }

    // 使用 GL_NV_framebuffer_multisample_coverage 扩展
    // 创建覆盖采样颜色 Renderbuffer
    m_GL.GenRenderbuffers(1, &m_MultiSampleColorRB);
    m_GL.BindRenderbuffer(GL_RENDERBUFFER, m_MultiSampleColorRB);

    // 尝试使用 NamedRenderbufferStorageMultisampleCoverageEXT
    if (m_GL.NamedRenderbufferStorageMultisampleCoverageEXT) {
        m_GL.NamedRenderbufferStorageMultisampleCoverageEXT(
            m_MultiSampleColorRB,
            m_Config.coverageSamples,  // 覆盖样本数（更多）
            m_Config.sampleCount,       // 颜色/深度样本数
            GL_RGBA8, width, height);
    } else {
        // 回退到标准 MSAA
        m_GL.RenderbufferStorageMultisample(
            GL_RENDERBUFFER, m_Config.sampleCount, GL_RGBA8, width, height);
    }

    // 深度 Renderbuffer（同样使用覆盖采样）
    m_GL.GenRenderbuffers(1, &m_MultiSampleDepthRB);
    m_GL.BindRenderbuffer(GL_RENDERBUFFER, m_MultiSampleDepthRB);

    if (m_GL.NamedRenderbufferStorageMultisampleCoverageEXT) {
        m_GL.NamedRenderbufferStorageMultisampleCoverageEXT(
            m_MultiSampleDepthRB,
            m_Config.coverageSamples,
            m_Config.sampleCount,
            GL_DEPTH_COMPONENT24, width, height);
    } else {
        m_GL.RenderbufferStorageMultisample(
            GL_RENDERBUFFER, m_Config.sampleCount, GL_DEPTH_COMPONENT24, width, height);
    }

    // FBO
    m_GL.GenFramebuffers(1, &m_MultiSampleFBO);
    m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_MultiSampleFBO);
    m_GL.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, m_MultiSampleColorRB);
    m_GL.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, m_MultiSampleDepthRB);

    GLenum status = m_GL.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        m_Log.Error("CSAA FBO incomplete! Status: 0x{:X}, falling back to MSAA", status);
        // 清理刚创建的 CSAA 资源，然后退回 MSAA
        if (m_MultiSampleFBO) { m_GL.DeleteFramebuffers(1, &m_MultiSampleFBO); m_MultiSampleFBO = 0; }
        if (m_MultiSampleColorRB) { m_GL.DeleteRenderbuffers(1, &m_MultiSampleColorRB); m_MultiSampleColorRB = 0; }
        if (m_MultiSampleDepthRB) { m_GL.DeleteRenderbuffers(1, &m_MultiSampleDepthRB); m_MultiSampleDepthRB = 0; }
        m_Config.mode = AntiAliasingMode::MSAA;
        CreateMSAAFBO(width, height);
        return;
    }

    // 设置覆盖采样参数
    m_GL.SampleCoverage(1.0f, GL_FALSE);

    m_Log.Info("CSAA FBO created ({}x{} coverage={} samples={})",
               width, height, m_Config.coverageSamples, m_Config.sampleCount);

    // 创建 resolve 纹理（同 MSAA）
    m_GL.GenTextures(1, &m_ResolveTex);
    m_GL.BindTexture(GL_TEXTURE_2D, m_ResolveTex);
    m_GL.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_GL.GenFramebuffers(1, &m_ResolveFBO);
    m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_ResolveFBO);
    m_GL.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_ResolveTex, 0);

    m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
    CheckGLError("CreateCSAAFBO");
}

// ============================================================
// 创建 MLAA FBO（场景渲染 → 边缘检测 → 混合）
// ============================================================

void OpenGLAntiAliasing::CreateMLAAFBO(int width, int height)
{
    // 场景颜色纹理
    m_GL.GenTextures(1, &m_MLAATex);
    m_GL.BindTexture(GL_TEXTURE_2D, m_MLAATex);
    m_GL.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 深度 Renderbuffer
    m_GL.GenRenderbuffers(1, &m_MLAADepthRB);
    m_GL.BindRenderbuffer(GL_RENDERBUFFER, m_MLAADepthRB);
    m_GL.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

    // 主 MLAA FBO
    m_GL.GenFramebuffers(1, &m_MLAAFBO);
    m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_MLAAFBO);
    m_GL.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_MLAATex, 0);
    m_GL.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, m_MLAADepthRB);

    GLenum status = m_GL.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        m_Log.Error("MLAA FBO incomplete! Status: 0x{:X}", status);
    }

    // 临时边缘纹理（用于边缘检测输出）
    m_GL.GenTextures(1, &m_MLAATempTex);
    m_GL.BindTexture(GL_TEXTURE_2D, m_MLAATempTex);
    m_GL.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 临时边缘 FBO
    m_GL.GenFramebuffers(1, &m_MLAATempFBO);
    m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_MLAATempFBO);
    m_GL.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_MLAATempTex, 0);
    status = m_GL.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        m_Log.Error("MLAA temp FBO incomplete! Status: 0x{:X}", status);
    }

    m_Log.Info("MLAA FBO created ({}x{})", width, height);

    // 创建 MLAA 着色器
    CreateMLAAShaders();
    InitFullscreenQuad();

    m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
    CheckGLError("CreateMLAAFBO");
}

// ============================================================
// MLAA 着色器创建
// ============================================================

void OpenGLAntiAliasing::CreateMLAAShaders()
{
    if (!m_MLAAEdgeProgramValid) {
        m_MLAAEdgeDetectProgram = CreateProgram(kFullscreenQuadVS, kMLAAEdgeDetectFS);
        if (m_MLAAEdgeDetectProgram) {
            m_MLAAEdgeProgramValid = true;
        }
    }

    if (!m_MLAABlendProgramValid) {
        m_MLAABlendProgram = CreateProgram(kFullscreenQuadVS, kMLAABlendFS);
        if (m_MLAABlendProgram) {
            m_MLAABlendProgramValid = true;
        }
    }
}

// ============================================================
// 全屏四边形（用于 post-process 绘制）
// ============================================================

void OpenGLAntiAliasing::InitFullscreenQuad()
{
    if (m_FullscreenVAO)
        return;

    float quadVertices[] = {
        // pos         texCoord
        -1.0f,  1.0f,   0.0f, 1.0f,
        -1.0f, -1.0f,   0.0f, 0.0f,
         1.0f, -1.0f,   1.0f, 0.0f,
        -1.0f,  1.0f,   0.0f, 1.0f,
         1.0f, -1.0f,   1.0f, 0.0f,
         1.0f,  1.0f,   1.0f, 1.0f,
    };

    m_GL.GenVertexArrays(1, &m_FullscreenVAO);
    m_GL.GenBuffers(1, &m_FullscreenVBO);

    m_GL.BindVertexArray(m_FullscreenVAO);
    m_GL.BindBuffer(GL_ARRAY_BUFFER, m_FullscreenVBO);
    m_GL.BufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    m_GL.EnableVertexAttribArray(0);
    m_GL.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    m_GL.EnableVertexAttribArray(1);
    m_GL.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    m_GL.BindVertexArray(0);
}

void OpenGLAntiAliasing::RenderFullscreenQuad()
{
    m_GL.BindVertexArray(m_FullscreenVAO);
    m_GL.DrawArrays(GL_TRIANGLES, 0, 6);
    m_GL.BindVertexArray(0);
}

// ============================================================
// Resolve: MSAA → 默认帧缓冲
// ============================================================

void OpenGLAntiAliasing::ResolveMSAA()
{
    // 步骤 1: 将多重采样 FBO blit 到 resolve 纹理
    m_GL.BindFramebuffer(GL_READ_FRAMEBUFFER, m_MultiSampleFBO);
    m_GL.BindFramebuffer(GL_DRAW_FRAMEBUFFER, m_ResolveFBO);
    m_GL.BlitFramebuffer(0, 0, m_Width, m_Height,
                          0, 0, m_Width, m_Height,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // 步骤 2: 将 resolve 纹理绘制到默认帧缓冲
    m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_DefaultFBO);
    m_GL.Viewport(0, 0, m_Width, m_Height);

    // 使用简单的纹理绘制
    m_GL.BindTexture(GL_TEXTURE_2D, m_ResolveTex);

    // 用全屏四边形 + 简单纹理采样绘制
    // 使用固定管线效果：直接绑定纹理并绘制
    // 注意：这里使用预先编译好的简单传递着色器
    GLuint program = m_SSAADownsampleProgram;
    if (!program) {
        program = CreateProgram(kFullscreenQuadVS, kSSAADownsampleFS);
        m_SSAADownsampleProgram = program;
        m_SSAAProgramValid = (program != 0);
    }

    if (program) {
        m_GL.UseProgram(program);
        m_GL.Uniform1i(m_GL.GetUniformLocation(program, "u_Texture"), 0);
        m_GL.Uniform2f(m_GL.GetUniformLocation(program, "u_TexelSize"),
                        1.0f / m_Width, 1.0f / m_Height);
        RenderFullscreenQuad();
        m_GL.UseProgram(0);
    }

    CheckGLError("ResolveMSAA");
}

// ============================================================
// Resolve: SSAA → 默认帧缓冲（下采样）
// ============================================================

void OpenGLAntiAliasing::ResolveSSAA()
{
    m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_DefaultFBO);
    m_GL.Viewport(0, 0, m_Width, m_Height);
    m_GL.Clear(GL_COLOR_BUFFER_BIT);

    if (m_SSAADownsampleProgram) {
        m_GL.UseProgram(m_SSAADownsampleProgram);
        m_GL.Uniform1i(m_GL.GetUniformLocation(m_SSAADownsampleProgram, "u_Texture"), 0);

        float scale = m_Config.superSamplingScale;
        m_GL.Uniform2f(m_GL.GetUniformLocation(m_SSAADownsampleProgram, "u_TexelSize"),
                        1.0f / (m_Width * scale), 1.0f / (m_Height * scale));

        // 生成 mipmap 以获得更好的下采样质量
        m_GL.BindTexture(GL_TEXTURE_2D, m_SSAAColorTex);
        m_GL.GenerateMipmap(GL_TEXTURE_2D);
        m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

        RenderFullscreenQuad();
        m_GL.UseProgram(0);
    }

    CheckGLError("ResolveSSAA");
}

// ============================================================
// Resolve: CSAA → 默认帧缓冲
// ============================================================

void OpenGLAntiAliasing::ResolveCSAA()
{
    // CSAA resolve 过程与 MSAA 相同（blit 覆盖采样 FBO → resolve 纹理 → 显示）
    m_GL.BindFramebuffer(GL_READ_FRAMEBUFFER, m_MultiSampleFBO);
    m_GL.BindFramebuffer(GL_DRAW_FRAMEBUFFER, m_ResolveFBO);
    m_GL.BlitFramebuffer(0, 0, m_Width, m_Height,
                          0, 0, m_Width, m_Height,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);

    m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_DefaultFBO);
    m_GL.Viewport(0, 0, m_Width, m_Height);

    GLuint program = m_SSAADownsampleProgram;
    if (program) {
        m_GL.UseProgram(program);
        m_GL.Uniform1i(m_GL.GetUniformLocation(program, "u_Texture"), 0);
        m_GL.Uniform2f(m_GL.GetUniformLocation(program, "u_TexelSize"),
                        1.0f / m_Width, 1.0f / m_Height);
        m_GL.BindTexture(GL_TEXTURE_2D, m_ResolveTex);
        RenderFullscreenQuad();
        m_GL.UseProgram(0);
    }

    CheckGLError("ResolveCSAA");
}

// ============================================================
// Resolve: MLAA（边缘检测 → 混合 → 输出）
// ============================================================

void OpenGLAntiAliasing::ResolveMLAA()
{
    if (!m_MLAAEdgeProgramValid || !m_MLAABlendProgramValid) {
        // 着色器无效，直接 blit
        m_GL.BindFramebuffer(GL_READ_FRAMEBUFFER, m_MLAAFBO);
        m_GL.BindFramebuffer(GL_DRAW_FRAMEBUFFER, m_DefaultFBO);
        m_GL.BlitFramebuffer(0, 0, m_Width, m_Height,
                              0, 0, m_Width, m_Height,
                              GL_COLOR_BUFFER_BIT, GL_LINEAR);
        return;
    }

    // ── Pass 1: 边缘检测 ──
    m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_MLAATempFBO);
    m_GL.Viewport(0, 0, m_Width, m_Height);
    m_GL.Clear(GL_COLOR_BUFFER_BIT);

    m_GL.UseProgram(m_MLAAEdgeDetectProgram);
    m_GL.Uniform1i(m_GL.GetUniformLocation(m_MLAAEdgeDetectProgram, "u_Texture"), 0);
    m_GL.Uniform2f(m_GL.GetUniformLocation(m_MLAAEdgeDetectProgram, "u_TexelSize"),
                    1.0f / m_Width, 1.0f / m_Height);
    m_GL.Uniform1f(m_GL.GetUniformLocation(m_MLAAEdgeDetectProgram, "u_Threshold"),
                    m_Config.edgeThreshold);

    m_GL.ActiveTexture(GL_TEXTURE0);
    m_GL.BindTexture(GL_TEXTURE_2D, m_MLAATex);

    RenderFullscreenQuad();
    m_GL.UseProgram(0);

    // ── Pass 2: 混合 ──
    m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_DefaultFBO);
    m_GL.Viewport(0, 0, m_Width, m_Height);
    m_GL.Clear(GL_COLOR_BUFFER_BIT);

    m_GL.UseProgram(m_MLAABlendProgram);
    m_GL.Uniform1i(m_GL.GetUniformLocation(m_MLAABlendProgram, "u_Texture"), 0);
    m_GL.Uniform1i(m_GL.GetUniformLocation(m_MLAABlendProgram, "u_EdgeTex"), 1);
    m_GL.Uniform2f(m_GL.GetUniformLocation(m_MLAABlendProgram, "u_TexelSize"),
                    1.0f / m_Width, 1.0f / m_Height);
    m_GL.Uniform1f(m_GL.GetUniformLocation(m_MLAABlendProgram, "u_MaxSteps"),
                    m_Config.maxSearchSteps);

    m_GL.ActiveTexture(GL_TEXTURE0);
    m_GL.BindTexture(GL_TEXTURE_2D, m_MLAATex);
    m_GL.ActiveTexture(GL_TEXTURE1);
    m_GL.BindTexture(GL_TEXTURE_2D, m_MLAATempTex);

    RenderFullscreenQuad();
    m_GL.UseProgram(0);

    CheckGLError("ResolveMLAA");
}

// ============================================================
// 工具函数
// ============================================================

GLuint OpenGLAntiAliasing::CreateShader(GLenum type, const std::string& source)
{
    GLuint shader = m_GL.CreateShader(type);
    const char* src = source.c_str();
    m_GL.ShaderSource(shader, 1, &src, nullptr);
    m_GL.CompileShader(shader);

    GLint success = 0;
    m_GL.GetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        m_GL.GetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        m_Log.Error("Shader compile error ({}): {}",
                     type == GL_VERTEX_SHADER ? "VS" : "FS", infoLog);
        m_GL.DeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint OpenGLAntiAliasing::CreateProgram(const std::string& vertexSrc, const std::string& fragmentSrc)
{
    if (!m_FullscreenVS) {
        m_FullscreenVS = CreateShader(GL_VERTEX_SHADER, vertexSrc);
        if (!m_FullscreenVS) return 0;
    }

    GLuint fs = CreateShader(GL_FRAGMENT_SHADER, fragmentSrc);
    if (!fs) return 0;

    GLuint program = m_GL.CreateProgram();
    m_GL.AttachShader(program, m_FullscreenVS);
    m_GL.AttachShader(program, fs);
    m_GL.LinkProgram(program);

    GLint success = 0;
    m_GL.GetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        m_GL.GetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        m_Log.Error("Program link error: {}", infoLog);
        m_GL.DeleteProgram(program);
        m_GL.DeleteShader(fs);
        return 0;
    }

    m_GL.DeleteShader(fs);
    return program;
}

void OpenGLAntiAliasing::CheckGLError(const char* stage)
{
    GLenum err;
    while ((err = m_GL.GetError()) != GL_NO_ERROR) {
        m_Log.Warn("OpenGL error at '{}': 0x{:X}", stage, err);
    }
}

} // namespace Engine
