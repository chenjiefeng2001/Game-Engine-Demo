#define _CRT_SECURE_NO_WARNINGS

#include "Engine/ConsolePanel.h"
#include "Engine/ConsoleLog.h"
#include "Engine/ConsoleCommandRegistry.h"

#include <imgui.h>
#include <cstring>
#include <cctype>
#include <algorithm>

namespace Engine {

// ============================================================
// OnImGui — 主渲染入口
// ============================================================

void ConsolePanel::OnImGui()
{
    if (!m_Visible)
        return;

    // 如果输入框处于激活状态，阻止键盘事件传递到游戏
    if (m_InputActive)
        ImGui::SetNextWindowFocus();

    ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_FirstUseEver);

    // 重要：允许键盘输入捕获
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar; // 我们自己在内部处理
    ImGui::Begin("Console", &m_Visible, flags);

    // ── 绘制日志区域 ──
    DrawLogSection();

    ImGui::Separator();

    // ── 绘制命令输入行 ──
    DrawInputLine();

    ImGui::End();

    // 如果控制台关闭，重置输入状态
    if (!m_Visible)
        m_InputActive = false;
}

// ============================================================
// 颜色代码渲染（Quake 风格：^0~^9）
// ============================================================

/// 颜色代码 → ABGR 映射表
static const uint32 kColorCodeTable[10] = {
    0xFFFFFFFF,   // ^0  White
    0xFF3333FF,   // ^1  Red
    0xFF88CC00,   // ^2  Green
    0xFF00CCFF,   // ^3  Yellow (actually ABGR orange)
    0xFFFF8800,   // ^4  Blue
    0xFFFFCC00,   // ^5  Cyan
    0xFFAA88FF,   // ^6  Magenta/pink
    0xFFCCCCCC,   // ^7  Light gray
    0xFF2288FF,   // ^8  Orange
    0xFF888888,   // ^9  Dark gray
};

/// 解析并渲染带 ^0~^9 颜色代码的文本
/// 支持 ^^ 转义为字面 ^
static void RenderColorCodedText(const char* text, ImVec4 defaultColor = ImVec4(0.90f, 0.90f, 0.90f, 1.00f))
{
    uint32 currentColor = 0xFFE6E6E6; // 默认浅灰 (ABGR)
    ImVec4 curColorVec = defaultColor;

    const char* p = text;
    const char* segmentStart = p;

    while (*p)
    {
        if (*p == '^' && *(p + 1) != '\0')
        {
            // ── 先 flush 上一段 ──
            if (p > segmentStart)
            {
                ImGui::TextColored(curColorVec, "%.*s", static_cast<int>(p - segmentStart), segmentStart);
                ImGui::SameLine(0, 0);
            }

            char code = *(p + 1);
            if (code == '^')
            {
                // ^^ → 字面 ^，继续累积
                p += 2;
                segmentStart = p;
                continue;
            }
            else if (code >= '0' && code <= '9')
            {
                int idx = code - '0';
                currentColor = kColorCodeTable[idx];
                // ABGR → ImVec4
                curColorVec = ImVec4(
                    ((currentColor >> 0)  & 0xFF) / 255.0f,   // R
                    ((currentColor >> 8)  & 0xFF) / 255.0f,   // G
                    ((currentColor >> 16) & 0xFF) / 255.0f,   // B
                    ((currentColor >> 24) & 0xFF) / 255.0f    // A
                );
                p += 2;
                segmentStart = p;
                continue;
            }
            // 否则 '^' 后跟非数字 → 作为普通字符
        }
        ++p;
    }

    // ── flush 剩余文本 ──
    if (p > segmentStart)
    {
        ImGui::TextColored(curColorVec, "%.*s", static_cast<int>(p - segmentStart), segmentStart);
    }
}

// ============================================================
// DrawLogSection — 日志显示区域
// ============================================================

