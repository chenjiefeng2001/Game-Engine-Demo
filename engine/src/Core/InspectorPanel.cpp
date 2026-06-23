#include "Engine/InspectorPanel.h"
#include "Engine/UiHelpers.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/Component.h"
#include "Engine/Core/GameObject/SpriteComponent.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/Physics/PhysicsComponent.h"

#include <imgui.h>
#include <cstring>

namespace Engine {

    // ============================================================
    // ════════════════════════════════════════════════════════════════
    // 内置组件绘制器声明（在 RegisterBuiltins 中注册到 m_DrawerRegistry）
    // ════════════════════════════════════════════════════════════════
    // ============================================================

    namespace {

        // ── Transform（非 Component 派生，是 GameObject 直接成员） ──
        void DrawTransformComponent(GameObject* obj) {
            using namespace Ui;

            DrawComponentSection("Transform", true, [&]() {
                ScopedID id("Transform");

                auto& transform = obj->GetTransform();
                Vec3 pos   = transform.GetPosition();
                Vec3 rot   = transform.GetRotation();
                Vec3 scale = transform.GetScale();

                if (DrawVec3Control("Position", pos, 0.1f))
                    transform.SetPosition(pos);
                if (DrawVec3Control("Rotation", rot, 1.0f, -360.0f, 360.0f))
                    transform.SetRotation(rot);
                if (DrawVec3Control("Scale",   scale, 0.1f, 0.01f, 100.0f))
                    transform.SetScale(scale);
            });
        }

        // ── Sprite ──
        void DrawSpriteComponent(GameObject* obj) {
            auto* sprite = obj->GetComponent<SpriteComponent>();
            if (!sprite) return;

            if (!ImGui::CollapsingHeader("Sprite", ImGuiTreeNodeFlags_DefaultOpen))
                return;

            ImGui::Indent();
            ImGui::PushID("Sprite");

            // 纹理
            if (sprite->HasTexture()) {
                ImGui::Text("Texture: (loaded)");
            } else {
                ImGui::Text("Texture: (none)");
            }

            // 颜色编辑
            Vec4 color = sprite->GetColor();
            if (ImGui::ColorEdit4("Color", &color.x,
                                  ImGuiColorEditFlags_NoInputs |
                                  ImGuiColorEditFlags_AlphaBar)) {
                sprite->SetColor(color);
            }

            // UV 参数
            float uv[4] = { sprite->GetUVX(), sprite->GetUVY(),
                            sprite->GetUVW(), sprite->GetUVH() };
            if (ImGui::InputFloat4("UV", uv))
                sprite->SetUV(uv[0], uv[1], uv[2], uv[3]);

            // Tiling & Offset
            Vec2 tiling = sprite->GetTiling();
            Vec2 offset = sprite->GetOffset();
            if (ImGui::InputFloat2("Tiling", &tiling.x))
                sprite->SetTiling(tiling);
            if (ImGui::InputFloat2("Offset", &offset.x))
                sprite->SetOffset(offset);

            // 排序
            int layer = sprite->GetSortingLayer();
            int order = sprite->GetOrderInLayer();
            if (ImGui::InputInt("Sorting Layer", &layer))
                sprite->SetSortingLayer(layer);
            if (ImGui::InputInt("Order In Layer", &order))
                sprite->SetOrderInLayer(order);

            // 可见性
            bool visible = sprite->IsVisible();
            if (ImGui::Checkbox("Visible", &visible))
                sprite->SetVisible(visible);

            ImGui::PopID();
            ImGui::Unindent();
        }

        // ── Physics ──
        void DrawPhysicsComponent(GameObject* obj) {
            auto* physics = obj->GetComponent<PhysicsComponent>();
            if (!physics) return;

            if (!ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen))
                return;

            ImGui::Indent();
            ImGui::PushID("Physics");

            auto* body = physics->GetBody();

            ImGui::Text("Body: %s", body ? "Created" : "None");

