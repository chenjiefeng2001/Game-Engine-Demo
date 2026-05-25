#pragma once

/**
 * @file SceneHierarchyPanel.h
 * @brief 场景层级面板 — 遍历场景中的 GameObject，以列表形式展示并支持选中
 *
 * 在 Application::OnImGui() 中调用 OnImGui() 即可显示。
 * 选中实体后可通过 GetSelected() 获取，供检视器等其他面板使用。
 */

#include "Engine/Types.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace Engine {

    class Scene;
    class GameObject;

    class SceneHierarchyPanel {
    public:
        SceneHierarchyPanel();
        explicit SceneHierarchyPanel(Scene* scene);
        ~SceneHierarchyPanel() = default;

        // 禁止拷贝
        SceneHierarchyPanel(const SceneHierarchyPanel&) = delete;
        SceneHierarchyPanel& operator=(const SceneHierarchyPanel&) = delete;

        // ── 场景绑定 ──
        void SetScene(Scene* scene) { m_Scene = scene; }
        Scene* GetScene() const { return m_Scene; }

        // ── 渲染 ──
        /** 在 OnImGui() 中调用，绘制场景层级面板 */
        void OnImGui();

        // ── 选中管理 ──
        /** 获取当前选中的对象（可为 nullptr） */
        GameObject* GetSelected() const { return m_Selected; }

        /** 手动设置选中对象 */
        void SetSelected(GameObject* obj) { m_Selected = obj; }

        /** 选中对象发生变化时触发的回调 */
        using SelectionCallback = std::function<void(GameObject*)>;
        void SetSelectionCallback(SelectionCallback callback) { m_SelectionCallback = std::move(callback); }

        // ── 显示控制 ──
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

    private:
        /** 递归遍历节点树，绘制 ImGui 树形节点 */
        void DrawEntityNode(GameObject* obj);

        /** 清除当前选中 */
        void ClearSelection();

        Scene* m_Scene = nullptr;
        GameObject* m_Selected = nullptr;
        SelectionCallback m_SelectionCallback = nullptr;
        bool m_Visible = true;
    };

} // namespace Engine
