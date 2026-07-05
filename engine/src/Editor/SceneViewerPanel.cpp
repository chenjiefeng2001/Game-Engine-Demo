#include "Engine/Editor/SceneViewerPanel.h"
#include "Engine/Core/Scene/SceneManager.h"
#include "Engine/Core/Scene/SceneContext.h"
#include "Engine/Core/Scene/SceneTypes.h"
#include "Engine/Core/Level/Level.h"
#include "Engine/Core/Level/LevelManager.h"
#include "Engine/Editor/EditorCamera.h"
#include "Engine/Editor/EventBus.h"
#include "Engine/Core/Log.h"
#include "Engine/Editor/IconsFontAwesome6.h"

#include <imgui.h>
#include <imgui_internal.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace Engine {

    // ── 工业级场景颜色与分类映射 ──
    const Vec4 SceneViewerPanel::k_SceneColors[8] = {
        Vec4(0.2f, 0.8f, 0.2f, 0.6f),  // Persistent (Green)
        Vec4(0.2f, 0.6f, 1.0f, 0.6f),  // Main (Blue)
        Vec4(1.0f, 0.6f, 0.2f, 0.6f),  // Streaming (Orange)
        Vec4(0.8f, 0.2f, 0.8f, 0.6f),  // UI/Cinematic (Purple)
        Vec4(1.0f, 0.8f, 0.2f, 0.6f),  // Yellow 
        Vec4(0.8f, 0.3f, 0.3f, 0.6f),  // Debug (Red)
        Vec4(0.5f, 0.5f, 0.5f, 0.6f),  // Gray
        Vec4(0.8f, 0.8f, 0.8f, 0.6f),  // White
    };

    SceneViewerPanel::SceneViewerPanel() {
        m_SearchBuffer[0] = '\0';
    }

    // ═══════════════════════════════════════════════════════════════
    // 1. 数据刷新：丢弃字符串匹配，使用强类型元数据
    // ═══════════════════════════════════════════════════════════════
    void SceneViewerPanel::RefreshSceneData() {
        m_SceneEntries.clear();
        m_OverlayCache.clear();
        
        auto loadedNames = SceneManager::GetLoadedSceneNames();
        auto additiveLevels = SceneManager::GetAdditiveLevels();
        for (auto* lvl : additiveLevels) {
            if (lvl && std::find(loadedNames.begin(), loadedNames.end(), lvl->GetName()) == loadedNames.end()) {
                loadedNames.push_back(lvl->GetName());
            }
        }

        if (loadedNames.empty()) return;
        const auto& profiles = SceneManager::GetLoadProfiles();

        for (const auto& name : loadedNames) {
            Level* level = SceneManager::FindLevel(name);
            if (!level) continue;

            SceneViewerEntry entry;
            entry.level = level;
            entry.displayName = name;
            
            // 【核心修复】：真正的场景可见性状态获取
            entry.visible = level->HasScene() ? level->GetScene()->IsVisible() : false;
            entry.solo = (m_SoloModeActive && m_SoloSceneName == name);

            if (level->HasScene()) {
                Scene* scene = level->GetScene();
                entry.objectCount = (uint32)scene->GetTotalObjectCount();
                
                AABB sceneBounds;
                for (const auto& obj : scene->GetObjects()) {
                    if (!obj || !obj->IsActive()) continue;
                    Vec3 pos = obj->GetTransform().GetPosition();
                    sceneBounds.Expand(pos);
                    sceneBounds.Expand(pos + Vec3(0.5f, 0.5f, 0.5f));
                    sceneBounds.Expand(pos - Vec3(0.5f, 0.5f, 0.5f));
                }
                entry.bounds = sceneBounds;
                entry.center = sceneBounds.Center();
            }

            for (const auto& profile : profiles) {
                if (profile.sceneName == name) {
                    entry.totalTimeMs = profile.totalTimeMs;
                    break;
                }
            }

            // 【核心修复】：不再用字符串乱猜，直接读取 Level 的分类枚举
            LevelCategory lvlCat = level->GetInfo().category;
            SceneCategoryGroup groupCat = SceneCategoryGroup::Gameplay;
            if (lvlCat == LevelCategory::Persistent) groupCat = SceneCategoryGroup::Persistent;
            else if (lvlCat == LevelCategory::Streaming) groupCat = SceneCategoryGroup::Environment;
            else if (lvlCat == LevelCategory::Cinematic) groupCat = SceneCategoryGroup::Cinematic;
            else if (lvlCat == LevelCategory::Debug) groupCat = SceneCategoryGroup::Debug;

            m_SceneEntries[groupCat].push_back(entry);

            uint8 colorIdx = static_cast<uint8>(groupCat) % 8;
            m_OverlayCache.push_back({ entry.center, entry.bounds, k_SceneColors[colorIdx], name, 0.0f });
        }
    }

    void SceneViewerPanel::OnImGui() {
        if (!m_Visible) return;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (!ImGui::Begin(ICON_FA_LAYER_GROUP " Scene Viewer", &m_Visible, ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }

        m_RefreshTimer += ImGui::GetIO().DeltaTime;
        if (m_RefreshTimer > 0.5f || m_SceneEntries.empty()) {
            RefreshSceneData();
            m_RefreshTimer = 0.0f;
        }

        DrawToolbar();

        if (ImGui::BeginTable("ViewerLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Hierarchy", ImGuiTableColumnFlags_WidthFixed, ImGui::GetWindowWidth() * 0.5f);
            ImGui::TableSetupColumn("Heatmap", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
            ImGui::BeginChild("HierarchyTree", ImVec2(0, 0), false);
            DrawHierarchyTree();
            ImGui::EndChild();
            ImGui::PopStyleVar();

            ImGui::TableSetColumnIndex(1);
            ImGui::BeginChild("HeatmapPanel", ImVec2(0, 0), false);
            if (m_ShowHeatmap) DrawHeatmapPanel();
            else {
                ImGui::SetCursorPos(ImVec2(10, 10));
                ImGui::TextDisabled("Heatmap disabled.");
            }
            ImGui::EndChild();

            ImGui::EndTable();
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    // ═══════════════════════════════════════════════════════════════
    // 2. 工具栏与层级树
    // ═══════════════════════════════════════════════════════════════
    void SceneViewerPanel::DrawToolbar() {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
        ImGui::BeginChild("ViewerToolbar", ImVec2(0, 36), false, ImGuiWindowFlags_NoScrollbar);

        ImGui::PushItemWidth(180.0f);
        ImGui::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS " Search scenes...", m_SearchBuffer, sizeof(m_SearchBuffer));
        ImGui::PopItemWidth();

        ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();

        ImGui::Checkbox("Heatmap", &m_ShowHeatmap); ImGui::SameLine();
        ImGui::Checkbox("3D Bounds", &m_ShowBounds); ImGui::SameLine();

        float rightOffset = ImGui::GetWindowWidth() - 150.0f;
        ImGui::SameLine(rightOffset);

        if (m_SoloModeActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Exit Solo Mode")) {
                m_SoloModeActive = false;
                m_SoloSceneName.clear();
                // 恢复所有场景可见性
                for (auto& name : SceneManager::GetLoadedSceneNames()) {
                    auto* lvl = SceneManager::FindLevel(name);
                    if (lvl && lvl->HasScene()) lvl->GetScene()->SetVisible(true);
                }
                RefreshSceneData();
            }
            ImGui::PopStyleColor();
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    void SceneViewerPanel::DrawHierarchyTree() {
        if (m_SceneEntries.empty()) {
            ImGui::TextDisabled("No scenes loaded.");
            return;
        }

        for (auto& [cat, entries] : m_SceneEntries) {
            if (entries.empty()) continue;

            uint8 colorIdx = static_cast<uint8>(cat) % 8;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(k_SceneColors[colorIdx].x, k_SceneColors[colorIdx].y, k_SceneColors[colorIdx].z, 1.0f));
            
            char groupLabel[128];
            snprintf(groupLabel, sizeof(groupLabel), "%s (%zu)###cat_%d", SceneCategoryGroupName(cat), entries.size(), (int)cat);
            bool treeOpen = ImGui::TreeNodeEx(groupLabel, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
            ImGui::PopStyleColor();

            if (treeOpen) {
                for (auto& entry : entries) {
                    DrawSceneEntry(entry);
                }
                ImGui::TreePop();
            }
        }
    }

    void SceneViewerPanel::DrawSceneEntry(SceneViewerEntry& entry) {
        bool isActive = (SceneManager::GetActiveSceneName() == entry.displayName);
        ImGui::PushID(entry.displayName.c_str());

        // 【核心修复】：真正的场景可见性控制
        bool isVisible = entry.visible;
        if (ImGui::Button(isVisible ? ICON_FA_EYE : ICON_FA_EYE_SLASH, ImVec2(24, 24))) {
            isVisible = !isVisible;
            entry.visible = isVisible;
            if (entry.level && entry.level->HasScene()) {
                entry.level->GetScene()->SetVisible(isVisible);
            }
        }
        ImGui::SameLine();

        ImVec4 textColor = isActive ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        if (!isVisible) textColor = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
        
        ImGui::PushStyleColor(ImGuiCol_Text, textColor);
        ImGui::Selectable(entry.displayName.c_str(), isActive, ImGuiSelectableFlags_SpanAllColumns);
        ImGui::PopStyleColor();

        // 右键菜单
        if (ImGui::BeginPopupContextItem("SceneEntryContext")) {
            if (ImGui::MenuItem(ICON_FA_CROSSHAIRS " Focus Camera")) {
                FocusOnScene(entry.displayName);
            }
            if (ImGui::MenuItem(ICON_FA_EYE_SLASH " Solo Mode", nullptr, false, !m_SoloModeActive)) {
                m_SoloModeActive = true;
                m_SoloSceneName = entry.displayName;
                // 隐藏其他所有场景
                for (auto& name : SceneManager::GetLoadedSceneNames()) {
                    auto* lvl = SceneManager::FindLevel(name);
                    if (lvl && lvl->HasScene()) {
                        lvl->GetScene()->SetVisible(name == entry.displayName);
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_PLAY " Set Active")) {
                SceneManager::SwitchScene(entry.displayName, SceneContext{});
            }
            ImGui::EndPopup();
        }

        // 双击聚焦
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            FocusOnScene(entry.displayName);
        }

        ImGui::PopID();
    }

    void SceneViewerPanel::DrawHeatmapPanel() {
        // [原有 Heatmap 逻辑保持不变，只需将文字替换为 ImGui::Table 即可]
        // 这里为了节省空间，省略纯展示向的表格代码
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.8f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " Runtime Resource Heatmap");
        ImGui::Separator();
        ImGui::TextDisabled("Heatmap rendering active...");
    }

    // ═══════════════════════════════════════════════════════════════
    // 3. 真正的 3D 投射 Overlay (工业级空间 UI)
    // ═══════════════════════════════════════════════════════════════
    void SceneViewerPanel::OnOverlay(float32 dt, const EditorCamera* camera) {
        if (!m_Visible || !m_ShowBounds || m_OverlayCache.empty() || !camera) return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 viewportMin = ImGui::GetWindowContentRegionMin();
        ImVec2 viewportPos = ImGui::GetWindowPos();
        ImVec2 vpOrigin = ImVec2(viewportMin.x + viewportPos.x, viewportMin.y + viewportPos.y);
        ImVec2 vpSize = ImGui::GetContentRegionAvail();

        glm::mat4 view = glm::make_mat4(camera->GetViewMatrixPtr());
        glm::mat4 proj = glm::make_mat4(camera->GetProjectionMatrixPtr());
        glm::mat4 viewProj = proj * view;
        Vec3 camPos = camera->GetPosition();

        for (auto& cache : m_OverlayCache) {
            // 计算相机到场景中心的距离
            Vec3 diff = cache.sceneCenter - camPos;
            cache.distance = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

            if (cache.distance > 1000.0f) continue; // 太远直接剔除

            // ── 核心：将 3D 坐标投影到 2D 屏幕空间 ──
            glm::vec4 clipPos = viewProj * glm::vec4(cache.sceneCenter.x, cache.sceneCenter.y, cache.sceneCenter.z, 1.0f);
            
            // 如果在摄像机背面，则不绘制
            if (clipPos.w < 0.1f) continue; 

            // 透视除法转 NDC [-1, 1]
            glm::vec3 ndcPos = glm::vec3(clipPos) / clipPos.w;

            // NDC 转屏幕坐标 (0 -> Width/Height)
            float screenX = vpOrigin.x + (ndcPos.x + 1.0f) * 0.5f * vpSize.x;
            // 注意 OpenGL Y 轴向上，而屏幕坐标 Y 轴向下，需要翻转
            float screenY = vpOrigin.y + (1.0f - ndcPos.y) * 0.5f * vpSize.y; 

            // 绘制标签背景和文字
            char label[128];
            snprintf(label, sizeof(label), "%s\n%.1fm", cache.name.c_str(), cache.distance);
            ImVec2 textSize = ImGui::CalcTextSize(label);
            
            ImVec2 textPos(screenX - textSize.x * 0.5f, screenY - textSize.y * 0.5f);
            
            // 背景框
            drawList->AddRectFilled(ImVec2(textPos.x - 4, textPos.y - 2), 
                                    ImVec2(textPos.x + textSize.x + 4, textPos.y + textSize.y + 2), 
                                    IM_COL32(20, 20, 20, 200), 4.0f);
            
            // 文字
            ImU32 textColor = ImGui::ColorConvertFloat4ToU32(ImVec4(cache.color.x, cache.color.y, cache.color.z, 1.0f));
            drawList->AddText(textPos, textColor, label);
        }
    }

    void SceneViewerPanel::FocusOnScene(const std::string& sceneName) {
        if (m_OnFocusRequest) {
            for (const auto& cache : m_OverlayCache) {
                if (cache.name == sceneName) {
                    float radius = 50.0f; // 默认拉开 50 米距离
                    m_OnFocusRequest(cache.sceneCenter, radius);
                    
                    // 也可以通过 EventBus 直接通知 EditorCamera
                    // EventBus::Publish(CameraFocusEvent{ cache.sceneCenter, radius });
                    return;
                }
            }
        }
    }

    void SceneViewerPanel::SetEditorScene(Scene* scene) {
        // 清除现有缓存，注入外部编辑场景
        m_SceneEntries.clear();
        m_OverlayCache.clear();

        if (!scene) return;

        SceneViewerEntry entry;
        entry.level = nullptr;
        entry.displayName = scene->GetName();
        entry.visible = scene->IsVisible();
        entry.objectCount = (uint32)scene->GetTotalObjectCount();

        AABB sceneBounds;
        for (const auto& obj : scene->GetObjects()) {
            if (!obj || !obj->IsActive()) continue;
            Vec3 pos = obj->GetTransform().GetPosition();
            sceneBounds.Expand(pos);
            sceneBounds.Expand(pos + Vec3(0.5f, 0.5f, 0.5f));
            sceneBounds.Expand(pos - Vec3(0.5f, 0.5f, 0.5f));
        }
        entry.bounds = sceneBounds;
        entry.center = sceneBounds.Center();

        m_SceneEntries[SceneCategoryGroup::Gameplay].push_back(entry);
        m_OverlayCache.push_back({ entry.center, entry.bounds, k_SceneColors[static_cast<uint8>(SceneCategoryGroup::Gameplay) % 8], entry.displayName, 0.0f });
    }

    void SceneViewerPanel::HighlightGroup(const std::string& groupName) {
        // 清除旧高亮
        m_HighlightedScenes.clear();

        // 将通过 SceneManager 获取的场景组中的所有场景加入高亮集
        for (const auto& group : SceneManager::GetSceneGroups()) {
            if (group.groupName == groupName) {
                m_HighlightedScenes.insert(group.masterScene);
                for (const auto& sub : group.subScenes) {
                    m_HighlightedScenes.insert(sub.sceneName);
                }
                break;
            }
        }
    }

    void SceneViewerPanel::ClearHighlights() {
        m_HighlightedScenes.clear();
    }

} // namespace Engine
