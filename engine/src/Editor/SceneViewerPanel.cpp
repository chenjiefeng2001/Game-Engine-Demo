#include "Engine/Editor/SceneViewerPanel.h"
#include "Engine/Core/Scene/SceneManager.h"
#include "Engine/Core/Scene/SceneContext.h"
#include "Engine/Core/Scene/SceneTypes.h"
#include "Engine/Core/Level/Level.h"
#include "Engine/Core/Level/LevelManager.h"
#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Core/RHI/ISceneGraph.h"
#include "Engine/Editor/EditorCamera.h"
#include "Engine/Core/Log.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>

namespace Engine {

    // ── 场景颜色方案（不同类别不同色系） ──
    const Vec4 SceneViewerPanel::k_SceneColors[8] = {
        Vec4(0.2f, 0.8f, 0.2f, 0.6f),  // Green  — Persistent
        Vec4(0.2f, 0.6f, 1.0f, 0.6f),  // Blue   — Environment
        Vec4(1.0f, 0.6f, 0.2f, 0.6f),  // Orange — Gameplay
        Vec4(0.8f, 0.2f, 0.8f, 0.6f),  // Purple — UI
        Vec4(1.0f, 0.8f, 0.2f, 0.6f),  // Yellow — Cinematic
        Vec4(0.8f, 0.3f, 0.3f, 0.6f),  // Red    — Debug
        Vec4(0.5f, 0.5f, 0.5f, 0.6f),  // Gray
        Vec4(0.8f, 0.8f, 0.8f, 0.6f),  // White
    };

    // ═══════════════════════════════════════════════════════════════
    // 构造
    // ═══════════════════════════════════════════════════════════════

    SceneViewerPanel::SceneViewerPanel() {
        m_SearchBuffer[0] = '\0';
    }

    // ═══════════════════════════════════════════════════════════════
    // 主动刷新场景数据
    // ═══════════════════════════════════════════════════════════════

