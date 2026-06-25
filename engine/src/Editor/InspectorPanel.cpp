#include "Engine/InspectorPanel.h"
#include "Engine/UiHelpers.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/Component.h"
#include "Engine/Core/GameObject/SpriteComponent.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/Physics/PhysicsComponent.h"
#include "Engine/Core/Log.h"

#include <imgui.h>
#include <cstring>
#include <algorithm>
#include <sstream>

namespace Engine {

    // ============================================================
    // 内置组件绘制器（自由函数，非成员 — 仅在 .cpp 内部使用）
    // ============================================================
    namespace {

        // ── Transform（非 Component 派生，是 GameObject 直接成员） ──
        void DrawTransformWidget(GameObject* obj, const DrawContext& ctx) {
            using namespace Inspector;

            if (!Inspector::DrawComponentHeader("Transform", nullptr, nullptr))
                return;

            auto& transform = obj->GetTransform();
            Vec3 pos   = transform.GetPosition();
            Vec3 rot   = transform.GetRotation();
            Vec3 scale = transform.GetScale();

            if (Inspector::DrawVec3Field("Position", &pos.x, 0.1f))
                transform.SetPosition(pos);
            if (Inspector::DrawVec3Field("Rotation", &rot.x, 1.0f, -360.0f, 360.0f))
                transform.SetRotation(rot);
            if (Inspector::DrawVec3Field("Scale", &scale.x, 0.1f, 0.01f, 100.0f))
                transform.SetScale(scale);

            ImGui::Unindent();
        }

        // ── SpriteComponent 自定义绘制器 ──
        class SpriteComponentDrawer : public ComponentDrawer {
        public:
            SpriteComponentDrawer() {
                displayName = "Sprite";
                category = "Rendering";
                orderInInspector = 10;
                builtin = true;

                drawFn = [](GameObject* obj, const DrawContext& ctx) {
                    auto* sprite = obj->GetComponent<SpriteComponent>();
                    if (!sprite) return;

                    using namespace Inspector;

                    std::string texName = sprite->HasTexture() ? "Texture (loaded)" : "None (none)";
                    PropertyMeta texMeta;
                    texMeta.flags = PropertyFlag::AssetReference;
                    DrawAssetRefField("Texture", &texName, nullptr, nullptr, nullptr, texMeta);

                    Vec4 color = sprite->GetColor();
                    PropertyMeta colorMeta;
                    if (DrawColorField("Color", &color, true, colorMeta))
                        sprite->SetColor(color);

                    float uv[4] = { sprite->GetUVX(), sprite->GetUVY(),
                                    sprite->GetUVW(), sprite->GetUVH() };
                    PropertyMeta uvMeta;
                    if (DrawVec4Field("UV", uv, 0.01f, uvMeta))
                        sprite->SetUV(uv[0], uv[1], uv[2], uv[3]);

                    Vec2 tiling = sprite->GetTiling();
                    Vec2 offset = sprite->GetOffset();
                    if (DrawVec2Field("Tiling", &tiling.x, 0.1f, uvMeta))
                        sprite->SetTiling(tiling);
                    if (DrawVec2Field("Offset", &offset.x, 0.1f, uvMeta))
                        sprite->SetOffset(offset);

                    int layer = sprite->GetSortingLayer();
                    int order = sprite->GetOrderInLayer();
                    if (DrawDragInt("Sorting Layer", &layer))
                        sprite->SetSortingLayer(layer);
                    if (DrawDragInt("Order In Layer", &order))
                        sprite->SetOrderInLayer(order);

                    bool visible = sprite->IsVisible();
                    if (DrawBoolField("Visible", &visible))
                        sprite->SetVisible(visible);
                };
            }
        };

