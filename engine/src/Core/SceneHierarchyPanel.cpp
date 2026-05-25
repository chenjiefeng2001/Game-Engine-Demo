#include "Engine/SceneHierarchyPanel.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/GameObject/GameObject.h"

#include <imgui.h>

namespace Engine {

    SceneHierarchyPanel::SceneHierarchyPanel()
    {
    }

    SceneHierarchyPanel::SceneHierarchyPanel(Scene* scene)
        : m_Scene(scene)
    {
    }

    void SceneHierarchyPanel::OnImGui()
    {
        if (!m_Visible) return;
        if (!m_Scene)
        {
            ImGui::Begin("Scene Hierarchy", &m_Visible);
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No scene assigned");
            ImGui::End();
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(280, 400), ImGuiCond_FirstUseEver);
        ImGui::Begin("Scene Hierarchy", &m_Visible);

        // 快捷按钮
        if (ImGui::Button("Collapse All"))
        {
            // 重置选中，不折叠 ImGui 树节点（ImGui 自动管理）
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Selection"))
        {
            ClearSelection();
        }

        ImGui::Separator();

        // ── 绘制场景中的根对象列表 ──
        const auto& objects = m_Scene->GetObjects();

        if (objects.empty())
        {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Empty scene");
        }
        else
        {
            ImGui::Text("%zu objects", objects.size());
            ImGui::Separator();

            // 使用 ListBox 展示所有根对象及其子对象
            // 用 ImGui 树形节点展示层级
            for (const auto& obj : objects)
            {
                if (obj)
                    DrawEntityNode(obj.get());
            }
        }

        ImGui::End();
    }

    void SceneHierarchyPanel::DrawEntityNode(GameObject* obj)
    {
        if (!obj) return;

        const auto& name = obj->GetName();
        const auto& children = obj->GetChildren();
        bool hasChildren = !children.empty();

        // 构建显示标签：如果对象有子节点则用树节点，否则用 selectable
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
        if (!hasChildren)
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (m_Selected == obj)
            flags |= ImGuiTreeNodeFlags_Selected;

        // 打开节点
        bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)obj, flags, "%s", name.c_str());

        // 鼠标点击 → 选中
        if (ImGui::IsItemClicked())
        {
            m_Selected = obj;
            if (m_SelectionCallback)
                m_SelectionCallback(m_Selected);
        }

        // 拖拽（placeholder，后续可扩展）
        // ...

        // 递归绘制子节点
        if (nodeOpen)
        {
            for (const auto& child : children)
            {
                DrawEntityNode(child.get());
            }
            ImGui::TreePop();
        }
    }

    void SceneHierarchyPanel::ClearSelection()
    {
        m_Selected = nullptr;
        if (m_SelectionCallback)
            m_SelectionCallback(nullptr);
    }

} // namespace Engine