    void SceneViewerPanel::RefreshSceneData() {
        // 清空旧数据
        for (auto& [cat, entries] : m_SceneEntries) {
            entries.clear();
        }
        m_SceneEntries.clear();
        m_OverlayCache.clear();

        // 获取所有已加载场景
        auto loadedNames = SceneManager::GetLoadedSceneNames();
        
        // 获取叠加场景
        auto additiveLevels = SceneManager::GetAdditiveLevels();
        for (auto* lvl : additiveLevels) {
            if (lvl && std::find(loadedNames.begin(), loadedNames.end(), lvl->GetName()) == loadedNames.end()) {
                loadedNames.push_back(lvl->GetName());
            }
        }

        if (loadedNames.empty()) return;

        // 获取性能数据
        const auto& profiles = SceneManager::GetLoadProfiles();

        Vec3 cameraPos(0, 0, 0); // EditorCamera 位置在 OnOverlay 时更新

        // 遍历所有加载的场景
        for (const auto& name : loadedNames) {
            Level* level = SceneManager::FindLevel(name);
            if (!level) continue;

            SceneViewerEntry entry;
            entry.level       = level;
            entry.displayName = name;
            entry.visible     = true;
            entry.solo        = (m_SoloModeActive && m_SoloSceneName == name);

            // 对象计数
            if (level->HasScene()) {
                entry.objectCount = (uint32)level->GetScene()->GetTotalObjectCount();
                
                // 计算场景包围盒（遍历所有 GameObject）
                const auto& objects = level->GetScene()->GetObjects();
                AABB sceneBounds;
                for (const auto& obj : objects) {
                    if (!obj || !obj->IsActive()) continue;
                    const auto& transform = obj->GetTransform();
                    Vec3 pos = transform.GetPosition();
                    sceneBounds.Expand(pos);
                    // 简单近似：每个对象周围 0.5m 范围
                    sceneBounds.Expand(pos + Vec3(0.5f, 0.5f, 0.5f));
                    sceneBounds.Expand(pos - Vec3(0.5f, 0.5f, 0.5f));
                }
                entry.bounds = sceneBounds;
                entry.center = sceneBounds.Center();
            }

            // 从性能剖面获取加载耗时
            for (const auto& profile : profiles) {
                if (profile.sceneName == name) {
                    entry.totalTimeMs       = profile.totalTimeMs;
                    entry.ioTimeMs          = profile.ioTimeMs;
                    entry.deserializeTimeMs = profile.deserializeTimeMs;
                    entry.initTimeMs        = profile.initTimeMs;
                    break;
                }
            }

            // 查找场景所属类别
            SceneCategoryGroup category = CategorizeScene(name);
            m_SceneEntries[category].push_back(entry);

            // 缓存叠加数据
            uint8 colorIdx = static_cast<uint8>(category);
            if (colorIdx >= 8) colorIdx = 7;
            m_OverlayCache.push_back({ entry.center, entry.bounds, k_SceneColors[colorIdx], name, 0.0f });
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 场景分类启发式规则
    // ═══════════════════════════════════════════════════════════════

    SceneCategoryGroup SceneViewerPanel::CategorizeScene(const std::string& name) const {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // 关键字匹配
        if (lower.find("persistent") != std::string::npos ||
            lower.find("global") != std::string::npos ||
            lower.find("manager") != std::string::npos ||
            lower.find("system") != std::string::npos)
            return SceneCategoryGroup::Persistent;

        if (lower.find("env") != std::string::npos ||
            lower.find("terrain") != std::string::npos ||
            lower.find("nature") != std::string::npos ||
            lower.find("foliage") != std::string::npos ||
            lower.find("sky") != std::string::npos ||
            lower.find("water") != std::string::npos)
            return SceneCategoryGroup::Environment;

        if (lower.find("ui") != std::string::npos ||
            lower.find("menu") != std::string::npos ||
            lower.find("hud") != std::string::npos ||
            lower.find("overlay") != std::string::npos)
            return SceneCategoryGroup::UI;

        if (lower.find("cinematic") != std::string::npos ||
            lower.find("cutscene") != std::string::npos ||
            lower.find("intro") != std::string::npos)
            return SceneCategoryGroup::Cinematic;

        if (lower.find("debug") != std::string::npos ||
            lower.find("test") != std::string::npos)
            return SceneCategoryGroup::Debug;

        // 默认归类为 Gameplay
        return SceneCategoryGroup::Gameplay;
    }

    // ═══════════════════════════════════════════════════════════════
    // 主渲染入口
    // ═══════════════════════════════════════════════════════════════

    void SceneViewerPanel::OnImGui() {
        if (!m_Visible) return;

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        if (!ImGui::Begin("Scene Viewer", &m_Visible, flags)) {
            ImGui::End();
            return;
        }

        // 定时刷新（每秒 2 次）
        m_RefreshTimer += ImGui::GetIO().DeltaTime;
        if (m_RefreshTimer > 0.5f || m_SceneEntries.empty()) {
            RefreshSceneData();
            m_RefreshTimer = 0.0f;
        }

        // 工具栏
        DrawToolbar();

        // 主布局：左侧层级树 + 右侧热力图
        ImGui::BeginGroup();

        // ── 左侧：场景层级树 ──
        ImGui::BeginChild("HierarchyTree", ImVec2(ImGui::GetContentRegionAvail().x * 0.55f, 0),
                          true, ImGuiWindowFlags_HorizontalScrollbar);
        DrawHierarchyTree();
        ImGui::EndChild();

        ImGui::SameLine();

        // ── 右侧：运行时热力图 ──
        ImGui::BeginChild("HeatmapPanel", ImVec2(0, 0), true);
        if (m_ShowHeatmap) {
            DrawHeatmapPanel();
        } else {
            ImGui::TextDisabled("Heatmap disabled — toggle in toolbar");
        }
        ImGui::EndChild();

        ImGui::EndGroup();

        ImGui::End();
    }

    // ═══════════════════════════════════════════════════════════════
    // 工具栏
    // ═══════════════════════════════════════════════════════════════

    void SceneViewerPanel::DrawToolbar() {
        // 搜索
        ImGui::PushItemWidth(180.0f);
        ImGui::InputTextWithHint("##Search", "Search scenes...", m_SearchBuffer, sizeof(m_SearchBuffer));
        ImGui::PopItemWidth();

        ImGui::SameLine();

        // 显示开关
        ImGui::Checkbox("Heatmap", &m_ShowHeatmap);
        ImGui::SameLine();
        ImGui::Checkbox("Bounds",  &m_ShowBounds);
        ImGui::SameLine();
        ImGui::Checkbox("Dist",    &m_ShowDistances);

        ImGui::SameLine();

        // 清除高亮
        if (ImGui::SmallButton("Clear Highlights")) {
            ClearHighlights();
        }

        ImGui::SameLine();

        // 退出 Solo 模式
        if (m_SoloModeActive) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Exit Solo Mode")) {
                m_SoloModeActive = false;
                m_SoloSceneName.clear();
                RefreshSceneData();
            }
            ImGui::PopStyleColor();
        }

        ImGui::Separator();
    }

