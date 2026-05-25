#pragma once

/**
 * @file ConsolePanel.h
 * @brief 控制台日志窗口 — 在 ImGui 中以 BeginChild 显示 ConsoleLog 的环形缓冲区内容
 *
 * 功能：
 *   - 自动滚动到底部（可开关）
 *   - 按级别着色（Info 灰 / Warn 黄 / Error 红）
 *   - 清除按钮
 *   - 搜索/过滤功能
 */

#include "Engine/Types.h"
#include <functional>
#include <string>

namespace Engine {

    class ConsolePanel {
    public:
        ConsolePanel() = default;
        ~ConsolePanel() = default;

        ConsolePanel(const ConsolePanel&) = delete;
        ConsolePanel& operator=(const ConsolePanel&) = delete;

        // ── 渲染 ──
        /** 在 OnImGui() 中调用，绘制控制台窗口 */
        void OnImGui();

        // ── 显示控制 ──
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

        // ── 配置 ──
        void SetAutoScroll(bool autoScroll) { m_AutoScroll = autoScroll; }
        bool GetAutoScroll() const { return m_AutoScroll; }
        void ToggleAutoScroll() { m_AutoScroll = !m_AutoScroll; }

    private:
        bool m_Visible = false;     // 默认隐藏，用户按 ~ 或点击菜单打开
        bool m_AutoScroll = true;
        bool m_ScrollToBottom = false;

        // 搜索过滤
        char m_FilterBuf[128] = {};
    };

} // namespace Engine
