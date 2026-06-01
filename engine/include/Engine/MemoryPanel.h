#pragma once

/**
 * @file MemoryPanel.h
 * @brief 游戏内置内存统计面板 — 实时堆使用量图表
 *
 * 功能：
 *   - 当前/峰值/帧增量的动态刷新
 *   - 最近 120 帧使用量历史柱状图
 *   - 可 Dock 到编辑器底部
 *   - 通过控制台 memory_panel 命令切换
 */

#include "Engine/Types.h"

namespace Engine {

    class MemoryPanel {
    public:
        MemoryPanel() = default;
        ~MemoryPanel() = default;

        MemoryPanel(const MemoryPanel&) = delete;
        MemoryPanel& operator=(const MemoryPanel&) = delete;

        // ── 渲染 ──
        void OnImGui();

        // ── 显示控制 ──
        void SetVisible(bool v) { m_Visible = v; }
        bool IsVisible() const   { return m_Visible; }
        void Toggle()            { m_Visible = !m_Visible; }

    private:
        bool m_Visible = false;
        bool m_ShowHistory = true;
        int  m_SelectedRecordIndex = -1;   // -1 = 未选中

        void DrawOverview();
        void DrawDetails();
        void DrawRecent();
        void DrawStackTraceDetail();       // 选中记录的堆栈详情
    };

} // namespace Engine