        void DrawPhysicsWidget(GameObject* obj, const DrawContext& ctx) {
            using namespace Inspector;
            auto* physics = obj->GetComponent<PhysicsComponent>();
            if (!physics) return;

            auto* body = physics->GetBody();

            bool enabled = physics->IsEnabled();
            if (!DrawComponentHeader("Physics", &enabled, [&]() {
                if (ImGui::MenuItem("Reset Body")) {
                    if (body) body->SetLinearVelocity({0, 0});
                }
            })) return;

            if (enabled != physics->IsEnabled())
                physics->SetEnabled(enabled);

            ImGui::Text("Body: %s", body ? "Created" : "None");

            if (body) {
                int type = static_cast<int>(body->GetType());
                const int typeValues[] = { 0, 1, 2 };
                const char* typeNames[] = { "Static", "Kinematic", "Dynamic" };
                DrawEnumField("Type", &type, typeValues, typeNames, 3);

                float linearDamping  = body->GetLinearDamping();
                float angularDamping = body->GetAngularDamping();
                PropertyMeta dampMeta;
                dampMeta.flags = PropertyFlag::Range;
                dampMeta.range = { 0.0f, 10.0f, 0.01f };
                if (DrawFloatField("Linear Damping", &linearDamping, dampMeta))
                    body->SetLinearDamping(linearDamping);
                if (DrawFloatField("Angular Damping", &angularDamping, dampMeta))
                    body->SetAngularDamping(angularDamping);

                ImGui::Separator();
                ImGui::TextDisabled("Mass: %.2f  Inertia: %.2f",
                    static_cast<double>(body->GetMass()),
                    static_cast<double>(body->GetInertia()));

                if (ctx.debugMode) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "[Debug]");
                    ImGui::Text("Position: %.2f, %.2f",
                        static_cast<double>(body->GetPosition().x),
                        static_cast<double>(body->GetPosition().y));
                    ImGui::Text("Velocity: %.2f, %.2f",
                        static_cast<double>(body->GetLinearVelocity().x),
                        static_cast<double>(body->GetLinearVelocity().y));
                }
            }

