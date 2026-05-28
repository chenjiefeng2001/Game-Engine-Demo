#pragma once

/**
 * @file ContentBrowserPanel.h
 * @brief 内容浏览器面板 — 浏览已加载的运行时资源
 *
 * 显示所有已加载的 Texture、Shader、AudioClip 等资源，
 * 支持按类型筛选、点击选中、拖拽到检视器。
 */

#include "Engine/Types.h"
#include <string>
#include <functional>

namespace Engine {

    class ResourceManager;

    class ContentBrowserPanel {
    public:
        ContentBrowserPanel() = default;
        ~ContentBrowserPanel() = default;

        ContentBrowserPanel(const ContentBrowserPanel&) = delete;
        ContentBrowserPanel& operator=(const ContentBrowserPanel&) = delete;

        // ── 配置 ──
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

        // ── 渲染 ──
        /** 在 OnImGui() 中调用，绘制内容浏览器 */
        void OnImGui();

    private:
        void DrawTextureList();
        void DrawShaderList();

        bool m_Visible = false;
        bool m_ShowTextures = true;
        bool m_ShowShaders  = true;

        // 当前选中的资源路径
        std::string m_SelectedPath;
    };

} // namespace Engine
