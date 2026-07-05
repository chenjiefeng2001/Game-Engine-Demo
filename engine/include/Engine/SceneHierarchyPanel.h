#pragma once

#include "Engine/Types.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Scene/SceneManager.h"
#include "Engine/Core/Level/Level.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/EventBus.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <set>

namespace Engine {

    class SceneHierarchyPanel {
    public:
        SceneHierarchyPanel();
        explicit SceneHierarchyPanel(Scene* scene);
        ~SceneHierarchyPanel();

        SceneHierarchyPanel(const SceneHierarchyPanel&) = delete;
        SceneHierarchyPanel& operator=(const SceneHierarchyPanel&) = delete;

        void Init(); // 注册 EventBus 订阅
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
        void DrawEntityNode(const std::shared_ptr<GameObject>& obj);
        void DrawContextMenu(GameObject* obj);
        void HandleDragDropToRoot();
        void ProcessPendingActions();

        const char* GetEntityIcon(GameObject* obj) const;
        bool PassesFilter(GameObject* obj, const std::string& name) const;
        void ParseSearchText();
        bool IsNodeVisible(GameObject* obj) const;

        // ── 延迟操作（防止遍历树时修改容器导致迭代器失效） ──
        std::shared_ptr<GameObject> m_PendingDestroyObj;
        std::shared_ptr<GameObject> m_PendingReparentChild;
        std::shared_ptr<GameObject> m_PendingReparentParent;

        // ── 重命名状态 ──
        uint32 m_RenamingNodeId = 0;
        char m_RenameBuffer[256] = "";

        // ── 事件订阅句柄 ──
        size_t m_SelectionEventId = 0;

        Scene* m_Scene = nullptr;
        GameObject* m_Selected = nullptr;
        SelectionCallback m_SelectionCallback = nullptr;
        bool m_Visible = true;

        char m_SearchBuffer[256] = {};
        std::unordered_map<uint32, uint32> m_NodeData; // id -> colorTag (简化)
        std::unordered_set<uint32> m_HiddenObjects;
        std::unordered_set<uint32> m_LockedObjects;
        std::unordered_set<std::string> m_CollapsedScenes;
        float m_ScrollY = 0.0f;
        GameObject* m_DragSource = nullptr;
        GameObject* m_DragTarget = nullptr;
        bool m_IsDragging = false;
        std::unordered_set<GameObject*> m_MultiSelected;
        bool m_MultiSelectActive = false;
    };

} // namespace Engine