void ConsolePanel::DrawLogSection()
{
    // ── 工具栏 ──
    {
        if (ImGui::Button("Clear"))
        {
            ConsoleLog::Instance().Clear();
            m_ScrollToBottom = false;
        }

        ImGui::SameLine();

        if (ImGui::Checkbox("Auto-scroll", &m_AutoScroll))
        {
            if (m_AutoScroll)
                m_ScrollToBottom = true;
        }

        ImGui::SameLine();

        // ── 复制按钮 ──
        if (ImGui::Button("Copy", ImVec2(50, 0))) {
            // 收集所有可见日志行到剪贴板
            std::string allText;
            auto& log = ConsoleLog::Instance();
            uint32 count = log.GetCount();
            uint32 start = log.GetStartIndex();
            uint32 cap   = log.GetCapacity();

            for (uint32 i = 0; i < count; ++i) {
                uint32 idx = (start + i) % cap;
                const auto& entry = log.GetBuffer()[idx];
                if (!allText.empty()) allText += "\n";
                allText += entry.message;
            }
            ImGui::SetClipboardText(allText.c_str());
        }

        ImGui::SameLine();

        ImGui::SetNextItemWidth(150);
        ImGui::InputTextWithHint("##Filter", "Filter...", m_FilterBuf, sizeof(m_FilterBuf));
    }

    ImGui::Separator();

    // ── 日志显示区域（自动伸缩填充剩余空间） ──
    ImGui::BeginChild("LogScroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4.0f), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    if (ImGui::BeginTable("LogTable", 3,
                          ImGuiTableFlags_NoPadInnerX |
                          ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("Time",  ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);

        auto& log = ConsoleLog::Instance();
        const LogEntry* buffer = log.GetBuffer();
        uint32 count = log.GetCount();
        uint32 start = log.GetStartIndex();
        uint32 cap   = log.GetCapacity();

        bool hasFilter = m_FilterBuf[0] != '\0';

        for (uint32 i = 0; i < count; ++i)
        {
            uint32 idx = (start + i) % cap;
            const auto& entry = buffer[idx];

            // 过滤
            if (hasFilter)
            {
                const char* msg = entry.message.c_str();
                bool found = false;
                for (const char* p = msg; *p; ++p)
                {
                    const char* f = m_FilterBuf;
                    const char* s = p;
                    while (*f && *s && std::tolower(*f) == std::tolower(*s))
                    {
                        ++f; ++s;
                    }
                    if (!*f)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                    continue;
            }

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            int minutes = static_cast<int>(entry.timestamp) / 60;
            int seconds = static_cast<int>(entry.timestamp) % 60;
            ImGui::Text("%02d:%02d", minutes, seconds);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(
                ImColor(LogLevelColor(entry.level)),
                "%s", LogLevelName(entry.level)
            );

            ImGui::TableSetColumnIndex(2);

            // ── 使用颜色代码解析渲染（支持 ^0~^9 标记） ──
            //     命令级消息用绿色基底，其他用默认灰
            if (entry.level == LogLevel::Command) {
                // 命令回显：绿色基底 + 内部颜色代码
                RenderColorCodedText(entry.message.c_str(),
                    ImVec4(0.55f, 0.80f, 0.20f, 1.00f)); // 亮绿
            } else if (entry.level == LogLevel::Warn) {
                RenderColorCodedText(entry.message.c_str(),
                    ImVec4(1.00f, 0.80f, 0.00f, 1.00f)); // 黄
            } else if (entry.level == LogLevel::Error) {
                RenderColorCodedText(entry.message.c_str(),
                    ImVec4(1.00f, 0.20f, 0.20f, 1.00f)); // 红
            } else {
                // Info + 其他：默认浅灰，支持颜色代码
                RenderColorCodedText(entry.message.c_str());
            }
        }

        if (m_AutoScroll && m_ScrollToBottom)
        {
            ImGui::SetScrollHereY(1.0f);
            m_ScrollToBottom = false;
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
}

// ============================================================
// DrawInputLine — 命令输入行
// ============================================================

void ConsolePanel::DrawInputLine()
{
    // 提示文字
    ImGui::Text("> ");
    ImGui::SameLine();

    // ── 手动处理 ↑/↓/Tab 键（在 InputText 之前） ──
    if (m_InputActive) {
        // ↑ 历史上一条
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
            HistoryPrev();
            if (m_HistoryPos >= 0 &&
                m_HistoryPos < static_cast<int>(m_CommandHistory.size())) {
                std::strncpy(m_InputBuf, m_CommandHistory[m_HistoryPos].c_str(), sizeof(m_InputBuf) - 1);
                m_InputBuf[sizeof(m_InputBuf) - 1] = '\0';
            }
        }

        // ↓ 历史下一条
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
            HistoryNext();
            if (m_HistoryPos >= 0 &&
                m_HistoryPos < static_cast<int>(m_CommandHistory.size())) {
                std::strncpy(m_InputBuf, m_CommandHistory[m_HistoryPos].c_str(), sizeof(m_InputBuf) - 1);
                m_InputBuf[sizeof(m_InputBuf) - 1] = '\0';
            } else {
                m_InputBuf[0] = '\0';
            }
        }

        // Tab 补全
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
            AutoComplete();
            if (!m_CompletionCandidates.empty()) {
                const auto& completion = m_CompletionCandidates[m_CompletionIndex];
                // 只替换命令部分（第一个词）
                std::string current(m_InputBuf);
                size_t spacePos = current.find(' ');
                std::string rest = (spacePos != std::string::npos) ? current.substr(spacePos) : "";
                std::string newText = completion + rest;
                std::strncpy(m_InputBuf, newText.c_str(), sizeof(m_InputBuf) - 1);
                m_InputBuf[sizeof(m_InputBuf) - 1] = '\0';
            }
        }
    }

    // ── ImGui 输入框 ──
    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;

    ImGui::PushItemWidth(-1.0f);

    bool executeCmd = false;

    // 如果刚打开控制台，自动聚焦输入框
    if (m_InputActive && !ImGui::IsAnyItemActive()) {
        ImGui::SetKeyboardFocusHere();
    }

    if (ImGui::InputText("##CmdInput", m_InputBuf, sizeof(m_InputBuf), inputFlags))
    {
        // Enter 被按下
        executeCmd = true;
    }

    // 跟踪焦点状态
    if (ImGui::IsItemActivated()) {
        m_InputActive = true;
    }
    if (ImGui::IsItemDeactivated()) {
        m_InputActive = false;
    }

    ImGui::PopItemWidth();

    // 执行命令
    if (executeCmd && m_InputBuf[0] != '\0') {
        SubmitCommand();
    }

    // 如果点击控制台窗口内部，自动聚焦到输入框
    if (ImGui::IsWindowHovered() && !m_InputActive &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        ImGui::SetKeyboardFocusHere();
        m_InputActive = true;
    }
}