    // ═══════════════════════════════════════════════════════════════
    // 场景层级树
    // ═══════════════════════════════════════════════════════════════

    void SceneViewerPanel::DrawHierarchyTree() {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Scene Hierarchy");
        ImGui::Separator();

        // 遍历所有分类组
        static const SceneCategoryGroup categories[] = {
            SceneCategoryGroup::Persistent,
            SceneCategoryGroup::Environment,
            SceneCategoryGroup::Gameplay,
            SceneCategoryGroup::UI,
            SceneCategoryGroup::Cinematic,
            SceneCategoryGroup::Debug
        };

        bool hasAny = false;
        for (auto cat : categories) {
            auto it = m_SceneEntries.find(cat);
            if (it == m_SceneEntries.end() || it->second.empty()) continue;
            hasAny = true;
            DrawCategoryGroup(cat, it->second);
        }

        if (!hasAny) {
            ImGui::TextDisabled("No scenes loaded.");
            return;
        }
    }

    void SceneViewerPanel::DrawCategoryGroup(SceneCategoryGroup category,
                                              const std::vector<SceneViewerEntry>& entries) {
        uint8 colorIdx = static_cast<uint8>(category);
        if (colorIdx >= 8) colorIdx = 7;
        const Vec4& color = k_SceneColors[colorIdx];

        char groupLabel[128];
        snprintf(groupLabel, sizeof(groupLabel), "%s (%zu)###cat_%d",
                 SceneCategoryGroupName(category), entries.size(), (int)category);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color.x, color.y, color.z, 1.0f));
        bool treeOpen = ImGui::TreeNodeEx(groupLabel, ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor();

        if (!treeOpen) return;

        // 搜索过滤
        bool hasFilter = (m_SearchBuffer[0] != '\0');

        for (const auto& entry : entries) {
            // 搜索过滤
            if (hasFilter) {
                std::string query(m_SearchBuffer);
                std::transform(query.begin(), query.end(), query.begin(), ::tolower);
                std::string lowerName = entry.displayName;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                if (lowerName.find(query) == std::string::npos) continue;
            }

            // Solo 模式过滤
            if (m_SoloModeActive && !entry.solo) continue;

            DrawSceneEntry(entry);
        }

        ImGui::TreePop();
    }

    void SceneViewerPanel::DrawSceneEntry(const SceneViewerEntry& entry) {
        // 确定状态图标
        const char* icon = "📦";
        if (entry.level) {
            const auto& info = entry.level->GetInfo();
            switch (info.category) {
                case LevelCategory::Persistent: icon = "📌"; break;
                case LevelCategory::Streaming:  icon = "🌊"; break;
                case LevelCategory::Cinematic:  icon = "🎬"; break;
                case LevelCategory::Debug:      icon = "🐛"; break;
                default: break;
            }
        }

        // 状态标签
        bool isActive = (SceneManager::GetActiveSceneName() == entry.displayName);
        bool isHighlighted = (m_HighlightedScenes.find(entry.displayName) != m_HighlightedScenes.end());

        // 行背景
        if (isHighlighted) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(60, 100, 60, 80));
        }

        ImGui::PushID(entry.displayName.c_str());

        // 眼睛图标（可见性切换）
        if (ImGui::SmallButton(entry.visible ? "👁" : "👁‍🗨")) {
            // 切换可见性（实际项目应更新场景的 LayerMask）
            // 这里只切换条目状态
            // m_SceneEntries 的修改需要通过非 const 引用
        }
        ImGui::SameLine();

        // 场景名（高亮状态）
        if (isActive) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s %s", icon, entry.displayName.c_str());
        } else if (isHighlighted) {
            ImGui::TextColored(ImVec4(0.8f, 0.9f, 0.5f, 1.0f), "%s %s", icon, entry.displayName.c_str());
        } else {
            ImGui::Text("%s %s", icon, entry.displayName.c_str());
        }

        // 右键菜单
        if (ImGui::BeginPopupContextItem("SceneEntryContext")) {
            if (ImGui::MenuItem("Focus")) {
                FocusOnScene(entry.displayName);
            }
            if (ImGui::MenuItem("Solo Mode", nullptr, false, !m_SoloModeActive)) {
                m_SoloModeActive = true;
                m_SoloSceneName = entry.displayName;
            }
            if (ImGui::MenuItem("Select in Manager")) {
                if (m_OnSceneSelect) m_OnSceneSelect(entry.displayName);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Alone")) {
                ENGINE_LOG_INFO("SceneViewer", "Save Alone: '{}'", entry.displayName);
            }
            if (ImGui::MenuItem("Extract to New Scene")) {
                ENGINE_LOG_INFO("SceneViewer", "Extract Selected to New Scene");
            }
            if (ImGui::MenuItem("Move to Top")) {
                ENGINE_LOG_INFO("SceneViewer", "Move '{}' to Top", entry.displayName);
            }
            ImGui::EndPopup();
        }

        // 双击聚焦
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            FocusOnScene(entry.displayName);
        }

        // Solo 标签
        if (entry.solo) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[SOLO]");
        }

        ImGui::PopID();
    }

    // ═══════════════════════════════════════════════════════════════
    // 运行时资源热力图
    // ═══════════════════════════════════════════════════════════════

    void SceneViewerPanel::DrawHeatmapPanel() {
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.8f, 1.0f), "🔥 Runtime Resource Heatmap");
        ImGui::Separator();

        // 收集所有条目
        std::vector<SceneViewerEntry> allEntries;
        float maxMemory = 0.0f;
        uint32 maxObjects = 0;
        double maxLoadTime = 0.0;

        for (const auto& [cat, entries] : m_SceneEntries) {
            for (const auto& e : entries) {
                allEntries.push_back(e);
                if (e.memoryUsageMB > maxMemory) maxMemory = e.memoryUsageMB;
                if (e.objectCount > maxObjects) maxObjects = e.objectCount;
                if (e.totalTimeMs > maxLoadTime) maxLoadTime = e.totalTimeMs;
            }
        }

        if (allEntries.empty()) {
            ImGui::TextDisabled("No loaded scenes to display.");
            return;
        }

        // 表头
        if (ImGui::BeginTable("HeatmapTable", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn("Scene");
            ImGui::TableSetupColumn("Memory");
            ImGui::TableSetupColumn("Objects");
            ImGui::TableSetupColumn("Load Time");
            ImGui::TableSetupColumn("Distance");
            ImGui::TableHeadersRow();

            for (const auto& entry : allEntries) {
                ImGui::TableNextRow();

                // 列 1：场景名
                ImGui::TableNextColumn();
                bool isActive = (SceneManager::GetActiveSceneName() == entry.displayName);
                if (isActive) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s", entry.displayName.c_str());
                } else {
                    ImGui::Text("%s", entry.displayName.c_str());
                }

                // 列 2：内存占用条
                ImGui::TableNextColumn();
                float memFrac = (maxMemory > 0.0f) ? (entry.memoryUsageMB / maxMemory) : 0.0f;
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));
                // 格式化内存字符串
                char memBuf[64];
                if (entry.memoryUsageMB > 0.0f) {
                    snprintf(memBuf, sizeof(memBuf), "%.1f MB", entry.memoryUsageMB);
                } else {
                    snprintf(memBuf, sizeof(memBuf), "—");
                }
                ImGui::ProgressBar(memFrac, ImVec2(-1, 0), memBuf);
                ImGui::PopStyleColor();

                // 列 3：对象数（带颜色）
                ImGui::TableNextColumn();
                if (entry.objectCount > 0) {
                    ImVec4 objColor = (entry.objectCount > 500)
                        ? ImVec4(1.0f, 0.5f, 0.3f, 1.0f)
                        : (entry.objectCount > 100
                           ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f)
                           : ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
                    ImGui::TextColored(objColor, "%u", entry.objectCount);
                } else {
                    ImGui::TextDisabled("—");
                }

                // 列 4：加载耗时
                ImGui::TableNextColumn();
                if (entry.totalTimeMs > 0.0) {
                    ImVec4 timeColor = (entry.totalTimeMs > 5000.0)
                        ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                        : (entry.totalTimeMs > 2000.0
                           ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f)
                           : ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
                    ImGui::TextColored(timeColor, "%.1f ms", entry.totalTimeMs);
                } else {
                    ImGui::TextDisabled("—");
                }

                // 列 5：摄像机距离
                ImGui::TableNextColumn();
                if (m_ShowDistances && entry.distanceToCamera > 0.0f) {
                    ImGui::Text("%.1f m", entry.distanceToCamera);
                } else {
                    ImGui::TextDisabled("—");
                }
            }

            ImGui::EndTable();
        }

        // 底部图例
        ImGui::Separator();
        ImGui::TextDisabled("Objects: ");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "<100  ");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "100-500  ");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), ">500  ");
        ImGui::SameLine();
        ImGui::TextDisabled("  |  Load: ");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "<2s  ");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "2-5s  ");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), ">5s");
    }

    // ═══════════════════════════════════════════════════════════════
    // 3D 视口叠加层
    // ═══════════════════════════════════════════════════════════════

    void SceneViewerPanel::OnOverlay(float32 dt, const EditorCamera* camera) {
        if (!m_Visible || !m_ShowBounds || m_OverlayCache.empty()) return;

        // 更新摄像机距离 — 从 EditorCamera 获取位置
        Vec3 camPos(0, 0, 0);
        if (camera) {
            camPos = camera->GetPosition();  // EditorCamera 提供 GetPosition()
        }

        // 为叠加层更新距离
        for (auto& cache : m_OverlayCache) {
            Vec3 diff = cache.sceneCenter - camPos;
            cache.distance = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
        }

        // 排序：按距离（远处先画）
        std::sort(m_OverlayCache.begin(), m_OverlayCache.end(),
                  [](const OverlayCache& a, const OverlayCache& b) {
                      return a.distance > b.distance;
                  });

        // 获取 ImGui 的绘制列表（在视口窗口中）
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (!drawList) return;

        // 视口大小
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        ImVec2 viewportPos = ImGui::GetCursorScreenPos();

        for (const auto& cache : m_OverlayCache) {
            Vec4 color = cache.color;

            // Solo 模式：非 solo 场景淡化
            if (m_SoloModeActive) {
                bool isSolo = (cache.name == m_SoloSceneName);
                color.w = isSolo ? 0.8f : 0.1f;
            }

            // 高亮场景增亮
            if (m_HighlightedScenes.find(cache.name) != m_HighlightedScenes.end()) {
                color = Vec4(1.0f, 1.0f, 0.5f, 0.8f);
            }

            // 在 2D 视口中绘制标签（3D 投影简化版本）
            // 实际项目中应使用 WorldToScreen 变换
            // 这里用简化的距离-大小映射
            float distScale = std::max(0.1f, 1.0f - cache.distance / 200.0f);
            if (distScale < 0.05f) continue; // 太远不画

            // 为简化，只在视口特定位置显示标签
            // 完整实现需要用投影矩阵转换 3D 坐标到屏幕
            ImU32 colorU32 = IM_COL32(
                (int)(color.x * 255),
                (int)(color.y * 255),
                (int)(color.z * 255),
                (int)(color.w * 255 * m_BoundsOpacity)
            );

            // 在视口左上角叠加场景名（简化：真实项目应 3D 投影）
            char label[128];
            snprintf(label, sizeof(label), "%s (%.1fm)", cache.name.c_str(), cache.distance);
            drawList->AddText(
                ImVec2(viewportPos.x + 10, viewportPos.y + 10 + m_NextColorIndex * 16),
                colorU32, label
            );
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 联动接口
    // ═══════════════════════════════════════════════════════════════

    void SceneViewerPanel::HighlightGroup(const std::string& groupName) {
        m_HighlightedScenes.clear();

        const auto& groups = SceneManager::GetSceneGroups();
        for (const auto& group : groups) {
            if (group.groupName == groupName) {
                // 高亮主场景
                if (!group.masterScene.empty()) {
                    m_HighlightedScenes.insert(group.masterScene);
                }
                // 高亮子场景
                for (const auto& entry : group.subScenes) {
                    m_HighlightedScenes.insert(entry.sceneName);
                }
                break;
            }
        }
    }

    void SceneViewerPanel::FocusOnScene(const std::string& sceneName) {
        if (m_OnFocusRequest) {
            // 从缓存中查找场景中心
            for (const auto& cache : m_OverlayCache) {
                if (cache.name == sceneName) {
                    float radius = (cache.sceneBounds.IsValid())
                        ? std::sqrt(
                            (cache.sceneBounds.max.x - cache.sceneBounds.min.x) *
                            (cache.sceneBounds.max.x - cache.sceneBounds.min.x) +
                            (cache.sceneBounds.max.y - cache.sceneBounds.min.y) *
                            (cache.sceneBounds.max.y - cache.sceneBounds.min.y) +
                            (cache.sceneBounds.max.z - cache.sceneBounds.min.z) *
                            (cache.sceneBounds.max.z - cache.sceneBounds.min.z)
                          ) * 0.5f
                        : 10.0f;
                    m_OnFocusRequest(cache.sceneCenter, radius);
                    return;
                }
            }
        }
    }

    void SceneViewerPanel::ClearHighlights() {
        m_HighlightedScenes.clear();
    }

} // namespace Engine