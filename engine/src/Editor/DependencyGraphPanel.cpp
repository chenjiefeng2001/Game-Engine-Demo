#include "Engine/Editor/DependencyGraphPanel.h"
#include "Engine/Core/FileSystem.h"
#include <imgui.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace Engine {

    // ============================================================
    // OnImGui — 主渲染入口
    // ============================================================

    void DependencyGraphPanel::OnImGui() {
        if (!m_Visible) return;

        ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Dependency Graph", &m_Visible)) {
            ImGui::End();
            return;
        }

        if (!m_Database) {
            ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "No AssetDatabase assigned.");
            ImGui::End();
            return;
        }

        const auto& allEntries = m_Database->GetAllEntries();
        if (allEntries.empty()) {
            ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1), "Asset database is empty. Scan a directory first.");
            ImGui::End();
            return;
        }

        // ── 布局：左侧资产列表 | 右侧详情 ──
        ImGui::Columns(2, "depColumns", true);
        ImGui::SetColumnWidth(0, 280.0f);

        DrawAssetSelector();

        ImGui::NextColumn();

        DrawDependencyView();

        ImGui::Columns(1);
        ImGui::End();
    }

    // ============================================================
    // 资产选择器
    // ============================================================

    void DependencyGraphPanel::DrawAssetSelector() {
        ImGui::Text("Assets (%zu)", m_Database->GetCount());
        ImGui::Separator();

        // ── 过滤器 ──
        {
            char filterBuf[256] = {};
            std::strncpy(filterBuf, m_FilterText.c_str(), sizeof(filterBuf) - 1);
            if (ImGui::InputText("Filter", filterBuf, sizeof(filterBuf)))
                m_FilterText = filterBuf;
        }
        ImGui::Separator();

        // ── 资产列表 ──
        ImGui::BeginChild("AssetList", ImVec2(0, 0), true);

        const auto& allEntries = m_Database->GetAllEntries();
        int index = 0;
        int selectedIndex = -1;

        for (const auto& entry : allEntries) {
            // 过滤
            if (!m_FilterText.empty()) {
                std::string lowerFilter = m_FilterText;
                std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);

                std::string lowerPath = entry.path;
                std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

                std::string lowerGuid = entry.guid;
                std::transform(lowerGuid.begin(), lowerGuid.end(), lowerGuid.begin(), ::tolower);

                if (lowerPath.find(lowerFilter) == std::string::npos &&
                    lowerGuid.find(lowerFilter) == std::string::npos) {
                    index++;
                    continue;
                }
            }

            // 确定选中项
            bool isSelected = (entry.guid == m_SelectedGuid);
            if (isSelected) selectedIndex = index;

            // 行标签
            std::string label = entry.fileName + "###" + entry.guid;

            // 根据类型显示颜色
            ImVec4 color(1,1,1,1);
            switch (entry.type) {
                case ResourceType::Texture:   color = ImVec4(0.4f,0.8f,0.4f,1); break;
                case ResourceType::Shader:    color = ImVec4(0.4f,0.6f,1.0f,1); break;
                case ResourceType::AudioClip: color = ImVec4(1.0f,0.6f,0.4f,1); break;
                case ResourceType::Font:      color = ImVec4(0.9f,0.9f,0.4f,1); break;
                case ResourceType::Mesh:      color = ImVec4(0.7f,0.4f,0.9f,1); break;
                case ResourceType::Material:  color = ImVec4(1.0f,0.5f,0.8f,1); break;
                default: break;
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);

            // 依赖指示器
            bool hasDeps = !entry.dependencies.empty();
            bool hasRefs = false;
            for (const auto& e : allEntries) {
                for (const auto& dep : e.dependencies) {
                    if (dep == entry.guid) { hasRefs = true; break; }
                }
                if (hasRefs) break;
            }

            std::string prefix;
            if (hasDeps && hasRefs) prefix = "⬡ ";
            else if (hasDeps)       prefix = "△ ";
            else if (hasRefs)       prefix = "▽ ";
            else                    prefix = "  ";

            if (ImGui::Selectable((prefix + label).c_str(), &isSelected)) {
                m_SelectedGuid = entry.guid;
            }

            ImGui::PopStyleColor();

            index++;
        }

        ImGui::EndChild();
    }

    // ============================================================
    // 依赖详情视图
    // ============================================================

    void DependencyGraphPanel::DrawDependencyView() {
        if (m_SelectedGuid.empty()) {
            ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1), "Select an asset from the left list.");
            ImGui::End();
            return;
        }

        const auto* entry = m_Database->FindByGuid(m_SelectedGuid);
        if (!entry) {
            ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Selected asset not found.");
            return;
        }

        // ── 资产基本信息 ──
        ImGui::Text("Asset: %s", entry->fileName.c_str());
        ImGui::Separator();

        ImGui::Text("GUID:     %s", entry->guid.c_str());
        ImGui::Text("Path:     %s", entry->path.c_str());
        ImGui::Text("Type:     %s", ResourceTypeName(entry->type));
        ImGui::Text("Size:     %llu bytes", (unsigned long long)entry->fileSize);
        ImGui::Text("Valid:    %s", entry->isValid ? "Yes" : "No");

        ImGui::Separator();

        // ── 标签 ──
        if (!entry->tags.empty()) {
            ImGui::Text("Tags: ");
            ImGui::SameLine();
            for (size_t i = 0; i < entry->tags.size(); ++i) {
                ImGui::TextColored(ImVec4(0.6f,0.8f,1.0f,1), "%s", entry->tags[i].c_str());
                if (i + 1 < entry->tags.size()) ImGui::SameLine();
            }
        }

        ImGui::Separator();
        ImGui::Spacing();

        // ── 依赖关系链 ──
        DrawDependencyChain();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── 管道控制 ──
        DrawPipelineControls();

        // ── 循环依赖警告 ──
        DrawCycleWarnings();

        // ── 构建报告 ──
        DrawBuildReport();
    }

    // ============================================================
    // 上下游依赖链
    // ============================================================

    void DependencyGraphPanel::DrawDependencyChain() {
        if (!m_Database || m_SelectedGuid.empty()) return;

        auto chain = m_Pipeline
            ? m_Pipeline->GetDependencyChain(*m_Database, m_SelectedGuid)
            : std::make_pair(
                std::vector<const AssetEntry*>(),
                std::vector<const AssetEntry*>());

        auto& upstream   = chain.first;   // 引用此资产的
        auto& downstream = chain.second;  // 此资产引用的

        // ── 下游依赖（此资产依赖的） ──
        ImGui::TextColored(ImVec4(0.4f,0.8f,1.0f,1), "Dependencies (%zu):", downstream.size());
        ImGui::Indent(16.0f);
        if (downstream.empty()) {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "  (none)");
        } else {
            for (const auto* dep : downstream) {
                bool isSelected = (dep && dep->guid == m_SelectedGuid);
                if (dep) {
                    std::string label = dep->fileName + "###" + dep->guid;
                    if (ImGui::Selectable(label.c_str(), &isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                        if (ImGui::IsMouseDoubleClicked(0))
                            m_SelectedGuid = dep->guid;
                    }
                }
            }
        }
        ImGui::Unindent(16.0f);

        ImGui::Spacing();

        // ── 上游引用（谁依赖此资产） ──
        ImGui::TextColored(ImVec4(0.8f,0.6f,0.4f,1), "Referenced by (%zu):", upstream.size());
        ImGui::Indent(16.0f);
        if (upstream.empty()) {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "  (none)");
        } else {
            for (const auto* ref : upstream) {
                bool isSelected = (ref && ref->guid == m_SelectedGuid);
                if (ref) {
                    std::string label = ref->fileName + "###" + ref->guid;
                    if (ImGui::Selectable(label.c_str(), &isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                        if (ImGui::IsMouseDoubleClicked(0))
                            m_SelectedGuid = ref->guid;
                    }
                }
            }
        }
        ImGui::Unindent(16.0f);
    }

    // ============================================================
    // 管道控制
    // ============================================================

    void DependencyGraphPanel::DrawPipelineControls() {
        if (!m_Pipeline) return;

        ImGui::TextColored(ImVec4(0.8f,0.9f,0.6f,1), "Pipeline Controls");
        ImGui::Separator();

        // ── 输出目录 ──
        {
            char outBuf[256] = {};
            std::strncpy(outBuf, m_OutputDir.c_str(), sizeof(outBuf) - 1);
            if (ImGui::InputText("Output Dir", outBuf, sizeof(outBuf)))
                m_OutputDir = outBuf;
        }

        ImGui::Spacing();

        // ── 匹配规则展示 ──
        if (m_Database) {
            const auto* entry = m_Database->FindByGuid(m_SelectedGuid);
            if (entry) {
                auto matchingRules = m_Pipeline->GetMatchingRules(*entry);
                if (!matchingRules.empty()) {
                    ImGui::Text("Matching rules (%zu):", matchingRules.size());
                    ImGui::Indent(16.0f);
                    for (const auto& rule : matchingRules) {
                        ImGui::BulletText("%s [%s]",
                            rule->GetName(),
                            PipelineStageName(rule->GetStage()));
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", rule->GetDescription());
                    }
                    ImGui::Unindent(16.0f);
                } else {
                    ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "No matching rules.");
                }
            }
        }

        ImGui::Spacing();

        // ── 操作按钮 ──
        if (ImGui::Button("Process This Asset", ImVec2(200, 0))) {
            const auto* entry = m_Database->FindByGuid(m_SelectedGuid);
            if (entry && m_Pipeline) {
                auto result = m_Pipeline->Process(*entry, m_OutputDir);
                m_HasReport = true;
                m_LastReport.entries.clear();

                BuildReportEntry bre;
                bre.guid = entry->guid;
                bre.path = entry->path;
                bre.success = result.success;
                bre.error = result.errorMessage;
                bre.elapsedSeconds = result.elapsedSeconds;
                bre.lastStage = result.lastStage;

                m_LastReport.entries.push_back(bre);
                m_LastReport.succeeded = result.success ? 1 : 0;
                m_LastReport.failed = result.success ? 0 : 1;
                m_LastReport.totalElapsedSeconds = result.elapsedSeconds;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Build All Assets", ImVec2(200, 0))) {
            if (m_Pipeline && m_Database) {
                m_LastReport = m_Pipeline->BuildAll(*m_Database, m_OutputDir);
                m_HasReport = true;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Build Dirty Only", ImVec2(200, 0))) {
            if (m_Pipeline && m_Database) {
                m_LastReport = m_Pipeline->BuildDirty(*m_Database, m_OutputDir);
                m_HasReport = true;
            }
        }

        ImGui::Spacing();

        // ── 循环检测 ──
        if (ImGui::Button("Detect Cycles", ImVec2(200, 0))) {
            if (m_Pipeline && m_Database) {
                m_CachedCycles = m_Pipeline->DetectCycles(*m_Database);
                m_CyclesChecked = true;
            }
        }

        ImGui::SameLine();

        // ── 完整性检查 ──
        if (ImGui::Button("Validate Integrity", ImVec2(200, 0))) {
            if (m_Database) {
                auto result = m_Database->ValidateIntegrity();
                if (result.IsClean) {
                    ImGui::OpenPopup("IntegrityOK");
                } else {
                    ImGui::OpenPopup("IntegrityIssues");
                }
            }
        }

        // Integrity popups
        if (ImGui::BeginPopupModal("IntegrityOK", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("All references are valid.");
            ImGui::Separator();
            ImGui::Text("Total: %zu, Valid: %zu", 
                m_Database->ValidateIntegrity().totalReferences,
                m_Database->ValidateIntegrity().validReferences);
            if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("IntegrityIssues", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            auto result = m_Database->ValidateIntegrity();
            ImGui::Text("Found %zu broken references:", result.brokenReferences);
            ImGui::Separator();
            for (const auto& issue : result.issues) {
                ImGui::BulletText("[%s] %s → %s",
                    issue.issueType.c_str(),
                    issue.sourcePath.c_str(),
                    issue.brokenGuid.c_str());
            }
            ImGui::Separator();
            if (ImGui::Button("Repair", ImVec2(120, 0))) {
                auto result = m_Database->RepairIntegrity("repaired from panel");
                ImGui::Text("Validation: %s", result.valid ? "OK" : "Failed");
            }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // ============================================================
    // 循环依赖警告
    // ============================================================

    void DependencyGraphPanel::DrawCycleWarnings() {
        if (!m_CyclesChecked || m_CachedCycles.empty()) return;

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(1.0f,0.3f,0.3f,1), "⚠ Cycle Detected!");
        ImGui::Text("%zu cycle(s) found:", m_CachedCycles.size());

        for (size_t i = 0; i < m_CachedCycles.size(); ++i) {
            const auto& cycle = m_CachedCycles[i];
            std::string title = "Cycle " + std::to_string(i + 1) + " (" + 
                                std::to_string(cycle.cycleGuids.size()) + " nodes)";
            if (ImGui::TreeNode(title.c_str())) {
                for (size_t j = 0; j < cycle.cyclePaths.size(); ++j) {
                    ImGui::BulletText("%s", cycle.cyclePaths[j].c_str());
                }
                ImGui::TreePop();
            }
        }
    }

    // ============================================================
    // 构建报告
    // ============================================================

    void DependencyGraphPanel::DrawBuildReport() {
        if (!m_HasReport) return;

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.8f,0.9f,0.6f,1), "Build Report");
        ImGui::Text("Total: %zu | Succeeded: %zu | Failed: %zu | Time: %.3fs",
            m_LastReport.entries.size(),
            m_LastReport.succeeded,
            m_LastReport.failed,
            m_LastReport.totalElapsedSeconds);

        if (!m_LastReport.entries.empty()) {
            ImGui::Separator();
            ImGui::BeginChild("ReportList", ImVec2(0, 150), true);

            for (const auto& entry : m_LastReport.entries) {
                if (entry.success) {
                    ImGui::TextColored(ImVec4(0.3f,0.8f,0.3f,1), "✓");
                } else {
                    ImGui::TextColored(ImVec4(1.0f,0.3f,0.3f,1), "✗");
                }
                ImGui::SameLine();

                ImGui::Text("%s", entry.path.c_str());
                ImGui::SameLine(400);
                ImGui::Text("(%s, %.3fs)",
                    entry.success ? "OK" : entry.error.c_str(),
                    entry.elapsedSeconds);

                // 点击选中对应资产
                ImGui::SameLine();
                std::string selectLabel = "Select##" + entry.guid;
                if (ImGui::SmallButton(selectLabel.c_str())) {
                    m_SelectedGuid = entry.guid;
                }
            }

            ImGui::EndChild();
        }
    }

} // namespace Engine
