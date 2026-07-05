#pragma once

/**
 * @file ViewModePanel.h
 * @brief 多维度渲染视图模式面板 — 按分组选择工业级可视化调试模式
 *
 * ┌─────────────────────────────────────────────┐
 * │  View Mode  [▼ Lit (Normal)]                │
 * ├─────────────────────────────────────────────┤
 * │  ▸ Basic                                    │
 * │    ○ Lit (Normal)    ○ Unlit                │
 * │    ○ Wireframe       ○ Shaded Wireframe     │
 * │  ▸ G-Buffer                                 │
 * │    ○ Normal   ○ Depth   ○ Albedo            │
 * │    ○ Rough    ○ Metal   ○ Specular          │
 * │    ○ AO       ○ Velocity                    │
 * │  ▸ Lighting                                 │
 * │    ○ Lighting Only  ○ LM Density            │
 * │    ○ Overdraw Heatmap                       │
 * │  ▸ Diagnostic                               │
 * │    ○ Shader Complex  ○ LOD Coloration       │
 * │    ○ Collision Debug ○ UV Overlap           │
 * │    ○ Vertex Density                         │
 * ├─────────────────────────────────────────────┤
 * │  [Apply]  [Reset to Normal]                 │
 * └─────────────────────────────────────────────┘
 */

#include "Engine/Types.h"
#include "Engine/Core/ViewMode.h"
#include <functional>

namespace Engine {

    class ViewModePanel {
    public:
        ViewModePanel() = default;
        ~ViewModePanel() = default;

        ViewModePanel(const ViewModePanel&) = delete;
        ViewModePanel& operator=(const ViewModePanel&) = delete;

        // ── 面板渲染 ──
        void OnImGui();

        // ── 可见性 ──
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

        // ── 当前模式 ──
        void SetCurrentMode(ViewMode mode) { m_CurrentMode = mode; }
        ViewMode GetCurrentMode() const { return m_CurrentMode; }

        // ── 模式变更回调 ──
        using ModeChangeCallback = std::function<void(ViewMode newMode)>;
        void SetModeChangeCallback(ModeChangeCallback cb) { m_ModeChangeCallback = std::move(cb); }

    private:
        // ── 绘制分组内部 ──
        void DrawModeGroup(ViewModeGroup group, const char* label,
                           ViewMode* modes, int modeCount, ViewMode firstMode);

        // ── 模式按钮辅助 ──
        void DrawModeButton(ViewMode mode, const char* label);

        bool m_Visible = true;
        ViewMode m_CurrentMode = ViewMode::Normal;
        ModeChangeCallback m_ModeChangeCallback;
    };

} // namespace Engine