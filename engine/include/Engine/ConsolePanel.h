#pragma once

/**
 * @file ConsolePanel.h
 * @brief 游戏内置控制台 — 支持日志显示 + 命令输入
 *
 * 功能：
 *   - 显示 ConsoleLog 环形缓冲区的内容（自动滚动 / 着色 / 过滤）
 *   - 底部命令输入行，支持历史记录（↑/↓）、Tab 自动补全
 *   - 与 ConsoleCommandRegistry 协作执行命令
 *   - 通过 `~` 键（或代码调用）切换可见性
 */

#include "Engine/Types.h"
#include <functional>
#include <string>
#include <vector>

namespace Engine {

    class ConsolePanel {
    public:
        ConsolePanel() = default;
        ~ConsolePanel() = default;

        ConsolePanel(const ConsolePanel&) = delete;
        ConsolePanel& operator=(const ConsolePanel&) = delete;

        // ── 渲染 ──
        /** 在 OnImGui() 中调用，绘制控制台窗口（日志 + 命令输入行） */
        void OnImGui();

        // ── 显示控制 ──
        void SetVisible(bool visible);
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility();

        // ── 输入控制 ──
        /** 是否正在捕获键盘输入（用于控制游戏输入阻塞） */
        bool IsCapturingInput() const { return m_Visible && m_InputActive; }

        /** 强制聚焦到输入框 */
        void FocusInput();

        /** 执行一条命令（程序化调用） */
        void ExecuteCommand(const std::string& cmd);

        /** 向控制台输出一行文本 */
        void Print(const std::string& text);

        // ── 配置 ──
        void SetAutoScroll(bool autoScroll) { m_AutoScroll = autoScroll; }
        bool GetAutoScroll() const { return m_AutoScroll; }
        void ToggleAutoScroll() { m_AutoScroll = !m_AutoScroll; }

        /** 获取最大保留的命令历史条数 */
        int GetMaxHistory() const { return kMaxHistory; }

    private:
        // ── 内部方法 ──
        void DrawLogSection();
        void DrawInputLine();

        /** 执行当前输入缓冲区的内容 */
        void SubmitCommand();

        /** Tab 补全当前输入 */
        void AutoComplete();

        /** 从历史中加载上一条/下一条命令 */
        void HistoryPrev();
        void HistoryNext();

        // ── 状态 ──
        bool m_Visible = false;
        bool m_AutoScroll = true;
        bool m_ScrollToBottom = false;

        // ── 搜索过滤 ──
        char m_FilterBuf[128] = {};

        // ── 命令输入 ──
        bool m_InputActive = false;     ///< 输入框是否获得焦点
        char m_InputBuf[256] = {};      ///< 当前输入文本
        int  m_HistoryPos = -1;         ///< 历史导航位置（-1 = 新输入）

        /** 命令历史记录（最新的在末尾） */
        std::vector<std::string> m_CommandHistory;
        static constexpr int kMaxHistory = 64;

        /** Tab 补全状态 */
        std::vector<std::string> m_CompletionCandidates;
        int m_CompletionIndex = 0;
        bool m_Completing = false;
        std::string m_CompletionBase;   ///< 触发补全时的前缀
    };

} // namespace Engine
