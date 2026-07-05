#include "Engine/Rendering/GameHUD.h"
#include "Engine/Rendering/TextRenderer.h"
#include "Engine/Core/RHI/IPrimitiveBatch.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include "Engine/Core/Log.h"
#include <cstdio>

namespace Engine {
Logger s_HUDLog("GameHUD");

GameHUD::GameHUD() {}
GameHUD::~GameHUD() {}

bool GameHUD::Initialize(IGraphicsFactory& factory, IRenderContext& context) {
    m_Text = std::make_unique<TextRenderer>();
    if (!m_Text->Initialize(context)) {
        s_HUDLog.Error("Failed to initialize TextRenderer");
        return false;
    }

    m_Shader = factory.CreateShader("assets/shaders/ui.vert", "assets/shaders/ui.frag");
    m_Text->SetShader(m_Shader.get());

    // 获取屏幕尺寸
    auto& gl = static_cast<OpenGLContext&>(context).GetGL();
    GLint vp[4];
    gl.GetIntegerv(GL_VIEWPORT, vp);
    m_ScreenWidth = (float)vp[2];
    m_ScreenHeight = (float)vp[3];

    s_HUDLog.Info("HUD initialized");
    return true;
}

void GameHUD::UpdateScreenSize(float w, float h) {
    m_ScreenWidth = w;
    m_ScreenHeight = h;
    if (m_Text) m_Text->UpdateScreenSize(w, h);
    s_HUDLog.Info("HUD screen size updated to {}x{}", w, h);
}

void GameHUD::Render(IPrimitiveBatch& batch) {
    if (!m_Visible || !m_Text || !m_Shader) return;

    // 设置正交投影矩阵，将像素坐标 (0,width)×(height,0) 映射到 NDC (-1~1)
    // ui.vert: gl_Position = u_Projection * vec4(aPos.xy, 0.0, 1.0)
    Mat4 orthoProj;
    float w = std::max(m_ScreenWidth, 1.0f);
    float h = std::max(m_ScreenHeight, 1.0f);
    orthoProj.data[0] =  2.0f / w;
    orthoProj.data[5] = -2.0f / h;
    orthoProj.data[10] = -1.0f;
    orthoProj.data[12] = -1.0f;
    orthoProj.data[13] =  1.0f;

    m_Shader->Bind();
    m_Shader->SetMat4("u_Projection", orthoProj.Data());
    m_Shader->SetInt("u_Mode", 1);         // 字体渲染模式 (R→alpha)
    m_Shader->SetInt("u_Texture", 0);
    m_Shader->SetInt("u_EnableAlpha", 1);

    // 绑定字体纹理到纹理单元 0
    // 调用者（ComplexSceneTest）应确保纹理已绑定

    char buf[256];

    // ── 左上角信息 ──
    float y = 10.0f;
    const float lineH = 22.0f;

    TextConfig cfg;
    cfg.fontSize = 20.0f;
    cfg.color = Vec4(0.2f, 0.8f, 1.0f, 0.9f);
    cfg.space = CoordSpace::Screen;

    if (m_Flags & HUDTextFlags::FPS) {
        std::snprintf(buf, sizeof(buf), "FPS: %.1f  (%.1f ms)", m_FPS, m_FrameTime);
        m_Text->DrawText(batch, buf, 12.0f, y, cfg);
        y += lineH;
    }

    cfg.color = Vec4(0.6f, 1.0f, 0.6f, 0.9f);
    if (m_Flags & HUDTextFlags::Objects) {
        std::snprintf(buf, sizeof(buf), "Objects: %u total, %u visible",
                       m_ObjectCount, m_VisibleCount);
        m_Text->DrawText(batch, buf, 12.0f, y, cfg);
        y += lineH;
    }

    cfg.color = Vec4(1.0f, 0.8f, 0.4f, 0.9f);
    if (m_Flags & HUDTextFlags::DrawCalls) {
        std::snprintf(buf, sizeof(buf), "Draw Calls: %u", m_DrawCalls);
        m_Text->DrawText(batch, buf, 12.0f, y, cfg);
        y += lineH;
    }

    cfg.color = Vec4(0.8f, 0.6f, 1.0f, 0.9f);
    if (m_Flags & HUDTextFlags::Memory) {
        const char* units[] = {"B", "KB", "MB", "GB"};
        int ui = 0;
        float mem = (float)m_MemoryBytes;
        while (mem > 1024 && ui < 3) { mem /= 1024; ui++; }
        std::snprintf(buf, sizeof(buf), "Memory: %.1f %s", mem, units[ui]);
        m_Text->DrawText(batch, buf, 12.0f, y, cfg);
        y += lineH;
    }

    cfg.color = Vec4(0.4f, 1.0f, 0.8f, 0.9f);
    if (m_Flags & HUDTextFlags::Position) {
        std::snprintf(buf, sizeof(buf), "Pos: (%.1f, %.1f, %.1f)",
                       m_Position.x, m_Position.y, m_Position.z);
        m_Text->DrawText(batch, buf, 12.0f, y, cfg);
        y += lineH;
    }

    // ── 自定义文本（底部居中） ──
    if (!m_CustomText.empty()) {
        cfg.color = Vec4(1, 1, 1, 0.7f);
        cfg.align = TextAlign::Center;
        cfg.space = CoordSpace::Normalized;
        m_Text->DrawText(batch, m_CustomText, 0, -0.85f, cfg);
    }
}

} // namespace Engine
