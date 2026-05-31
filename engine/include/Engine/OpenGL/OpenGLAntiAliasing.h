#pragma once

#include "Engine/Core/RHI/AntiAliasingTypes.h"
#include "Engine/Core/Log.h"
#include <glad/gl.h>

namespace Engine {

// ============================================================
// OpenGL 抗锯齿管理器
//
// 支持 MSAA / SSAA / CSAA / MLAA 等多种抗锯齿技术。
// 负责创建/管理 FBO、渲染缓冲、着色器，并执行 resolve / post-process。
// ============================================================
class OpenGLAntiAliasing {
public:
    OpenGLAntiAliasing(GladGLContext& gl);
    ~OpenGLAntiAliasing();

    // ── 不可拷贝 ──
    OpenGLAntiAliasing(const OpenGLAntiAliasing&) = delete;
    OpenGLAntiAliasing& operator=(const OpenGLAntiAliasing&) = delete;

    // ── 配置 ──
    void SetConfig(const AntiAliasingConfig& config);
    const AntiAliasingConfig& GetConfig() const { return m_Config; }
    AntiAliasingMode GetMode() const { return m_Config.mode; }
    bool IsActive() const { return m_Config.mode != AntiAliasingMode::None; }

    // ── 能力查询 ──
    const AntiAliasingCaps& GetCaps() const { return m_Caps; }

    // ── 生命周期 ──
    void OnResize(int width, int height);

    // ── 渲染集成 ──
    /** 在场景渲染前调用：绑定 AA 帧缓冲 */
    void BindForRender();

    /** 在 SwapBuffers 前调用：执行 resolve / post-process */
    void ResolveToDefault();

    // ── 调试辅助 ──
    const char* GetModeName() const;

private:
    // ── 初始化 ──
    void QueryCaps();
    void CreateMLAAShaders();

    // ── FBO 创建 ──
    void CreateMSAAFBO(int width, int height);
    void CreateSSAAFBO(int width, int height);
    void CreateCSAAFBO(int width, int height);
    void CreateMLAAFBO(int width, int height);
    void DestroyFBOs();
    void DestroyMLAAResources();

    // ── Resolve ──
    void ResolveMSAA();
    void ResolveSSAA();
    void ResolveCSAA();
    void ResolveMLAA();

    // ── 全屏四边形 ──
    void InitFullscreenQuad();
    void RenderFullscreenQuad();

    // ── 工具 ──
    GLuint CreateShader(GLenum type, const std::string& source);
    GLuint CreateProgram(const std::string& vertexSrc, const std::string& fragmentSrc);
    void CheckGLError(const char* stage);

    // ── 成员 ──
    GladGLContext& m_GL;

    AntiAliasingConfig m_Config;
    AntiAliasingCaps   m_Caps;
    Logger             m_Log;

    // ── 窗口尺寸 ──
    int m_Width  = 0;
    int m_Height = 0;

    // ── MSAA / CSAA 共用资源 ──
    GLuint m_MultiSampleFBO    = 0;   // 多重采样 FBO
    GLuint m_MultiSampleColorRB = 0;  // 颜色 Renderbuffer
    GLuint m_MultiSampleDepthRB = 0;  // 深度 Renderbuffer

    // ── SSAA 资源 ──
    GLuint m_SSAAFBO     = 0;   // SSAA FBO
    GLuint m_SSAAColorTex = 0;  // 高分辨率颜色纹理
    GLuint m_SSAADepthRB  = 0;  // 深度 Renderbuffer

    // ── 中间 resolve 纹理（MSAA/CSAA → 单采样纹理，用于 MLAA 或显示） ──
    GLuint m_ResolveFBO  = 0;   // resolve FBO
    GLuint m_ResolveTex  = 0;   // resolve 颜色纹理

    // ── MLAA 资源 ──
    GLuint m_MLAAFBO        = 0;   // MLAA 临时 FBO
    GLuint m_MLAATex        = 0;   // MLAA 输入纹理
    GLuint m_MLAADepthRB    = 0;   // MLAA 深度缓冲
    GLuint m_MLAATempFBO    = 0;   // MLAA 临时 FBO（边缘检测输出）
    GLuint m_MLAATempTex    = 0;   // MLAA 临时纹理（边缘图）

    // ── MLAA 着色器 ──
    GLuint m_MLAAEdgeDetectProgram = 0;  // 边缘检测着色器
    GLuint m_MLAABlendProgram     = 0;   // 混合着色器

    // ── SSAA 着色器（双线性下采样） ──
    GLuint m_SSAADownsampleProgram = 0;

    // ── 通用全屏四边形 ──
    GLuint m_FullscreenVAO  = 0;
    GLuint m_FullscreenVBO  = 0;
    GLuint m_FullscreenVS   = 0;    // 全屏顶点着色器

    // ── 默认 FBO 缓存（创建 AA FBO 前保存，用于恢复） ──
    GLint m_DefaultFBO = 0;

    // ── 脏标记 ──
    bool m_NeedsResize = false;
    bool m_NeedsRecreate = false;   // 模式/配置已变，需在下一帧 BindForRender 时重建
    bool m_ResourcesCreated = false;
    bool m_MLAAEdgeProgramValid = false;
    bool m_MLAABlendProgramValid = false;
    bool m_SSAAProgramValid = false;
};

} // namespace Engine