            ImGui::Unindent();
        }

    } // anonymous namespace

    // ============================================================
    // InspectorPanel 实现
    // ============================================================

    InspectorPanel::InspectorPanel() {
        m_AddComponentSearch[0] = '\0';
    }

    void InspectorPanel::SetTarget(GameObject* target) {
        if (m_Locked) return;
        m_Target = target;
        m_MultiTargets.clear();
    }

    void InspectorPanel::SetMultiTarget(const std::vector<GameObject*>& targets) {
        if (m_Locked) return;
        m_MultiTargets = targets;
        if (!targets.empty())
            m_Target = targets.front();
    }

    void InspectorPanel::ClearMultiTarget() {
        m_MultiTargets.clear();
    }

    DrawContext InspectorPanel::MakeDrawContext(GameObject* obj) {
        DrawContext ctx;
        ctx.objectPtr = obj;
        ctx.objectId = obj ? obj->GetID() : 0;
        ctx.debugMode = m_DebugMode;
        ctx.isMultiSelect = IsMultiSelection();
        ctx.isPrefabOverride = false;
        ctx.isReadOnly = (m_Locked && !m_DebugMode);
        ctx.searchFilter = m_Filter.searchText;

        ctx.recordUndo = [this, obj]() {
            if (m_UndoCallback && obj)
                m_UndoCallback(obj);
        };

        return ctx;
    }

    bool InspectorPanel::PassesFilter(const std::string& name) const {
        if (m_Filter.searchText.empty()) return true;

        std::string search = m_Filter.searchText;
        std::string target = name;

        if (!m_Filter.matchCase) {
            std::transform(search.begin(), search.end(), search.begin(), ::tolower);
            std::transform(target.begin(), target.end(), target.begin(), ::tolower);
        }

        return target.find(search) != std::string::npos;
    }

    void InspectorPanel::OnPropertyModified(GameObject* obj, const char* propertyName) {
        if (m_ModifyCallback && obj) {
            m_ModifyCallback(obj, propertyName ? propertyName : "");
        }
    }

    void InspectorPanel::DrawTransformComponent(GameObject* obj, const DrawContext& ctx) {
        DrawTransformWidget(obj, ctx);
    }

    void InspectorPanel::OnImGui() {
        if (!m_Visible) return;

        if (!m_BuiltinsRegistered) {
            RegisterBuiltins();
            m_BuiltinsRegistered = true;
        }

        ImGui::SetNextWindowSize(ImVec2(360, 480), ImGuiCond_FirstUseEver);
        ImGui::Begin("Inspector", &m_Visible);

        DrawToolbar();

        if (!m_Filter.searchText.empty() || ImGui::IsWindowFocused()) {
            char searchBuf[256];
            std::strncpy(searchBuf, m_Filter.searchText.c_str(), sizeof(searchBuf) - 1);
            searchBuf[sizeof(searchBuf) - 1] = '\0';

            ImGui::PushItemWidth(-1);
            if (ImGui::InputTextWithHint("##search", "Search properties...",
                                         searchBuf, sizeof(searchBuf))) {
                m_Filter.searchText = searchBuf;
            }
            ImGui::PopItemWidth();
        }

        if (!m_Target && m_MultiTargets.empty()) {
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No object selected");
            ImGui::Spacing();
            if (m_Locked) {
                ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.0f, 1.0f),
                                   "(Inspector is locked)");
            }
            ImGui::End();
            return;
        }

        if (IsMultiSelection()) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f),
                               "Multiple objects selected (%zu)",
                               m_MultiTargets.size());
            ImGui::Separator();
        }

        GameObject* obj = m_Target;
        if (!obj) {
            ImGui::End();
            return;
        }

        ImGui::Spacing();
        DrawHeader(obj);

        ImGui::Separator();
        ImGui::Spacing();

        DrawContext ctx = MakeDrawContext(obj);
        DrawTransformComponent(obj, ctx);

        ImGui::Separator();
        ImGui::Spacing();

        obj->ForEachComponent([this, obj, &ctx](Component& comp) {
            const size_t typeId = typeid(comp).hash_code();
            auto it = m_DrawerRegistry.find(typeId);

            if (it != m_DrawerRegistry.end()) {
                const auto& drawer = it->second;
                if (!PassesFilter(drawer.displayName)) return;

                ImGui::Spacing();
                if (drawer.drawFn) {
                    drawer.drawFn(obj, ctx);
                }
            } else {
                if (!PassesFilter(comp.GetTypeDisplayName())) return;

                ImGui::Spacing();
                bool enabled = comp.IsEnabled();
                if (Inspector::DrawComponentHeader(comp.GetTypeDisplayName(),
                                                    &enabled, nullptr)) {
                    ImGui::Indent();
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                       "No editor for this component");
                    if (m_DebugMode) {
                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "[Debug Mode]");
                        ImGui::Text("TypeID: 0x%zx", typeId);
                        ImGui::Text("Enabled: %s", enabled ? "true" : "false");
                    }
                    ImGui::Unindent();
                }
                if (enabled != comp.IsEnabled())
                    comp.SetEnabled(enabled);
            }
        });

        if (obj->HasSprite() && !obj->GetComponent<SpriteComponent>()) {
            ImGui::Spacing();
            SpriteComponentDrawer spriteDrawer;
            if (spriteDrawer.drawFn) {
                spriteDrawer.drawFn(obj, ctx);
            }
        }
        if (obj->HasPhysics() && !obj->GetComponent<PhysicsComponent>()) {
            ImGui::Spacing();
            DrawPhysicsWidget(obj, ctx);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("+ Add Component", ImVec2(-1, 0))) {
            m_ShowAddComponentMenu = !m_ShowAddComponentMenu;
        }
        if (m_ShowAddComponentMenu) {
            DrawAddComponentMenu();
        }

        if (m_DebugMode) {
            DrawDebugInfo(obj);
        }

        ImGui::End();
    }

    void InspectorPanel::DrawToolbar() {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

        if (m_Locked) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.0f, 1.0f));
            if (ImGui::Button("Locked", ImVec2(60, 0)))
                ToggleLocked();
            ImGui::PopStyleColor();
        } else {
            if (ImGui::Button("Lock", ImVec2(60, 0)))
                ToggleLocked();
        }

        ImGui::SameLine();

        if (m_DebugMode) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
            if (ImGui::Button("Debug", ImVec2(55, 0)))
                ToggleDebugMode();
            ImGui::PopStyleColor();
        } else {
            if (ImGui::Button("Debug", ImVec2(55, 0)))
                ToggleDebugMode();
        }

        ImGui::SameLine();

        if (IsMultiSelection()) {
            ImGui::TextDisabled("x%zu", m_MultiTargets.size());
        }

        ImGui::PopStyleVar();
    }

    void InspectorPanel::DrawHeader(GameObject* obj) {
        if (!obj) return;

        char nameBuf[256];
        std::strncpy(nameBuf, obj->GetName().c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';

        ImGui::PushItemWidth(-1);
        if (ImGui::InputText("##Name", nameBuf, sizeof(nameBuf)))
            obj->SetName(nameBuf);
        ImGui::PopItemWidth();

        bool active = obj->IsActive();
        ImGui::SameLine();
        if (ImGui::Checkbox("Active", &active))
            obj->SetActive(active);

        if (m_DebugMode) {
            ImGui::SameLine();
            ImGui::TextDisabled("ID: %u", obj->GetID());
        }

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                           "Children: %zu", obj->GetChildren().size());

        uint32 layer = obj->GetLayer();
        if (ImGui::InputScalar("Layer", ImGuiDataType_U32, &layer))
            obj->SetLayer(layer);
    }

    void InspectorPanel::DrawAddComponentMenu() {
        if (ImGui::Begin("Add Component", &m_ShowAddComponentMenu,
                         ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputTextWithHint("##search", "Search component...",
                                     m_AddComponentSearch, sizeof(m_AddComponentSearch));
            ImGui::Separator();

            std::string search(m_AddComponentSearch);
            if (!search.empty()) {
                std::transform(search.begin(), search.end(), search.begin(), ::tolower);
            }

            for (auto& [typeId, drawer] : m_DrawerRegistry) {
                (void)typeId;
                if (drawer.builtin) continue;

                std::string name = drawer.displayName;
                if (!search.empty()) {
                    std::string lowerName = name;
                    std::transform(lowerName.begin(), lowerName.end(),
                                   lowerName.begin(), ::tolower);
                    if (lowerName.find(search) == std::string::npos)
                        continue;
                }

                if (ImGui::Selectable(name.c_str())) {
                    m_ShowAddComponentMenu = false;
                }
            }
            ImGui::End();
        }
    }

    void InspectorPanel::DrawDebugInfo(GameObject* obj) {
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "--- Debug Info ---");

        if (ImGui::CollapsingHeader("Object State", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("ID: %u", obj->GetID());
            ImGui::Text("Active: %s", obj->IsActive() ? "true" : "false");
            ImGui::Text("Layer: %u", obj->GetLayer());
            ImGui::Text("LayerMask: 0x%08X", obj->GetLayerMask());
            ImGui::Text("Parent: %s", obj->GetParent() ?
                        obj->GetParent()->GetName().c_str() : "(none)");
            ImGui::Text("Children: %zu", obj->GetChildren().size());

            auto& t = obj->GetTransform();
            Vec3 worldPos = t.GetPosition();
            ImGui::Text("World Pos: %.2f, %.2f, %.2f",
                        worldPos.x, worldPos.y, worldPos.z);
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Components")) {
            ImGui::Indent();
            int compIdx = 0;
            obj->ForEachComponent([&](Component& comp) {
                ImGui::Text("[%d] %s (0x%zx) enabled=%s",
                            compIdx++,
                            comp.GetTypeDisplayName(),
                            typeid(comp).hash_code(),
                            comp.IsEnabled() ? "yes" : "no");
            });
            ImGui::Unindent();
        }
        ImGui::Separator();
    }

    // ============================================================
    // 绘制器注册
    // ============================================================

    void InspectorPanel::RegisterDrawerByType(size_t typeId, ComponentDrawer drawer) {
        m_DrawerRegistry[typeId] = std::move(drawer);

        m_SortedDrawers.clear();
        for (auto& [id, d] : m_DrawerRegistry) {
            (void)id;
            m_SortedDrawers.push_back(&d);
        }
        std::sort(m_SortedDrawers.begin(), m_SortedDrawers.end(),
                  [](const ComponentDrawer* a, const ComponentDrawer* b) {
                      return a->orderInInspector < b->orderInInspector;
                  });
    }

    void InspectorPanel::UnregisterDrawerByType(size_t typeId) {
        m_DrawerRegistry.erase(typeId);
        m_SortedDrawers.clear();
        for (auto& [id, d] : m_DrawerRegistry) {
            (void)id;
            m_SortedDrawers.push_back(&d);
        }
    }

    void InspectorPanel::RegisterBuiltins() {
        static SpriteComponentDrawer s_SpriteDrawer;
        RegisterDrawerByType(typeid(SpriteComponent).hash_code(), s_SpriteDrawer);

        ComponentDrawer physicsDrawer;
        physicsDrawer.displayName = "Physics";
        physicsDrawer.category = "Physics";
        physicsDrawer.orderInInspector = 20;
        physicsDrawer.builtin = true;
        physicsDrawer.drawFn = [](GameObject* obj, const DrawContext& ctx) {
            DrawPhysicsWidget(obj, ctx);
        };
        RegisterDrawerByType(typeid(PhysicsComponent).hash_code(),
                              std::move(physicsDrawer));
    }

} // namespace Engine