// ============================================================
// SubmitCommand — 执行当前输入
// ============================================================

void ConsolePanel::SubmitCommand()
{
    std::string cmd(m_InputBuf);

    // 去除首尾空白
    size_t start = cmd.find_first_not_of(" \t");
    size_t end = cmd.find_last_not_of(" \t");
    if (start == std::string::npos) {
        m_InputBuf[0] = '\0';
        return;
    }
    cmd = cmd.substr(start, end - start + 1);

    if (cmd.empty()) {
        m_InputBuf[0] = '\0';
        return;
    }

    // ── 加入历史 ──
    if (m_CommandHistory.empty() || m_CommandHistory.back() != cmd) {
        m_CommandHistory.push_back(cmd);
        if (static_cast<int>(m_CommandHistory.size()) > kMaxHistory) {
            m_CommandHistory.erase(m_CommandHistory.begin());
        }
    }
    m_HistoryPos = -1;

    // ── 回显到控制台（命令级别，绿色高亮） ──
    std::string echoLine = "> " + cmd;
    ConsoleLog::Instance().Log(LogLevel::Command, echoLine);

    // ── 执行 ──
    std::string output;
    ConsoleCommandRegistry::Instance().Execute(cmd, output);

    // ── 输出结果 ──
    if (!output.empty()) {
        ConsoleLog::Instance().Log(LogLevel::Info, output);
    }

    // ── 清空输入 ──
    m_InputBuf[0] = '\0';

    // 触发自动滚动
    if (m_AutoScroll)
        m_ScrollToBottom = true;
}

