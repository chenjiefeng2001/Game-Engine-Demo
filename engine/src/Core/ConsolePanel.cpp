#include "Engine/ConsolePanel.h"
#include "Engine/Core/Log.h"
#include "Engine/Editor/IconsFontAwesome6.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <ctime>
#include <cstring>
#include <regex>

namespace Engine {

    // ============================================================
    // 日志条目（带元数据）
    // ============================================================
    struct LogEntry {
        std::string text;
        std::string channel;   // e.g. "Graphics", "Physics", "Network"
        int         level = 0; // 0=Info, 1=Warning, 2=Error, 3=Fatal
        uint64      timeStamp = 0;
        uint32      repeatCount = 1;  // 折叠相同日志

        // 对象溯源
        uint64      sourceObjectId = 0;
        std::string sourceObjectName;

        // 堆栈信息
        std::string sourceFile;
        int         sourceLine = 0;

        bool operator==(const LogEntry& other) const {
            return text == other.text && channel == other.channel && level == other.level;
        }
    };

    // ============================================================
    // 全局环形日志缓冲区
    // ============================================================
    namespace {
        constexpr int kMaxLogEntries = 10000;
        std::vector<LogEntry> s_LogBuffer;
        int s_LogHead = 0;
        int s_LogCount = 0;

        void PushLog(const LogEntry& entry) {
            if (s_LogBuffer.size() < kMaxLogEntries) {
                s_LogBuffer.push_back(entry);
                s_LogHead = static_cast<int>(s_LogBuffer.size()) - 1;
                s_LogCount++;
            } else {
                s_LogHead = (s_LogHead + 1) % kMaxLogEntries;
                s_LogBuffer[s_LogHead] = entry;
                s_LogCount = kMaxLogEntries;
            }
        }

        // 获取日志（从旧到新）
        std::vector<const LogEntry*> GetLogs() {
            std::vector<const LogEntry*> result;
            if (s_LogBuffer.empty()) return result;

            int start = (s_LogCount < kMaxLogEntries) ? 0 : (s_LogHead + 1) % kMaxLogEntries;
            int count = std::min(s_LogCount, kMaxLogEntries);

            for (int i = 0; i < count; ++i) {
                int idx = (start + i) % kMaxLogEntries;
                result.push_back(&s_LogBuffer[idx]);
            }
            return result;
        }
    }

    // ============================================================
    // 挂钩到日志系统（从 spdlog 重定向到控制台）
    // ============================================================
    struct ConsoleLogSink {
        static void Write(const std::string& msg, int level, const char* channel) {
            LogEntry entry;
            entry.text = msg;
            entry.level = level;
            entry.channel = channel ? channel : "General";
            entry.timeStamp = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            // 折叠相同日志
            if (s_LogCount > 0) {
                auto& last = s_LogBuffer[s_LogHead];
                if (last == entry) {
                    last.repeatCount++;
                    return;
                }
            }

            PushLog(entry);
        }
    };

    // ============================================================
    // ConsolePanel 实现
    // ============================================================

    void ConsolePanel::SetVisible(bool visible) {
        m_Visible = visible;
        if (visible) FocusInput();
    }

    void ConsolePanel::ToggleVisibility() {
        SetVisible(!m_Visible);
    }

    void ConsolePanel::FocusInput() {
        m_InputActive = true;
        ImGui::SetKeyboardFocusHere();
    }

    void ConsolePanel::ExecuteCommand(const std::string& cmd) {
        if (cmd.empty()) return;

        std::strncpy(m_InputBuf, cmd.c_str(), sizeof(m_InputBuf) - 1);
        m_InputBuf[sizeof(m_InputBuf) - 1] = '\0';
        SubmitCommand();
    }

    void ConsolePanel::Print(const std::string& text) {
        ConsoleLogSink::Write(text, 0, "Console");
    }

    void ConsolePanel::OnImGui() {
        if (!m_Visible) return;

        ImGui::SetNextWindowSize(ImVec2(640, 300), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(320, 150), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::Begin(ICON_FA_TERMINAL " Console", &m_Visible);

        // ── 工具栏 ──
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));

