#pragma once

/**
 * @file SceneHierarchyPanel.h
 * @brief 工业级场景层级面板
 */

#include "Engine/Types.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Scene/SceneManager.h"
#include "Engine/Core/Level/Level.h"
#include "Engine/Core/GameObject/GameObject.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <set>

namespace Engine {

    enum class SearchFilterMode : uint8 {
        Name, Type, Status, Mixed
    };

    struct HierarchyFilter {
        std::string  searchText;
        std::string  nameFilter;
        std::string  typeFilter;
        bool         showHidden   = true;
        bool         showDisabled = true;
        bool         showError    = true;
        bool         activeOnly   = false;
    };

    enum class PrefabStatus : uint8 {
        None,
        Instance,
        InstanceModified,
        InstanceMissing,
        Variant
    };

    struct HierarchyNodeData {
        bool visible      = true;
        bool locked       = false;
        uint32 colorTag   = 0;
        PrefabStatus prefabStatus = PrefabStatus::None;
        float estimatedMemoryKB = 0.0f;
        bool  hasScript   = false;
        bool  isStatic    = false;
        uint32 tagId      = 0;
        uint32 layer      = 0;
    };

    struct MemoryCache {
        std::unordered_map<uint32, float> objectMemoryKB;
        float totalSceneMemoryKB = 0.0f;
        uint32 lastUpdateFrame = 0;
    };

    class SceneHierarchyPanel {
    public:
        SceneHierarchyPanel();
        explicit SceneHierarchyPanel(Scene* scene);
        ~SceneHierarchyPanel() = default;

        SceneHierarchyPanel(const SceneHierarchyPanel&) = delete;
        SceneHierarchyPanel& operator=(const SceneHierarchyPanel&) = delete;

        void SetScene(Scene* scene);
        Scene* GetScene() const { return m_Scene; }

        void OnImGui();

        GameObject* GetSelected() const { return m_Selected; }
        void SetSelected(GameObject* obj);
        void ClearSelection();

        using SelectionCallback = std::function<void(GameObject*)>;
        void SetSelectionCallback(SelectionCallback callback) { m_SelectionCallback = std::move(callback); }

        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

    private:
        void DrawToolbar();
        void DrawSceneList();
        void DrawSceneNode(const std::string& sceneName, Level* level);
        void DrawEntityNode(GameObject* obj, int depth = 0, bool forceDraw = false);
        void DrawPrefabStatusIcon(PrefabStatus status);
        void DrawVisibilityToggle(GameObject* obj, bool& visible);
        void DrawLockToggle(GameObject* obj, bool& locked);
        void DrawContextMenu(GameObject* obj);

        bool PassesFilter(GameObject* obj, const std::string& name) const;
        void ParseSearchText();
        bool IsNodeVisible(GameObject* obj) const;

        HierarchyNodeData& GetNodeData(GameObject* obj);
        float EstimateMemoryKB(GameObject* obj);
        void UpdateMemoryCache();

        void UnloadScene(const std::string& name);
        void SetActiveScene(const std::string& name);
        PrefabStatus DetectPrefabStatus(GameObject* obj) const;

        Scene* m_Scene = nullptr;
        GameObject* m_Selected = nullptr;
        SelectionCallback m_SelectionCallback = nullptr;
        bool m_Visible = true;

        char m_SearchBuffer[256] = {};
        HierarchyFilter m_Filter;
        std::unordered_map<uint32, HierarchyNodeData> m_NodeData;
        MemoryCache m_MemCache;
        uint32 m_FrameCount = 0;
        std::unordered_set<uint32> m_HiddenObjects;
        std::unordered_set<uint32> m_LockedObjects;
        std::unordered_set<std::string> m_CollapsedScenes;
        float m_ScrollY = 0.0f;
        GameObject* m_DragSource = nullptr;
        GameObject* m_DragTarget = nullptr;
        bool m_IsDragging = false;
        std::unordered_set<GameObject*> m_MultiSelected;
        bool m_MultiSelectActive = false;
        SearchFilterMode m_SearchMode = SearchFilterMode::Name;
    };

} // namespace Engine