// ============================================================
// AutoComplete — Tab 补全
// ============================================================

void ConsolePanel::AutoComplete()
{
    std::string current(m_InputBuf);

    // 只补全第一个词（命令名）
    size_t spacePos = current.find(' ');
    if (spacePos != std::string::npos) {
        // 已经有参数了，不在命令名上补全
        return;
    }

    // 如果输入为空，不补全
    if (current.empty())
        return;

    // 如果正在补全中且输入没有变化，循环下一个候选项
    if (m_Completing && current == m_CompletionBase) {
        m_CompletionIndex = (m_CompletionIndex + 1) % static_cast<int>(m_CompletionCandidates.size());
        return;
    }

    // 开始新的补全
    m_CompletionBase = current;
    m_CompletionCandidates = ConsoleCommandRegistry::Instance().GetCompletions(current);

    if (!m_CompletionCandidates.empty()) {
        m_Completing = true;
        m_CompletionIndex = 0;

        // 如果只有一个候选项，直接补全（回调中处理）
        // 如果有多个，在控制台显示候选项
        if (m_CompletionCandidates.size() > 1) {
            std::string msg = "  ";
            for (size_t i = 0; i < m_CompletionCandidates.size(); ++i) {
                if (i > 0) msg += "  ";
                msg += m_CompletionCandidates[i];
            }
            ConsoleLog::Instance().Log(LogLevel::Info, msg);
        }
    } else {
        m_Completing = false;
    }
}

// ============================================================
// History navigation
// ============================================================

void ConsolePanel::HistoryPrev()
{
    if (m_CommandHistory.empty()) {
        m_HistoryPos = -1;
        return;
    }

    if (m_HistoryPos < 0) {
        // 从最新一条开始
        m_HistoryPos = static_cast<int>(m_CommandHistory.size()) - 1;
    } else if (m_HistoryPos > 0) {
        --m_HistoryPos;
    }
    // 否则已经在最旧一条，不动
}

void ConsolePanel::HistoryNext()
{
    if (m_HistoryPos < 0)
        return;

    if (m_HistoryPos < static_cast<int>(m_CommandHistory.size()) - 1) {
        ++m_HistoryPos;
    } else {
        // 回到新输入
        m_HistoryPos = -1;
    }
}

// ============================================================
// 便捷方法
// ============================================================

void ConsolePanel::ToggleVisibility()
{
    m_Visible = !m_Visible;
    if (m_Visible) {
        FocusInput();
    } else {
        m_InputActive = false;
    }
}

void ConsolePanel::SetVisible(bool visible)
{
    m_Visible = visible;
    if (m_Visible) {
        FocusInput();
    } else {
        m_InputActive = false;
    }
}

void ConsolePanel::FocusInput()
{
    m_InputActive = true;
    ImGui::SetKeyboardFocusHere(-1);  // -1 表示聚焦到上一个未聚焦的控件
    // 注意：实际的 ImGui::SetKeyboardFocusHere 需要在 InputText 前调用
    // 我们在 OnImGui 的 DrawInputLine 之前无法直接调用，
    // 所以这里只设置标志，DrawInputLine 中会检查
}

void ConsolePanel::ExecuteCommand(const std::string& cmd)
{
    // 复制到输入缓冲区并执行
    std::strncpy(m_InputBuf, cmd.c_str(), sizeof(m_InputBuf) - 1);
    m_InputBuf[sizeof(m_InputBuf) - 1] = '\0';
    SubmitCommand();
}

void ConsolePanel::Print(const std::string& text)
{
    ConsoleLog::Instance().Log(LogLevel::Info, text);
    if (m_AutoScroll)
        m_ScrollToBottom = true;
}

} // namespace Engine
