#include "Engine/InspectorPanel.h"
#include "Engine/UiHelpers.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/SpriteComponent.h"
#include "Engine/Core/Physics/PhysicsComponent.h"

#include <imgui.h>
#include <cstring>

namespace Engine {

    InspectorPanel::InspectorPanel()
    {
    }

    void InspectorPanel::OnImGui()
    {
        if (!m_Visible) return;

        ImGui::SetNextWindowSize(ImVec2(320, 480), ImGuiCond_FirstUseEver);
        ImGui::Begin("Inspector", &m_Visible);

        if (!m_Target)
        {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               "No object selected");
            ImGui::End();
            return;
        }

        // ── 头部：名称 + Active ──
        DrawHeader(m_Target);

        ImGui::Separator();
        ImGui::Spacing();

        // ── 内置组件 ──
        // Transform 始终存在
        DrawTransform(m_Target);

        // Sprite 组件（条件显示）
        if (m_Target->HasSprite())
            DrawSprite(m_Target);

        // Physics 组件（条件显示）
        if (m_Target->HasPhysics())
            DrawPhysics(m_Target);

        // ── 自定义组件绘制器 ──
        for (const auto& [name, drawFn] : m_CustomDrawers)
        {
            if (drawFn)
            {
                ImGui::Spacing();
                if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Indent();
                    drawFn(m_Target);
                    ImGui::Unindent();
                }
            }
        }

        ImGui::End();
    }

    // ============================================================
    // 扩展接口
    // ============================================================

    void InspectorPanel::RegisterComponentDrawer(
        const std::string& name, ComponentDrawFn drawFn)
    {
        if (!name.empty() && drawFn)
            m_CustomDrawers[name] = std::move(drawFn);
    }

    void InspectorPanel::UnregisterComponentDrawer(const std::string& name)
    {
        m_CustomDrawers.erase(name);
    }

    // ============================================================
    // 内置组件绘制器
    // ============================================================

    void InspectorPanel::DrawHeader(GameObject* obj)
    {
        // 对象名称（可编辑）
        char nameBuf[256];
        std::strncpy(nameBuf, obj->GetName().c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';

        ImGui::PushItemWidth(-1);
        if (ImGui::InputText("##Name", nameBuf, sizeof(nameBuf)))
            obj->SetName(nameBuf);
        ImGui::PopItemWidth();

        // Active 复选框
        bool active = obj->IsActive();
        if (ImGui::Checkbox("Active", &active))
            obj->SetActive(active);

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                           "Children: %zu", obj->GetChildren().size());
    }

    void InspectorPanel::DrawTransform(GameObject* obj)
    {
        using namespace Ui;

        DrawComponentSection("Transform", true, [&]() {
            ScopedID id("Transform");

            auto& transform = obj->GetTransform();
            Vec3 pos = transform.GetPosition();
            Vec3 rot = transform.GetRotation();
            Vec3 scale = transform.GetScale();

            if (DrawVec3Control("Position", pos, 0.1f))
                transform.SetPosition(pos);
            if (DrawVec3Control("Rotation", rot, 1.0f, -360.0f, 360.0f))
                transform.SetRotation(rot);
            if (DrawVec3Control("Scale",   scale, 0.1f, 0.01f, 100.0f))
                transform.SetScale(scale);
        });
    }

    void InspectorPanel::DrawSprite(GameObject* obj)
    {
        if (!ImGui::CollapsingHeader("Sprite", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::Indent();
        ImGui::PushID("Sprite");

        auto& sprite = obj->GetSprite();

        // 纹理路径（只读显示）
        if (sprite.HasTexture())
        {
            // 获取纹理路径 — 通过 Texture 对象的调试信息或路径
            ImGui::Text("Texture: %s", "(loaded)");
        }
        else
        {
            ImGui::Text("Texture: (none)");
        }

        // 颜色编辑
        Vec4 color = sprite.GetColor();
        if (ImGui::ColorEdit4("Color", &color.x,
                              ImGuiColorEditFlags_NoInputs |
                              ImGuiColorEditFlags_AlphaBar))
        {
            sprite.SetColor(color);
        }

        // UV 参数
        float uv[4] = { sprite.GetUVX(), sprite.GetUVY(),
                        sprite.GetUVW(), sprite.GetUVH() };
        if (ImGui::InputFloat4("UV", uv))
            sprite.SetUV(uv[0], uv[1], uv[2], uv[3]);

        // 排序
        int layer = sprite.GetSortingLayer();
        int order = sprite.GetOrderInLayer();
        if (ImGui::InputInt("Sorting Layer", &layer))
            sprite.SetSortingLayer(layer);
        if (ImGui::InputInt("Order In Layer", &order))
            sprite.SetOrderInLayer(order);

        // 可见性
        bool visible = sprite.IsVisible();
        if (ImGui::Checkbox("Visible", &visible))
            sprite.SetVisible(visible);

        ImGui::PopID();
        ImGui::Unindent();
    }

    void InspectorPanel::DrawPhysics(GameObject* obj)
    {
        if (!ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::Indent();
        ImGui::PushID("Physics");

        auto& physics = obj->GetPhysics();
        auto* body = physics.GetBody();

        ImGui::Text("Body: %s", body ? "Created" : "None");

        if (body)
        {
            // 刚体类型
            BodyType type = body->GetType();
            const char* typeNames[] = { "Static", "Kinematic", "Dynamic" };
            const char* currentType = typeNames[static_cast<int>(type)];
            if (ImGui::BeginCombo("Type", currentType))
            {
                for (int i = 0; i < 3; ++i)
                {
                    bool isSelected = (static_cast<int>(type) == i);
                    if (ImGui::Selectable(typeNames[i], isSelected))
                        body->SetType(static_cast<BodyType>(i));
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // 线性阻尼 / 角阻尼
            float linearDamping = body->GetLinearDamping();
            float angularDamping = body->GetAngularDamping();
            if (ImGui::DragFloat("Linear Damping", &linearDamping, 0.01f, 0.0f, 10.0f))
                body->SetLinearDamping(linearDamping);
            if (ImGui::DragFloat("Angular Damping", &angularDamping, 0.01f, 0.0f, 10.0f))
                body->SetAngularDamping(angularDamping);

            // 质量 / 转动惯量（只读）
            ImGui::Text("Mass: %.2f  Inertia: %.2f",
                        static_cast<double>(body->GetMass()),
                        static_cast<double>(body->GetInertia()));
        }

        ImGui::PopID();
        ImGui::Unindent();
    }

} // namespace Engine
