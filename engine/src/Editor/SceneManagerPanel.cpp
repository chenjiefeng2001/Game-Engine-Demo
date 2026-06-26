#include "Engine/Editor/SceneManagerPanel.h"
#include "Engine/Core/Scene/SceneManager.h"
#include "Engine/Core/Scene/SceneContext.h"
#include "Engine/Core/Scene/SceneTypes.h"
#include "Engine/Core/Level/Level.h"
#include "Engine/Core/Level/LevelManager.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/JobSystem.h"

#include "Engine/Editor/IconsFontAwesome6.h"
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace Engine {

    // ═══════════════════════════════════════════════════════════════
    // 辅助颜色工具
    // ═══════════════════════════════════════════════════════════════

    namespace {

        ImVec4 LevelStateToColor(LevelState state) {
            switch (state) {
                case LevelState::Unloaded:     return ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // 灰色
                case LevelState::Loading:      return ImVec4(0.2f, 0.6f, 1.0f, 1.0f); // 蓝色
                case LevelState::Loaded:       return ImVec4(0.8f, 0.8f, 0.2f, 1.0f); // 黄色
                case LevelState::Activating:   return ImVec4(0.2f, 0.8f, 0.6f, 1.0f); // 青色
                case LevelState::Active:       return ImVec4(0.2f, 0.8f, 0.2f, 1.0f); // 绿色
                case LevelState::Deactivating: return ImVec4(0.9f, 0.5f, 0.2f, 1.0f); // 橙色
                case LevelState::Unloading:    return ImVec4(0.8f, 0.3f, 0.3f, 1.0f); // 红色
                case LevelState::Failed:       return ImVec4(1.0f, 0.2f, 0.2f, 1.0f); // 亮红
                default:                       return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
            }
        }

        const char* LoadTypeToString(bool isAdditive) {
            return isAdditive ? "Additive" : "Single";
        }

    } // anonymous namespace

    // ═══════════════════════════════════════════════════════════════
    // 构造
    // ═══════════════════════════════════════════════════════════════

    SceneManagerPanel::SceneManagerPanel() {
        // 预填充搜索缓冲区
        m_SearchBuffer[0] = '\0';
        m_NewSceneName[0] = '\0';
    }

    // ═══════════════════════════════════════════════════════════════
    // 主渲染入口
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerPanel::OnImGui() {
        if (!m_Visible) return;

        // 窗口标志 — 可停靠、可调整大小
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        ImGui::SetNextWindowSizeConstraints(ImVec2(400, 200), ImVec2(FLT_MAX, FLT_MAX));
        if (!ImGui::Begin(ICON_FA_CUBES " Scene Manager", &m_Visible, flags)) {
            ImGui::End();
            return;
        }

        // ── 缓存性能数据 ──
        m_CachedProfiles = SceneManager::GetLoadProfiles();

        // ═══ 布局：顶部工具栏 + 左右分栏 + 底部日志 ═══

        // 1. 工具栏
        DrawToolbar();

        // 2. 主内容区域（左侧场景组列表 + 中间主区域）
        ImGui::BeginGroup();

        // 左侧：场景组列表
        ImGui::BeginChild("GroupSidebar", ImVec2(m_LeftPanelWidth, -ImGui::GetFrameHeightWithSpacing() - 40),
                          true, ImGuiWindowFlags_HorizontalScrollbar);
        DrawGroupSidebar();
        ImGui::EndChild();

        ImGui::SameLine();

        // 分割线（可拖拽调整宽度）
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 8.0f);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::PopStyleVar();

        // 检测拖拽调整左侧面板宽度
        if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(0)) {
            m_LeftPanelWidth += ImGui::GetIO().MouseDelta.x;
            if (m_LeftPanelWidth < 120.0f) m_LeftPanelWidth = 120.0f;
            if (m_LeftPanelWidth > 500.0f) m_LeftPanelWidth = 500.0f;
        }

        ImGui::SameLine();

        // 中间：主区域（组编辑器 / 运行时监视器 / 性能分析）
        ImGui::BeginChild("MainArea", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 40), true);
        DrawMainArea();
        ImGui::EndChild();

        ImGui::EndGroup();

        // 3. 底部日志
        DrawFooter();

        // ── 创建新场景对话框 ──
        if (m_ShowNewSceneDialog) {
            CreateNewSceneLevelDialog();
        }

        ImGui::End();
    }

    // ═══════════════════════════════════════════════════════════════
    // 工具栏
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerPanel::DrawToolbar() {
        // 搜索框
        ImGui::PushItemWidth(200.0f);
        ImGui::InputTextWithHint("##Search", "Search scenes...", m_SearchBuffer, sizeof(m_SearchBuffer));
        ImGui::PopItemWidth();

        ImGui::SameLine();

        // 新建组
        if (ImGui::Button("+ New Group")) {
            // 创建空编辑状态（char[] 只能用 strncpy/snprintf 赋值）
            m_EditState = SceneGroupEditState{};
            snprintf(m_EditState.groupName, sizeof(m_EditState.groupName),
                     "NewGroup_%lld", (long long)std::time(nullptr));
            m_IsEditing = true;
            m_GroupDirty = true;
            m_SelectedGroupIndex = -1;
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // 一键打包 Build Settings
        if (ImGui::Button("Sync Build Settings")) {
            SyncBuildSettings();
        }

        ImGui::SameLine();

        // 过滤器下拉
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::BeginCombo("##Filter", "Filter")) {
            ImGui::Checkbox("Normal", &m_FilterNormal);
            ImGui::Checkbox("Streaming", &m_FilterStreaming);
            ImGui::Checkbox("Cinematic", &m_FilterCinematic);
            ImGui::EndCombo();
        }

        ImGui::SameLine();

        // 设置按钮
        if (ImGui::SmallButton("⚙ Settings")) {
            ImGui::OpenPopup("SettingsPopup");
        }

        if (ImGui::BeginPopup("SettingsPopup")) {
            if (ImGui::MenuItem("Show Validation Panel", nullptr, &m_ShowValidation)) {}
            if (ImGui::MenuItem("Show Performance Panel", nullptr, &m_ShowPerformance)) {}
            ImGui::Separator();
            ImGui::TextDisabled("Load Timeout: %u ms", SceneManager::GetLevelManager().FindLevel("") == nullptr ? 10000 : 10000);
            ImGui::Separator();
            if (ImGui::MenuItem("Clear Load Profiles")) {
                SceneManager::ClearLoadProfiles();
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();
    }

    // ═══════════════════════════════════════════════════════════════
    // 左侧：场景组列表
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerPanel::DrawGroupSidebar() {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Scene Groups");
        ImGui::Separator();

        const auto& groups = SceneManager::GetSceneGroups();

        if (groups.empty()) {
            ImGui::TextDisabled("No groups defined.");
            ImGui::TextDisabled("Create one or load from");
            ImGui::TextDisabled("a JSON config file.");
            ImGui::Separator();

            if (ImGui::Button("Load groups.json")) {
                SceneManager::LoadGroupConfig("assets/scenes/scene_groups.json");
            }
            return;
        }

        // 遍历所有场景组，带过滤器
        for (int i = 0; i < static_cast<int>(groups.size()); ++i) {
            const auto& group = groups[i];

            // 过滤器
            if (!PassesFilter(group.groupName) &&
                !PassesFilter(group.masterScene) &&
                !PassesFilter(group.description)) {
                bool anyMatch = false;
                for (const auto& entry : group.subScenes) {
                    if (PassesFilter(entry.sceneName)) {
                        anyMatch = true;
                        break;
                    }
                }
                if (!anyMatch) continue;
            }

            const char* icon = group.isStreaming ? "🌊" : "📦";
            bool isSelected = (i == m_SelectedGroupIndex);

            ImGui::PushID(i);

            // 使用 Selectable 替代 Table 布局
            char label[256];
            snprintf(label, sizeof(label), "%s %s  (%zu scenes)", icon, group.groupName.c_str(), group.subScenes.size() + 1);
            ImVec2 textSize = ImGui::CalcTextSize(label);

            if (ImGui::Selectable(label, &isSelected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 28))) {
                m_SelectedGroupIndex = i;
                LoadSceneGroupToEdit(group.groupName);
            }

            // 在 Selectable 右侧叠加快速切换按钮
            if (ImGui::IsItemHovered()) {
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 8);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 0.6f));
                if (ImGui::SmallButton("▶")) {
                    QuickSwitchToGroup(group.groupName);
                }
                ImGui::PopStyleColor();
            }

            // 右键菜单
            if (ImGui::BeginPopupContextItem("GroupContextMenu")) {
                if (ImGui::MenuItem("Edit")) {
                    m_SelectedGroupIndex = i;
                    LoadSceneGroupToEdit(group.groupName);
                }
                if (ImGui::MenuItem("Quick Switch")) {
                    QuickSwitchToGroup(group.groupName);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Duplicate")) {
                    ENGINE_LOG_INFO("SceneManagerPanel", "Duplicate group '{}' (would add runtime group)", group.groupName);
                }
                if (ImGui::MenuItem("Delete", nullptr, false, true)) {
                    DeleteGroup(group.groupName);
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 中间主区域
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerPanel::DrawMainArea() {
        // 运行时监视器 — 始终绘制在顶部
        DrawRuntimeMonitor();

        // 性能面板
        if (m_ShowPerformance) {
            DrawPerformancePanel();
        }

        // 合规性检查
        if (m_ShowValidation) {
            DrawValidationPanel();
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Scene Group Editor");
        ImGui::Separator();

        // 快速工具
        DrawQuickTools();

        ImGui::Separator();

        if (m_IsEditing) {
            DrawGroupEditor();
        } else {
            ImGui::TextDisabled("Select a scene group from the left panel to edit, or create a new one.");
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 场景组编辑器
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerPanel::DrawGroupEditor() {
        if (!m_IsEditing) return;

        // ── 组属性编辑（使用 char[] 避免 imgui_stdlib 依赖） ──
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Group Name:");
        ImGui::SameLine();
        ImGui::PushItemWidth(300);
        ImGui::InputText("##GroupName", m_EditState.groupName, sizeof(m_EditState.groupName));
        ImGui::PopItemWidth();

        ImGui::Text("Master Scene:");
        ImGui::SameLine();
        ImGui::PushItemWidth(300);
        ImGui::InputText("##MasterScene", m_EditState.masterScene, sizeof(m_EditState.masterScene));
        ImGui::PopItemWidth();

        ImGui::Text("Description:");
        ImGui::SameLine();
        ImGui::PushItemWidth(400);
        ImGui::InputText("##Desc", m_EditState.description, sizeof(m_EditState.description));
        ImGui::PopItemWidth();

        ImGui::Checkbox("Is Streaming Group", &m_EditState.isStreaming);

        ImGui::Separator();

        // ── 子场景列表编辑 ──
        DrawSceneListEditor();

        ImGui::Separator();

        // ── 保存/取消按钮 ──
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        if (ImGui::Button("Save Group", ImVec2(120, 30))) {
            SaveCurrentGroup();
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(80, 30))) {
            m_IsEditing = false;
            m_GroupDirty = false;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 子场景列表编辑器
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerPanel::DrawSceneListEditor() {
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f),
                           "Sub-Scenes (%zu)", m_EditState.entries.size());
        ImGui::SameLine();

        if (ImGui::SmallButton("+ Add Scene")) {
            SceneGroupEntry newEntry;
            newEntry.sceneName = "NewScene_" + std::to_string(m_EditState.entries.size());
            newEntry.required = true;
            newEntry.loadAsync = true;
            m_EditState.entries.push_back(std::move(newEntry));
            m_GroupDirty = true;
        }

        ImGui::Separator();

        if (m_EditState.entries.empty()) {
            ImGui::TextDisabled("No sub-scenes. The grup will only contain the master scene.");
            return;
        }

        // 使用 ImGui 表格展示场景列表
        const int numColumns = 6;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));

        if (ImGui::BeginTable("SceneList", numColumns,
                              ImGuiTableFlags_Reorderable |
                              ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingFixedFit)) {

            // 表头
            ImGui::TableSetupColumn("##Drag",   ImGuiTableColumnFlags_WidthFixed, 20);
            ImGui::TableSetupColumn("Scene Name",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Required",  ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Async",     ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("Priority",  ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("##Delete",  ImGuiTableColumnFlags_WidthFixed, 30);
            ImGui::TableHeadersRow();

            // 行（可拖拽排序）
            for (int i = 0; i < static_cast<int>(m_EditState.entries.size()); ++i) {
                auto& entry = m_EditState.entries[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();

                // 拖拽手柄
                ImGui::TableNextColumn();
                ImGui::Text("≡");

                // 场景名称（使用临时 char[] 缓冲编辑）
                ImGui::TableNextColumn();
                ImGui::PushItemWidth(-1);
                char sceneBuf[256];
                strncpy(sceneBuf, entry.sceneName.c_str(), sizeof(sceneBuf) - 1);
                sceneBuf[sizeof(sceneBuf) - 1] = '\0';
                if (ImGui::InputText("##name", sceneBuf, sizeof(sceneBuf))) {
                    entry.sceneName = sceneBuf;
                }
                ImGui::PopItemWidth();

                // Required 勾选
                ImGui::TableNextColumn();
                ImGui::Checkbox("##req", &entry.required);

                // Async 勾选
                ImGui::TableNextColumn();
                ImGui::Checkbox("##async", &entry.loadAsync);

                // Priority 滑动条
                ImGui::TableNextColumn();
                ImGui::PushItemWidth(-1);
                int priority = entry.loadPriority;
                ImGui::SliderInt("##pri", &priority, 0, 100, "%d");
                entry.loadPriority = priority;
                ImGui::PopItemWidth();

                // 删除按钮
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 0.8f));
                if (ImGui::SmallButton("✕")) {
                    m_EditState.entries.erase(m_EditState.entries.begin() + i);
                    m_GroupDirty = true;
                }
                ImGui::PopStyleColor();

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::PopStyleVar();
    }

    // ═══════════════════════════════════════════════════════════════
    // 运行时监视器
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerPanel::DrawRuntimeMonitor() {
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "⚡ Runtime Monitor");
        ImGui::SameLine();

        // 刷新率
        m_MonitorRefreshTimer += ImGui::GetIO().DeltaTime;
        bool refresh = (m_MonitorRefreshTimer > 0.5f); // 0.5秒刷新一次
        if (refresh) m_MonitorRefreshTimer = 0.0f;

        // 全局加载进度
        if (SceneManager::IsLoading()) {
            ImGui::SameLine();
            float progress = SceneManager::GetLoadProgress();
            ImGui::ProgressBar(progress, ImVec2(150, 0),
                               ("Loading: " + SceneManager::GetCurrentLoadingScene()).c_str());
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled("(idle)");
        }

        ImGui::Separator();

        // 表头
        if (ImGui::BeginTable("RuntimeScenes", 7,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame |
                              ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Scene Name",  ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("State",       ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Progress",    ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableSetupColumn("Objects",     ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Load Time",   ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("Actions",     ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableSetupColumn("Active",      ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableHeadersRow();

            // 获取所有已加载场景
            auto loadedScenes = SceneManager::GetLoadedSceneNames();
            if (loadedScenes.empty()) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("No scenes loaded");
            } else {
                for (const auto& name : loadedScenes) {
                    Level* level = SceneManager::FindLevel(name);
                    if (level) {
                        DrawRuntimeSceneRow(name, level);
                    }
                }
            }

            // 叠加场景（可能不在 loadedScenes 中）
            auto additiveLevels = SceneManager::GetAdditiveLevels();
            for (auto* level : additiveLevels) {
                if (level) {
                    // 避免重复显示
                    bool alreadyShown = false;
                    for (const auto& name : loadedScenes) {
                        if (name == level->GetName()) {
                            alreadyShown = true;
                            break;
                        }
                    }
                    if (!alreadyShown) {
                        DrawRuntimeSceneRow(level->GetName(), level);
                    }
                }
            }

            ImGui::EndTable();
        }
    }

    void SceneManagerPanel::DrawRuntimeSceneRow(const std::string& sceneName, Level* level) {
        if (!level) return;

        LevelState state = level->GetState();
        Scene* scene = level->HasScene() ? level->GetScene() : nullptr;

        ImGui::TableNextRow();

        // 列 1：场景名称
        ImGui::TableNextColumn();
        bool isActive = (SceneManager::GetActiveSceneName() == sceneName);
        if (isActive) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "▶ %s", sceneName.c_str());
        } else {
            ImGui::Text("%s", sceneName.c_str());
        }

        // 列 2：生命周期状态标签
        ImGui::TableNextColumn();
        DrawLifecycleStateTag(state);

        // 列 3：加载进度条
        ImGui::TableNextColumn();
        float loadProgress = level->GetLoadProgress();
        if (state == LevelState::Loading) {
            ImGui::ProgressBar(loadProgress, ImVec2(-1, 0));
        } else if (state == LevelState::Active || state == LevelState::Loaded) {
            ImGui::ProgressBar(1.0f, ImVec2(-1, 0), "100%");
        } else {
            ImGui::TextDisabled("—");
        }

        // 列 4：对象数量
        ImGui::TableNextColumn();
        if (scene) {
            ImGui::Text("%zu", scene->GetTotalObjectCount());
        } else {
            ImGui::TextDisabled("—");
        }

        // 列 5：加载时间（从性能剖面获取）
        ImGui::TableNextColumn();
        bool foundProfile = false;
        for (const auto& profile : m_CachedProfiles) {
            if (profile.sceneName == sceneName && profile.success) {
                ImGui::Text("%.1fms", profile.totalTimeMs);
                foundProfile = true;
                break;
            }
        }
        if (!foundProfile) {
            ImGui::TextDisabled("—");
        }

        // 列 6：操作按钮
        ImGui::TableNextColumn();
        if (state == LevelState::Active || state == LevelState::Loaded) {
            if (ImGui::SmallButton("Unload")) {
                SceneManager::UnloadScene(sceneName);
            }
            ImGui::SameLine();
            if (!isActive) {
                if (ImGui::SmallButton("SetActive")) {
                    SceneManager::SwitchScene(sceneName, SceneContext{});
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Reload")) {
                // SceneContext 不可拷贝，Reload 时不保留旧上下文
                // 调用方可传新上下文到 LoadSceneAsync
                SceneManager::UnloadScene(sceneName);
                SceneManager::LoadSceneAsync(sceneName, SceneContext{});
            }
        } else if (state == LevelState::Unloaded) {
            if (ImGui::SmallButton("Load")) {
                SceneManager::LoadSceneAsync(sceneName, SceneContext{});
            }
        }

        // 列 7：活动标记
        ImGui::TableNextColumn();
        if (isActive) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "✓");
        }
    }

    void SceneManagerPanel::DrawLifecycleStateTag(LevelState state) {
        ImVec4 color = LevelStateToColor(state);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::Text("%s", LevelStateToString(state));
        ImGui::PopStyleColor();
    }

    // ═══════════════════════════════════════════════════════════════
    // 性能分析面板
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerPanel::DrawPerformancePanel() {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "📊 Performance Profile");
        ImGui::Separator();

        if (m_CachedProfiles.empty()) {
            ImGui::TextDisabled("No load profiling data yet. Load a scene to see statistics.");
            return;
        }

        // 最近5次加载的平均耗时
        double avgTime = SceneManager::GetAverageLoadTimeMs(5);
        ImGui::Text("Avg Load Time (last 5): ");
        ImGui::SameLine();
        if (avgTime > 5000.0) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%.1f ms ⚠", avgTime);
        } else if (avgTime > 2000.0) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%.1f ms", avgTime);
        } else {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%.1f ms ✓", avgTime);
        }

        ImGui::SameLine();
        ImGui::TextDisabled("  (threshold: %.0fms)", SceneManager::GetLoadProfiles().empty() ? 5000.0 : 5000.0);

        // 详细列表
        if (ImGui::BeginTable("ProfileTable", 6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn("Scene");
            ImGui::TableSetupColumn("I/O (ms)");
            ImGui::TableSetupColumn("Deserialize");
            ImGui::TableSetupColumn("Init (ms)");
            ImGui::TableSetupColumn("Total (ms)");
            ImGui::TableSetupColumn("Objects");
            ImGui::TableHeadersRow();

            // 只显示最近10条
            int startIdx = std::max(0, static_cast<int>(m_CachedProfiles.size()) - 10);
            for (int i = startIdx; i < static_cast<int>(m_CachedProfiles.size()); ++i) {
                const auto& profile = m_CachedProfiles[i];

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%s", profile.sceneName.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%.1f", profile.ioTimeMs);

                ImGui::TableNextColumn();
                ImGui::Text("%.1f", profile.deserializeTimeMs);

                ImGui::TableNextColumn();
                ImGui::Text("%.1f", profile.initTimeMs);

                // 总耗时（带颜色）
                ImGui::TableNextColumn();
                ImVec4 timeColor = profile.ExceedsThreshold(5000.0)
                    ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                    : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                ImGui::TextColored(timeColor, "%.1f", profile.totalTimeMs);

                ImGui::TableNextColumn();
                ImGui::Text("%zu", profile.objectCount);
            }

            ImGui::EndTable();
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 快速工具
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerPanel::DrawQuickTools() {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.5f, 1.0f), "🔧 Quick Tools");
        ImGui::SameLine();
        if (ImGui::SmallButton("Create New Scene Level")) {
            m_ShowNewSceneDialog = true;
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Load groups.json")) {
            SceneManager::LoadGroupConfig("assets/scenes/scene_groups.json");
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Unload All Non-Persistent")) {
            SceneManager::GetLevelManager().UnloadAllNonPersistent();
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 合规性检查面板
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerPanel::DrawValidationPanel() {
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f), "🔍 Resource Validation");
        ImGui::Separator();

        // 检查当前加载的场景
        auto loadedScenes = SceneManager::GetLoadedSceneNames();
        if (loadedScenes.empty()) {
            ImGui::TextDisabled("No scenes loaded — validation skipped.");
            return;
        }

        bool hasIssues = false;

        for (const auto& name : loadedScenes) {
            Level* level = SceneManager::FindLevel(name);
            if (!level || !level->HasScene()) continue;

            Scene* scene = level->GetScene();
            ImGui::Text("Checking: %s", name.c_str());
            ImGui::Indent(16.0f);

            // 1. 检查是否有 MainCamera — 根据场景属性预估
            // 实际项目中应扫描所有 GameObject
            size_t objCount = scene->GetTotalObjectCount();
            if (objCount == 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "⚠ No objects in scene");
                hasIssues = true;
            }

            // 2. 场景属性检查
            const auto& props = scene->GetProperties();
            if (props.ambientColor.x == 0.0f && props.ambientColor.y == 0.0f && props.ambientColor.z == 0.0f) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "✗ Ambient color is black (may be uninitialized)");
                hasIssues = true;
            }

            if (props.gravity.x == 0.0f && props.gravity.y == 0.0f) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "⚠ Gravity is zero");
                hasIssues = true;
            }

            // 3. 检查是否存在场景上下文
            const SceneContext* ctx = SceneManager::GetCurrentContext();
            if (ctx && ctx->GetSceneName() == name) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "✓ SceneContext available (%zu args)", ctx->GetArgCount());
            } else {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "ℹ No SceneContext for this scene");
            }

            // 4. 检查性能
            for (const auto& profile : m_CachedProfiles) {
                if (profile.sceneName == name) {
                    if (profile.ExceedsThreshold(5000.0)) {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                                           "✗ Load time %.0fms exceeds threshold", profile.totalTimeMs);
                        hasIssues = true;
                    } else {
                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
                                           "✓ Load time %.0fms (within threshold)", profile.totalTimeMs);
                    }
                    break;
                }
            }

            ImGui::Unindent(16.0f);
        }

        if (!hasIssues && !loadedScenes.empty()) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "✓ All checks passed — no issues found.");
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 底部日志
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerPanel::DrawFooter() {
        ImGui::Separator();

        // 状态栏
        ImGui::BeginChild("FooterBar", ImVec2(0, 24), false);

        // 左侧：当前操作状态
        if (SceneManager::IsLoading()) {
            ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f),
                               "🔄 Loading '%s'... %.1f%%",
                               SceneManager::GetCurrentLoadingScene().c_str(),
                               SceneManager::GetLoadProgress() * 100.0f);
        } else if (m_GroupDirty) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                               "⚠ Unsaved changes in '%s'", m_EditState.groupName);
        } else {
            ImGui::TextDisabled("Ready");
        }

        // 右侧：统计
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 250);
        auto groups = SceneManager::GetSceneGroups();
        auto loadedScenes = SceneManager::GetLoadedSceneNames();
        ImGui::TextDisabled("Groups: %zu | Loaded: %zu | Profiles: %zu",
                            groups.size(), loadedScenes.size(), m_CachedProfiles.size());

        ImGui::EndChild();
    }

    // ═══════════════════════════════════════════════════════════════
    // 内部操作实现
    // ═══════════════════════════════════════════════════════════════

    void SceneManagerPanel::LoadSceneGroupToEdit(const std::string& groupName) {
        const auto& groups = SceneManager::GetSceneGroups();
        for (const auto& group : groups) {
            if (group.groupName == groupName) {
                // char[] 必须用 strncpy 赋值
                strncpy(m_EditState.groupName, group.groupName.c_str(), sizeof(m_EditState.groupName) - 1);
                m_EditState.groupName[sizeof(m_EditState.groupName) - 1] = '\0';
                strncpy(m_EditState.masterScene, group.masterScene.c_str(), sizeof(m_EditState.masterScene) - 1);
                m_EditState.masterScene[sizeof(m_EditState.masterScene) - 1] = '\0';
                strncpy(m_EditState.description, group.description.c_str(), sizeof(m_EditState.description) - 1);
                m_EditState.description[sizeof(m_EditState.description) - 1] = '\0';
                m_EditState.isStreaming  = group.isStreaming;
                m_EditState.entries      = group.subScenes;
                m_IsEditing = true;
                m_GroupDirty = false;
                return;
            }
        }
    }

    void SceneManagerPanel::SaveCurrentGroup() {
        // 验证（char[] 用索引 0 判空）
        if (m_EditState.groupName[0] == '\0') {
            ENGINE_LOG_WARN("SceneManagerPanel", "Cannot save group with empty name");
            return;
        }

        // 序列化为 JSON 并保存到文件（char[] 自动退化为 const char*）
        SceneGroup group;
        group.groupName    = m_EditState.groupName;
        group.masterScene  = m_EditState.masterScene;
        group.description  = m_EditState.description;
        group.isStreaming  = m_EditState.isStreaming;
        group.subScenes    = m_EditState.entries;

        SceneGroupConfig config;
        config.groups = SceneManager::GetSceneGroups();

        // 更新或添加
        bool found = false;
        for (auto& g : config.groups) {
            if (g.groupName == group.groupName) {
                g = group;
                found = true;
                break;
            }
        }
        if (!found) {
            config.groups.push_back(group);
        }

        // 写回文件
        std::string json = config.ToJson();
        std::string filePath = "assets/scenes/scene_groups.json";

        std::ofstream file(filePath);
        if (file.is_open()) {
            file << json;
            file.close();
            ENGINE_LOG_INFO("SceneManagerPanel", "Saved scene group '{}' to {}", group.groupName, filePath);

            // 重新加载配置
            SceneManager::LoadGroupConfig(filePath);
            m_GroupDirty = false;
        } else {
            ENGINE_LOG_ERROR("SceneManagerPanel", "Failed to save group config to {}", filePath);
        }
    }

    void SceneManagerPanel::DeleteGroup(const std::string& groupName) {
        // 当前 SceneManager 不支持动态删除组
        // 实际项目应提供 RemoveGroup API
        ENGINE_LOG_WARN("SceneManagerPanel", "Delete group '{}' — not yet implemented via SceneManager API. ",
                        groupName);

        // 备选：直接修改 JSON 文件
        auto config = SceneGroupConfig::LoadFromFile("assets/scenes/scene_groups.json");
        auto it = std::remove_if(config.groups.begin(), config.groups.end(),
                                  [&groupName](const SceneGroup& g) { return g.groupName == groupName; });
        if (it != config.groups.end()) {
            config.groups.erase(it, config.groups.end());
            std::string json = config.ToJson();
            std::ofstream file("assets/scenes/scene_groups.json");
            if (file.is_open()) {
                file << json;
                file.close();
                SceneManager::LoadGroupConfig("assets/scenes/scene_groups.json");
                ENGINE_LOG_INFO("SceneManagerPanel", "Deleted group '{}'", groupName);
                m_IsEditing = false;
                m_SelectedGroupIndex = -1;
            }
        }
    }

    void SceneManagerPanel::SyncBuildSettings() {
        ENGINE_LOG_INFO("SceneManagerPanel", "Syncing all scene group scenes to Build Settings...");

        const auto& groups = SceneManager::GetSceneGroups();
        std::vector<std::string> allScenes;

        for (const auto& group : groups) {
            // 添加主场景
            if (!group.masterScene.empty()) {
                if (std::find(allScenes.begin(), allScenes.end(), group.masterScene) == allScenes.end()) {
                    allScenes.push_back(group.masterScene);
                }
            }
            // 添加子场景
            for (const auto& entry : group.subScenes) {
                if (!entry.sceneName.empty()) {
                    if (std::find(allScenes.begin(), allScenes.end(), entry.sceneName) == allScenes.end()) {
                        allScenes.push_back(entry.sceneName);
                    }
                }
            }
        }

        ENGINE_LOG_INFO("SceneManagerPanel", "Found %zu unique scenes across %zu groups",
                        allScenes.size(), groups.size());

        // 写入场景索引文件
        std::ofstream file("assets/scenes/build_scenes.txt");
        if (file.is_open()) {
            for (const auto& scene : allScenes) {
                file << scene << std::endl;
            }
            file.close();
            ENGINE_LOG_INFO("SceneManagerPanel", "Wrote %zu scenes to assets/scenes/build_scenes.txt", allScenes.size());
        }

        // 输出到日志方便 CMake/构建系统读取
        for (const auto& scene : allScenes) {
            ENGINE_LOG_DEBUG("SceneManagerPanel", "  Build Scene: {}", scene);
        }
    }

    void SceneManagerPanel::QuickSwitchToGroup(const std::string& groupName) {
        ENGINE_LOG_INFO("SceneManagerPanel", "Quick switching to group '{}'", groupName);
        SceneManager::SwitchSceneAsync(groupName, SceneContext::Create(groupName));
    }

    void SceneManagerPanel::CreateNewSceneLevelDialog() {
        ImGui::OpenPopup("Create New Scene Level");

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_Appearing);

        if (ImGui::BeginPopupModal("Create New Scene Level", &m_ShowNewSceneDialog,
                                    ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped("This will create a new scene file, folder, and configuration entry.");

            ImGui::Separator();

            ImGui::Text("Scene Name:");
            ImGui::InputText("##NewSceneName", m_NewSceneName, sizeof(m_NewSceneName));

            ImGui::Separator();

            if (ImGui::Button("Create", ImVec2(120, 0))) {
                std::string name(m_NewSceneName);
                if (!name.empty()) {
                    // 自动创建场景目录和文件
                    std::string sceneDir = "assets/scenes/levels/" + name;
                    std::string sceneFile = sceneDir + "/" + name + ".scene";

                    // 创建目录
                    std::string mkdirCmd = "mkdir -p " + sceneDir;
                    system(mkdirCmd.c_str());

                    // 创建基础场景 JSON
                    nlohmann::json sceneJson;
                    sceneJson["scene"]["name"] = name;
                    sceneJson["scene"]["properties"] = nlohmann::json::object();
                    sceneJson["scene"]["objects"] = nlohmann::json::array();

                    std::ofstream file(sceneFile);
                    if (file.is_open()) {
                        file << sceneJson.dump(4);
                        file.close();
                    }

                    // 注册场景
                    LevelInfo info;
                    info.name = name;
                    info.filePath = sceneFile;
                    info.category = LevelCategory::Main;
                    SceneManager::RegisterLevel(info);

                    // 添加到当前编辑组
                    SceneGroupEntry entry;
                    entry.sceneName = name;
                    entry.required = true;
                    entry.loadAsync = true;
                    m_EditState.entries.push_back(std::move(entry));
                    m_GroupDirty = true;

                    ENGINE_LOG_INFO("SceneManagerPanel", "Created new scene level: '{}' at {}", name, sceneFile);
                    m_ShowNewSceneDialog = false;
                    m_NewSceneName[0] = '\0';
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(80, 0))) {
                m_ShowNewSceneDialog = false;
                m_NewSceneName[0] = '\0';
            }

            ImGui::EndPopup();
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 搜索过滤
    // ═══════════════════════════════════════════════════════════════

    bool SceneManagerPanel::PassesFilter(const std::string& name) const {
        if (m_SearchBuffer[0] == '\0') return true; // 无过滤

        std::string query(m_SearchBuffer);
        std::transform(query.begin(), query.end(), query.begin(), ::tolower);

        std::string lowerName = name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

        return lowerName.find(query) != std::string::npos;
    }

} // namespace Engine