            if (body) {
                // 刚体类型
                BodyType type = body->GetType();
                const char* typeNames[] = { "Static", "Kinematic", "Dynamic" };
                const char* currentType = typeNames[static_cast<int>(type)];
                if (ImGui::BeginCombo("Type", currentType)) {
                    for (int i = 0; i < 3; ++i) {
                        bool isSelected = (static_cast<int>(type) == i);
                        if (ImGui::Selectable(typeNames[i], isSelected))
                            body->SetType(static_cast<BodyType>(i));
                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                // 阻尼
                float linearDamping  = body->GetLinearDamping();
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

    } // anonymous namespace

    // ============================================================
    // InspectorPanel 实现
    // ============================================================

    InspectorPanel::InspectorPanel() = default;

    void InspectorPanel::OnImGui() {
        if (!m_Visible) return;

        // 首次调用时注册内置组件绘制器
        if (!m_BuiltinsRegistered) {
            RegisterBuiltins();
            m_BuiltinsRegistered = true;
        }

        ImGui::SetNextWindowSize(ImVec2(320, 480), ImGuiCond_FirstUseEver);
        ImGui::Begin("Inspector", &m_Visible);

        if (!m_Target) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               "No object selected");
            ImGui::End();
            return;
        }

        // ── 头部：名称 + Active ──
        DrawHeader(m_Target);

        ImGui::Separator();
        ImGui::Spacing();

        // ── 内置 Transform（非 Component 派生，是 GameObject 直接成员） ──
        DrawTransformComponent(m_Target);

        ImGui::Separator();
        ImGui::Spacing();

        // ── 自动化动态组件 UI ──
        // 遍历 GameObject 上所有通过 AddComponent 挂载的组件
        m_Target->ForEachComponent([this](Component& comp) {
            const size_t typeId = typeid(comp).hash_code();
            auto it = m_DrawerRegistry.find(typeId);
            if (it != m_DrawerRegistry.end()) {
                ImGui::Spacing();
                const auto& meta = it->second;
                if (ImGui::CollapsingHeader(meta.displayName.c_str(),
                                            ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Indent();
                    if (meta.drawFn)
                        meta.drawFn(m_Target);
                    ImGui::Unindent();
                }
            } else {
                // 未注册绘制器的组件：显示占位标题
                ImGui::Spacing();
                if (ImGui::CollapsingHeader(comp.GetTypeDisplayName(),
                                            ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Indent();
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                       "No editor for this component");
                    ImGui::Unindent();
                }
            }
        });

        // ── 旧版便捷方法支持（若组件未通过 AddComponent 挂载） ──
        // Sprite 组件（便捷方法 GetSprite 可能自动创建）
        if (m_Target->HasSprite() && !m_Target->GetComponent<SpriteComponent>()) {
            DrawSpriteComponent(m_Target);
        }

        // Physics 组件
        if (m_Target->HasPhysics() && !m_Target->GetComponent<PhysicsComponent>()) {
            DrawPhysicsComponent(m_Target);
        }

        // ── 旧版 RegisterComponentDrawer 兼容（通过名称而非 typeid 注册的） ──
        // 这部分已由上面的 ForEachComponent 覆盖

        ImGui::End();
    }

    // ============================================================
    // 绘制器注册
    // ============================================================

    void InspectorPanel::RegisterDrawerByType(size_t typeId, ComponentMeta meta) {
        if (!meta.displayName.empty() && meta.drawFn)
            m_DrawerRegistry[typeId] = std::move(meta);
    }

    void InspectorPanel::UnregisterDrawerByType(size_t typeId) {
        m_DrawerRegistry.erase(typeId);
    }

    void InspectorPanel::RegisterBuiltins() {
        // 注册 SpriteComponent 绘制器
        RegisterDrawer<SpriteComponent>([
            drawFn = DrawSpriteComponent
        ](GameObject* obj) {
            drawFn(obj);
        });

        // 注册 PhysicsComponent 绘制器
        RegisterDrawer<PhysicsComponent>([
            drawFn = DrawPhysicsComponent
        ](GameObject* obj) {
            drawFn(obj);
        });
    }

    // ============================================================
    // 头部绘制
    // ============================================================

    void InspectorPanel::DrawHeader(GameObject* obj) {
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

} // namespace Engine