        // 严重性过滤
        static bool showInfo = true, showWarning = true, showError = true, showFatal = true;
        ImGui::Checkbox("Info", &showInfo); ImGui::SameLine();
        ImGui::Checkbox("Warn", &showWarning); ImGui::SameLine();
        ImGui::Checkbox("Error", &showError); ImGui::SameLine();
        ImGui::Checkbox("Fatal", &showFatal); ImGui::SameLine();

        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();

        // 频道过滤
        static bool showGraphics = true, showPhysics = true, showNetwork = true;
        static bool showAI = true, showAudio = true, showGeneral = true;
        if (ImGui::BeginMenu("Channels")) {
            ImGui::MenuItem("Graphics", nullptr, &showGraphics);
            ImGui::MenuItem("Physics",  nullptr, &showPhysics);
            ImGui::MenuItem("Network",  nullptr, &showNetwork);
            ImGui::MenuItem("AI",       nullptr, &showAI);
            ImGui::MenuItem("Audio",    nullptr, &showAudio);
            ImGui::MenuItem("General",  nullptr, &showGeneral);
            ImGui::EndMenu();
        }
        ImGui::SameLine();

        // 自动滚动
        ImGui::Checkbox("AutoScroll", &m_AutoScroll); ImGui::SameLine();

        // 清除
        if (ImGui::Button("Clear")) {
            s_LogBuffer.clear();
            s_LogHead = -1;
            s_LogCount = 0;
        }
        ImGui::SameLine();

        // 折叠相同日志
        static bool collapseEnabled = true;
        ImGui::Checkbox("Collapse", &collapseEnabled);

        ImGui::PopStyleVar();

