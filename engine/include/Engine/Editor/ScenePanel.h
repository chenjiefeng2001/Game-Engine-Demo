#pragma once

/**
 * @file ScenePanel.h
 * @brief 统一场景面板 — 以 Tab 形式合并 SceneHierarchy / SceneViewer / SceneManager
 *
 * 参考 Unity Editor 的布局策略：
 *   - Unity 中 "Hierarchy" 和 "Scene" 是独立窗口，可分别停靠
 *   - 但当编辑器布局紧凑时，可以用 Tab 将它们合并
 *
 * 本面板提供两种模式：
 *   1. 独立模式（默认）：三个面板作为独立 DockSpace 窗口存在
 *   2. 合并模式：在同一个 ImGui 窗口中用 TabBar 切换三个视图
 *
 * 无论哪种模式，三个面板内部的逻辑完全复用，通过 ScenePanelMediator 通信。
 */

#include "Engine/Types.h"
#include "Engine/Editor/ScenePanelMediator.h"
#include <memory>

namespace Engine {

    // 前向声明
    class SceneHierarchyPanel;
    class SceneViewerPanel;
    class SceneManagerPanel;

    // ============================================================
    // 场景面板合并模式
    // ============================================================
    enum class ScenePanelMode : uint8 {
        Split,   ///< 独立窗口（默认，每个面板可独立停靠在 DockSpace 不同位置）
        Tabbed,  ///< 合并为带 Tab 的单一窗口
    };

    // ============================================================
    // 统一场景面板
    // ============================================================
    class ScenePanel {
    public:
        ScenePanel();
        ~ScenePanel() = default;

        ScenePanel(const ScenePanel&) = delete;
        ScenePanel& operator=(const ScenePanel&) = delete;

        // ── 面板注册（由 EngineEditor 在 Init 时调用） ──
        void RegisterPanels(SceneHierarchyPanel* hierarchy,
                            SceneViewerPanel*    viewer,
                            SceneManagerPanel*   manager);

        // ── 每帧渲染 ──
        void OnImGui();

        // ── 模式切换 ──
        void SetMode(ScenePanelMode mode);
        ScenePanelMode GetMode() const { return m_Mode; }
        void ToggleMode() {
            m_Mode = (m_Mode == ScenePanelMode::Split)
                     ? ScenePanelMode::Tabbed
                     : ScenePanelMode::Split;
        }

        // ── 可见性 ──
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

        // ── 中介者访问 ──
        ScenePanelMediator& GetMediator() { return m_Mediator; }
        const ScenePanelMediator& GetMediator() const { return m_Mediator; }

    private:
        // ── Tabbed 模式下各 Tab 的渲染 ──
        void DrawHierarchyTab();
        void DrawViewerTab();
        void DrawManagerTab();

        ScenePanelMode m_Mode = ScenePanelMode::Tabbed;
        bool m_Visible = true;
        int  m_ActiveTab = 0;  // 0=Hierarchy, 1=Viewer, 2=Manager

        // 中介者（协调三个面板的通信）
        ScenePanelMediator m_Mediator;

        // 外部面板指针（不拥有所有权）
        SceneHierarchyPanel* m_Hierarchy = nullptr;
        SceneViewerPanel*    m_Viewer    = nullptr;
        SceneManagerPanel*   m_Manager   = nullptr;
    };

} // namespace Engine