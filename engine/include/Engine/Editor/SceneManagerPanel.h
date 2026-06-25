#pragma once

/**
 * @file SceneManagerPanel.h
 * @brief 场景管理器面板 — 工业级场景可视化编辑/调试面板
 */

#include "Engine/Types.h"
#include "Engine/Core/Scene/SceneContext.h"
#include "Engine/Core/Scene/SceneTypes.h"
#include "Engine/Core/Scene/SceneManager.h"
#include "Engine/Core/Level/Level.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <atomic>

namespace Engine {

    // ============================================================
    // 面板内部编辑状态（使用 char[] 数组避免 std::string ImGui 依赖）
    // ============================================================
    struct SceneGroupEditState {
        char    groupName[256]   = {};
        char    masterScene[256] = {};
        char    description[512] = {};
        bool    isStreaming = false;
        std::vector<SceneGroupEntry> entries;
    };

    // ============================================================
    // 场景管理器面板
    // ============================================================
    class SceneManagerPanel {
    public:
        SceneManagerPanel();
        ~SceneManagerPanel() = default;

        SceneManagerPanel(const SceneManagerPanel&) = delete;
        SceneManagerPanel& operator=(const SceneManagerPanel&) = delete;

        void OnImGui();
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

        using SceneSwitchCallback = std::function<void(const std::string& sceneName)>;
        void SetSceneSwitchCallback(SceneSwitchCallback cb) { m_OnSceneSwitch = std::move(cb); }

    private:
        void DrawToolbar();
        void DrawGroupSidebar();
        void DrawMainArea();
        void DrawGroupEditor();
        void DrawSceneListEditor();
        void DrawSceneEntryRow(SceneGroupEntry& entry, int index);
        void DrawRuntimeMonitor();
        void DrawRuntimeSceneRow(const std::string& sceneName, Level* level);
        void DrawLifecycleStateTag(LevelState state);
        void DrawPerformancePanel();
        void DrawQuickTools();
        void DrawValidationPanel();
        void DrawFooter();
        void LoadSceneGroupToEdit(const std::string& groupName);
        void SaveCurrentGroup();
        void DeleteGroup(const std::string& groupName);
        void SyncBuildSettings();
        void QuickSwitchToGroup(const std::string& groupName);
        void CreateNewSceneLevelDialog();
        bool PassesFilter(const std::string& name) const;

        bool                    m_Visible = true;
        int                     m_SelectedGroupIndex = -1;
        SceneGroupEditState     m_EditState;
        bool                    m_IsEditing = false;
        bool                    m_GroupDirty = false;

        char                    m_SearchBuffer[256] = {};
        bool                    m_FilterStreaming    = true;
        bool                    m_FilterNormal       = true;
        bool                    m_FilterCinematic     = true;
        float                   m_MonitorRefreshTimer = 0.0f;
        float                   m_LeftPanelWidth = 220.0f;
        bool                    m_ShowValidation  = false;
        bool                    m_ShowPerformance = true;
        SceneSwitchCallback     m_OnSceneSwitch;
        std::vector<SceneLoadProfile> m_CachedProfiles;
        bool                    m_ShowNewSceneDialog = false;
        char                    m_NewSceneName[128] = {};
    };

} // namespace Engine