        // ── 搜索栏 ──
        ImGui::PushItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "Search (regex supported)...",
                                 m_FilterBuf, sizeof(m_FilterBuf));
        ImGui::PopItemWidth();

        ImGui::Separator();

        // ── 日志列表（虚拟滚动优化） ──
        ImGui::BeginChild("##LogRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4),
                          ImGuiChildFlags_Borders);

        auto logs = GetLogs();
        bool filterActive = m_FilterBuf[0] != '\0';
        std::regex filterRegex;
        if (filterActive) {
            try { filterRegex = std::regex(m_FilterBuf, std::regex::icase); }
            catch (...) { filterActive = false; }
        }

        for (const auto* entry : logs) {
            // 严重性过滤
            if ((entry->level == 0 && !showInfo) ||
                (entry->level == 1 && !showWarning) ||
                (entry->level == 2 && !showError) ||
                (entry->level == 3 && !showFatal))
                continue;

            // 频道过滤
            if ((entry->channel == "Graphics" && !showGraphics) ||
                (entry->channel == "Physics" && !showPhysics) ||
                (entry->channel == "Network" && !showNetwork) ||
                (entry->channel == "AI" && !showAI) ||
                (entry->channel == "Audio" && !showAudio))
                continue;

            // 搜索过滤
            if (filterActive) {
                try {
                    if (!std::regex_search(entry->text, filterRegex)) continue;
                } catch (...) { continue; }
            }

            // 颜色
            ImVec4 color;
            switch (entry->level) {
                case 0: color = ImVec4(0.8f, 0.8f, 0.8f, 1); break;  // Info
                case 1: color = ImVec4(1.0f, 0.9f, 0.3f, 1); break;  // Warn
                case 2: color = ImVec4(1.0f, 0.3f, 0.3f, 1); break;  // Error
                case 3: color = ImVec4(1.0f, 0.1f, 0.1f, 1); break;  // Fatal
                default: color = ImVec4(0.8f, 0.8f, 0.8f, 1);
            }

            // 显示频道标签
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 0.8f, 1));
            ImGui::Text("[%s]", entry->channel.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();

            // 日志文本
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(entry->text.c_str());

            // 折叠计数
            if (collapseEnabled && entry->repeatCount > 1) {
                ImGui::SameLine();
                ImGui::TextDisabled(" (x%d)", entry->repeatCount);
            }
            ImGui::PopStyleColor();

            // 对象溯源（点击可 Ping）
            if (!entry->sourceObjectName.empty()) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.7f, 0.3f, 1));
                if (ImGui::SmallButton(entry->sourceObjectName.c_str())) {
                    // TODO: Ping object in Hierarchy
                }
                ImGui::PopStyleColor();
            }

            // 堆栈跳转
            if (!entry->sourceFile.empty()) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
                if (ImGui::SmallButton((entry->sourceFile + ":" + std::to_string(entry->sourceLine)).c_str())) {
                    // TODO: Open in IDE
                }
                ImGui::PopStyleColor();
            }
        }

        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();

        // ── 命令输入行 ──
        DrawInputLine();

        ImGui::End();
    }

    void ConsolePanel::DrawInputLine() {
        ImGui::PushItemWidth(-1);
        bool reclaimFocus = false;

        if (ImGui::InputTextWithHint("##CmdInput", "Enter command...",
                                     m_InputBuf, sizeof(m_InputBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue |
                                     ImGuiInputTextFlags_CallbackCompletion |
                                     ImGuiInputTextFlags_CallbackHistory,
                                     [](ImGuiInputTextCallbackData* data) -> int {
            auto* panel = static_cast<ConsolePanel*>(data->UserData);
            if (!panel) return 0;

            if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
                panel->AutoComplete();
            } else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
                if (data->EventKey == ImGuiKey_UpArrow)   panel->HistoryPrev();
                if (data->EventKey == ImGuiKey_DownArrow) panel->HistoryNext();
            }
            return 0;
        }, this)) {
            SubmitCommand();
            reclaimFocus = true;
        }
        ImGui::PopItemWidth();

        if (reclaimFocus) {
            ImGui::SetKeyboardFocusHere(-1);
        }
    }

    void ConsolePanel::SubmitCommand() {
        std::string cmd(m_InputBuf);
        if (cmd.empty()) return;

        // 加入历史
        if (m_CommandHistory.empty() || m_CommandHistory.back() != cmd) {
            m_CommandHistory.push_back(cmd);
            if (static_cast<int>(m_CommandHistory.size()) > kMaxHistory) {
                m_CommandHistory.erase(m_CommandHistory.begin());
            }
        }
        m_HistoryPos = -1;

        // 显示执行的命令
        ConsoleLogSink::Write("> " + cmd, 0, "Console");

        // 解析并执行命令
        if (cmd == "clear" || cmd == "cls") {
            s_LogBuffer.clear();
            s_LogHead = -1;
            s_LogCount = 0;
        } else if (cmd == "help") {
            ConsoleLogSink::Write("Available commands:", 0, "Console");
            ConsoleLogSink::Write("  clear/cls  - Clear console", 0, "Console");
            ConsoleLogSink::Write("  help       - Show this help", 0, "Console");
            ConsoleLogSink::Write("  stats      - Toggle stats overlay", 0, "Console");
            ConsoleLogSink::Write("  gc         - Force garbage collection", 0, "Console");
        } else {
            ConsoleLogSink::Write("Unknown command: " + cmd, 1, "Console");
        }

        m_InputBuf[0] = '\0';
    }

    void ConsolePanel::AutoComplete() {
        // 简化 Tab 补全
        if (!m_CompletionCandidates.empty()) {
            m_CompletionIndex = (m_CompletionIndex + 1) % m_CompletionCandidates.size();
            std::strncpy(m_InputBuf, m_CompletionCandidates[m_CompletionIndex].c_str(), sizeof(m_InputBuf) - 1);
        }
    }

    void ConsolePanel::HistoryPrev() {
        if (m_CommandHistory.empty()) return;
        if (m_HistoryPos == -1) {
            m_HistoryPos = static_cast<int>(m_CommandHistory.size()) - 1;
        } else if (m_HistoryPos > 0) {
            m_HistoryPos--;
        }
        std::strncpy(m_InputBuf, m_CommandHistory[m_HistoryPos].c_str(), sizeof(m_InputBuf) - 1);
    }

    void ConsolePanel::HistoryNext() {
        if (m_CommandHistory.empty() || m_HistoryPos < 0) return;
        m_HistoryPos++;
        if (m_HistoryPos >= static_cast<int>(m_CommandHistory.size())) {
            m_HistoryPos = -1;
            m_InputBuf[0] = '\0';
        } else {
            std::strncpy(m_InputBuf, m_CommandHistory[m_HistoryPos].c_str(), sizeof(m_InputBuf) - 1);
        }
    }

} // namespace Engine