#include "Engine/ConsolePanel.h"
#include "Engine/ConsoleLog.h"

#include <imgui.h>
#include <cstring>
#include <cctype>

namespace Engine {

    void ConsolePanel::OnImGui()
    {
        if (!m_Visible)
            return;

        ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_FirstUseEver);
        ImGui::Begin("Console", &m_Visible);

        // ── 工具栏 ──
        {
            // 清除按钮
            if (ImGui::Button("Clear"))
            {
                ConsoleLog::Instance().Clear();
                m_ScrollToBottom = false;
            }

            ImGui::SameLine();

            // 自动滚动开关
            if (ImGui::Checkbox("Auto-scroll", &m_AutoScroll))
            {
                if (m_AutoScroll)
                    m_ScrollToBottom = true;
            }

            ImGui::SameLine();

            // 搜索过滤框
            ImGui::SetNextItemWidth(200);
            ImGui::InputTextWithHint("##Filter", "Filter...", m_FilterBuf, sizeof(m_FilterBuf));
        }

        ImGui::Separator();

        // ── 日志显示区域（BeginChild） ──
        ImGui::BeginChild("LogScroll", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);

        if (ImGui::BeginTable("LogTable", 3,
                              ImGuiTableFlags_NoPadInnerX |
                              ImGuiTableFlags_SizingFixedFit))
        {
            // 列设置：时间戳 | 级别 | 消息
            ImGui::TableSetupColumn("Time",  ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
            // 不显示表头

            auto& log = ConsoleLog::Instance();
            const LogEntry* buffer = log.GetBuffer();
            uint32 count = log.GetCount();
            uint32 start = log.GetStartIndex();
            uint32 cap   = log.GetCapacity();

            // 是否需要过滤
            bool hasFilter = m_FilterBuf[0] != '\0';

            // 遍历环形缓冲区（从最旧到最新）
            for (uint32 i = 0; i < count; ++i)
            {
                uint32 idx = (start + i) % cap;
                const auto& entry = buffer[idx];

                // 过滤
                if (hasFilter)
                {
                    // 简单子串匹配（大小写不敏感）
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

                // ── 时间戳列 ──
                ImGui::TableSetColumnIndex(0);
                int minutes = static_cast<int>(entry.timestamp) / 60;
                int seconds = static_cast<int>(entry.timestamp) % 60;
                ImGui::Text("%02d:%02d", minutes, seconds);

                // ── 级别列（着色） ──
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(
                    ImColor(LogLevelColor(entry.level)),
                    "%s", LogLevelName(entry.level)
                );

                // ── 消息列 ──
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(entry.message.c_str());
            }

            // 自动滚动到底部
            if (m_AutoScroll && m_ScrollToBottom)
            {
                ImGui::SetScrollHereY(1.0f);
                m_ScrollToBottom = false;
            }

            ImGui::EndTable();
        }

        ImGui::EndChild();

        // 当用户手动滚动时，暂停自动滚动
        if (ImGui::GetScrollY() < ImGui::GetScrollMaxY())
        {
            // 用户向上滚动了，不自动滚回去
            // 但保持 m_AutoScroll 的状态不变
        }
        else
        {
            // 已经在底部，下次新消息时自动滚动
            if (m_AutoScroll)
                m_ScrollToBottom = true;
        }

        ImGui::End();
    }

} // namespace Engine
