/**
 * @file MemoryPanel.cpp
 * @brief 内存监控面板 — 分类折线图 + 详细追踪（标签/分配大小/最近记录）
 */

#include "Engine/MemoryPanel.h"
#include "Engine/MemoryTracker.h"
#include "Engine/Debug/StackTrace.h"
#include <imgui.h>
#include <cstdio>
#include <algorithm>
#include <cstring>

namespace Engine {

    void MemoryPanel::OnImGui()
    {
        if (!m_Visible) return;

        ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Memory Monitor", &m_Visible)) { ImGui::End(); return; }

        size_t usage = MemoryTracker::GetCurrentUsage();
        size_t peak  = MemoryTracker::GetPeakUsage();

        ImGui::TextColored(ImVec4(0.3f,1.0f,0.4f,1.0f), "Heap: %s  |  Peak: %s",
            MemoryTracker::FormatBytes(usage).c_str(),
            MemoryTracker::FormatBytes(peak).c_str());

        // ── 标签页切换 ──
        if (ImGui::BeginTabBar("MemTabs")) {
            if (ImGui::BeginTabItem("Overview")) { DrawOverview(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Details"))  { DrawDetails(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Recent"))   { DrawRecent(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }

        // ── 底部：当前选中记录的堆栈详情（始终显示） ──
        DrawStackTraceDetail();

        ImGui::End();
    }

    void MemoryPanel::DrawOverview() {
        size_t usage  = MemoryTracker::GetCurrentUsage();
        size_t fAlloc = MemoryTracker::GetFrameAllocBytes();
        size_t fFree  = MemoryTracker::GetFrameFreeBytes();

        // ── 分类表格 ──
        if (ImGui::BeginTable("CatTable", 5, ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn("Category"); ImGui::TableSetupColumn("Current"); ImGui::TableSetupColumn("Peak");
            ImGui::TableSetupColumn("Total"); ImGui::TableSetupColumn("Count"); ImGui::TableHeadersRow();
            for (int i = 0; i < MemoryTracker::GetCategoryCount(); ++i) {
                auto cat = (MemCategory)i; auto st = MemoryTracker::GetCategoryStat(cat);
                if (st.current==0 && st.total==0) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextColored(ImColor(MemCategoryColor(cat)), "%s", MemCategoryName(cat));
                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", MemoryTracker::FormatBytes(st.current).c_str());
                ImGui::TableSetColumnIndex(2); ImGui::Text("%s", MemoryTracker::FormatBytes(st.peak).c_str());
                ImGui::TableSetColumnIndex(3); ImGui::Text("%s", MemoryTracker::FormatBytes(st.total).c_str());
                ImGui::TableSetColumnIndex(4); ImGui::Text("%zu", st.count);
            }
            ImGui::EndTable();
        }

        // 占比条
        if (usage > 0) {
            for (int i = 0; i < MemoryTracker::GetCategoryCount(); ++i) {
                auto cat = (MemCategory)i; auto st = MemoryTracker::GetCategoryStat(cat);
                if (st.current==0) continue;
                float frac = (float)st.current/(float)usage;
                char lbl[32]; snprintf(lbl,32,"%.0f%%",frac*100);
                ImGui::ProgressBar(frac, ImVec2(-30,12), lbl);
                ImGui::SameLine(0,4); ImGui::TextColored(ImColor(MemCategoryColor(cat)), "%s", MemCategoryName(cat));
            }
        }

        ImGui::Separator();
        ImGui::Text("Frame: +%s / -%s", MemoryTracker::FormatBytes(fAlloc).c_str(), MemoryTracker::FormatBytes(fFree).c_str());

        // ── 历史折线图 ──
        ImGui::Checkbox("Show History", &m_ShowHistory);
        if (m_ShowHistory) {
            const auto& hist = MemoryTracker::GetUsageHistory();
            float vals[MemoryTracker::kHistoryFrames];
            size_t maxV=1, minV=size_t(-1);
            for (int i=0;i<MemoryTracker::kHistoryFrames;++i){vals[i]=(float)hist[i];if(hist[i]>maxV)maxV=hist[i];if(hist[i]<minV)minV=hist[i];}
            if(minV==size_t(-1))minV=0;
            char ov[64]; snprintf(ov,64,"Max:%s",MemoryTracker::FormatBytes(maxV).c_str());
            float rMax = (float)maxV*1.2f; if(rMax<1024)rMax=1024;
            ImGui::PlotLines("##Hist",vals,MemoryTracker::kHistoryFrames,0,ov,(float)minV*0.8f,rMax,ImVec2(-1,100));
            ImGui::TextDisabled("(last %d frames)", MemoryTracker::kHistoryFrames);
        }
    }

    void MemoryPanel::DrawDetails() {
        // ── 标签统计 ──
        static int detailCat = 0;
        ImGui::Combo("Category", &detailCat, "Retail\0Debug\0Stack\0GPU\0");
        auto cat = (MemCategory)detailCat;

        auto tags = MemoryTracker::GetTagStats(cat);
        if (tags.empty()) {
            ImGui::TextDisabled("No tagged allocations in this category.");
            return;
        }

        size_t catTotal = MemoryTracker::GetCategoryStat(cat).current;
        if (ImGui::BeginTable("TagTable", 4, ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Tag"); ImGui::TableSetupColumn("Bytes"); ImGui::TableSetupColumn("Count"); ImGui::TableSetupColumn("%");
            ImGui::TableHeadersRow();
            for (auto& t : tags) {
                float pct = catTotal ? (float)t.bytes/(float)catTotal*100 : 0;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%s", t.tag.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", MemoryTracker::FormatBytes(t.bytes).c_str());
                ImGui::TableSetColumnIndex(2); ImGui::Text("%zu", t.count);
                ImGui::TableSetColumnIndex(3);
                ImGui::ProgressBar(pct/100.0f, ImVec2(-1,12), "");
                ImGui::SameLine(0,2); ImGui::Text("%.1f%%", pct);
            }
            ImGui::EndTable();
        }
    }

    void MemoryPanel::DrawRecent() {
        int count = 0;
        auto* recs = MemoryTracker::GetRecentRecords(count);
        int start = count > 200 ? count - 200 : 0;

        ImGui::Text("Last %d/%d records — Click a row to view stack trace:", count-start, kRecentMax);

        ImVec2 tableSize = ImVec2(0, m_SelectedRecordIndex >= 0 ? 180 : 280);
        if (ImGui::BeginTable("RecTable", 4,
                ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,
                tableSize))
        {
            ImGui::TableSetupColumn("Type");   ImGui::TableSetupColumn("Size");
            ImGui::TableSetupColumn("Category"); ImGui::TableSetupColumn("Tag");
            ImGui::TableHeadersRow();

            for (int i = start; i < count; ++i) {
                auto& r = recs[i % kRecentMax];
                ImGui::TableNextRow();

                // 第一列：可点击选择
                ImGui::TableSetColumnIndex(0);
                char label[64];
                snprintf(label, sizeof(label), "%s##rec%d", r.isFree ? "FREE" : "ALLOC", i);
                ImVec4 col = r.isFree ? ImVec4(1,0.3f,0.3f,1) : ImVec4(0.3f,1,0.3f,1);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                if (ImGui::Selectable(label, m_SelectedRecordIndex == i,
                                      ImGuiSelectableFlags_SpanAllColumns))
                {
                    m_SelectedRecordIndex = (m_SelectedRecordIndex == i) ? -1 : i;
                }
                ImGui::PopStyleColor();

                if (ImGui::IsItemHovered() && r.stackDepth > 0) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Stack: %d frames — Click to view", r.stackDepth);
                    ImGui::EndTooltip();
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", MemoryTracker::FormatBytes(r.size).c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(ImColor(MemCategoryColor(r.cat)), "%s", MemCategoryName(r.cat));

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", r.tag[0] ? r.tag : "-");
            }
            ImGui::EndTable();
        }
    }

    void MemoryPanel::DrawStackTraceDetail() {
        if (m_SelectedRecordIndex < 0) return;

        int count = 0;
        auto* recs = MemoryTracker::GetRecentRecords(count);
        if (count <= 0) return;

        int idx = m_SelectedRecordIndex % kRecentMax;
        auto& r = recs[idx];

        // ── 选中记录摘要 ──
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.8f,0.8f,1.0f,1.0f), "📋 Selected Record");
        ImGui::SameLine();
        ImGui::TextDisabled("(ID:%d)", m_SelectedRecordIndex);

        ImGui::Text("Type: %s  |  Size: %s  |  Category: %s  |  Tag: %s",
            r.isFree ? "FREE" : "ALLOC",
            MemoryTracker::FormatBytes(r.size).c_str(),
            MemCategoryName(r.cat),
            r.tag[0] ? r.tag : "-");

        // ── 堆栈帧列表 ──
        if (r.stackDepth > 0 && r.stackFrames[0] != nullptr) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f,0.9f,0.5f,1.0f), "🔍 Call Stack (%d frames):", r.stackDepth);

            if (ImGui::BeginChild("StackFrameList", ImVec2(0, 120), true,
                                  ImGuiWindowFlags_HorizontalScrollbar))
            {
                for (int fi = 0; fi < r.stackDepth && fi < kStackFramesPerRecord; ++fi) {
                    void* addr = r.stackFrames[fi];
                    if (addr == nullptr) break;

                    // 延迟解析符号（只在查看时解析，不在分配时）
                    std::string symbol = StackTrace::ResolveSymbol(addr);

                    ImGui::Text("  #%d  0x%p", fi, addr);
                    ImGui::SameLine();
                    if (!symbol.empty()) {
                        ImGui::TextColored(ImVec4(0.6f,0.8f,1.0f,1.0f), "%s", symbol.c_str());
                    } else {
                        ImGui::TextDisabled("(symbol unavailable)");
                    }
                }
            }
            ImGui::EndChild();
        } else {
            ImGui::Separator();
            ImGui::TextDisabled("(No stack trace captured for this record.)");
            ImGui::TextDisabled("Tip: Stack capture requires _DEBUG + ENGINE_ENABLE_PROFILING on Windows.");
        }
    }

} // namespace Engine
