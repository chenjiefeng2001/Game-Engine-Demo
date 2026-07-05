#include "Engine/InspectorPanel.h"
#include "Engine/UiHelpers.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/Component.h"
#include "Engine/Core/GameObject/SpriteComponent.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/GameObject/MeshRendererComponent.h"
#include "Engine/Core/Physics/PhysicsComponent.h"
#include "Engine/Core/Log.h"
#include "Engine/Editor/Reflect.h"
#include "Engine/Editor/IconsFontAwesome6.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <vector>
#include <functional>

namespace Engine {

    // ── 全局延迟操作队列：解决遍历时删除组件导致的迭代器失效崩溃 ──
    static std::vector<std::function<void()>> s_DeferredActions;

    // ============================================================
    // UI 辅助函数 (绝对稳定的排版)
    // ============================================================
    namespace UI {

        bool BeginPropertyGrid(const char* id) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
            // 保持右侧控件列占满剩余空间
            return ImGui::BeginTable(id, 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp);
        }

        void EndPropertyGrid() {
            ImGui::EndTable();
            ImGui::PopStyleVar(2);
        }

        void DrawLabel(const char* label) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            // 修复栈泄露：使用 SetNextItemWidth 代替 PushItemWidth
            ImGui::SetNextItemWidth(-FLT_MIN); 
        }

        bool DrawVec3Control(const char* label, Vec3& values, float resetValue = 0.0f) {
            bool changed = false;
            ImGuiIO& io = ImGui::GetIO();
            auto boldFont = io.Fonts->Fonts[0]; // 假定索引 0 是粗体或基础字体

            DrawLabel(label);

            // 【核心修复】：利用传入的 label 作为 ID 作用域，隔离 Position/Rotation/Scale 的 X,Y,Z 控件！
            ImGui::PushID(label);

            ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

            float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
            ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };
            
            // 计算等比三等分的输入框宽度
            float totalWidth = ImGui::GetContentRegionAvail().x;
            float inputWidth = (totalWidth - buttonSize.x * 3.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;

            auto DrawSingleAxis = [&](const char* id, const char* btnLabel, float& val, ImVec4 btnCol, ImVec4 btnHover, ImVec4 btnActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnHover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, btnActive);
                ImGui::PushFont(boldFont);
                if (ImGui::Button(btnLabel, buttonSize)) { val = resetValue; changed = true; }
                ImGui::PopFont();
                ImGui::PopStyleColor(3);

                ImGui::SameLine();
                ImGui::SetNextItemWidth(inputWidth);
                if (ImGui::DragFloat(id, &val, 0.1f, 0.0f, 0.0f, "%.2f")) changed = true;
            };

            DrawSingleAxis("##X", "X", values.x, ImVec4(0.8f, 0.1f, 0.15f, 1.0f), ImVec4(0.9f, 0.2f, 0.2f, 1.0f), ImVec4(0.8f, 0.1f, 0.15f, 1.0f));
            ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x);
            DrawSingleAxis("##Y", "Y", values.y, ImVec4(0.2f, 0.7f, 0.2f, 1.0f), ImVec4(0.3f, 0.8f, 0.3f, 1.0f), ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
            ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x);
            DrawSingleAxis("##Z", "Z", values.z, ImVec4(0.1f, 0.25f, 0.8f, 1.0f), ImVec4(0.2f, 0.35f, 0.9f, 1.0f), ImVec4(0.1f, 0.25f, 0.8f, 1.0f));

            ImGui::PopStyleVar();
            
            // 记得弹出 ID 作用域
            ImGui::PopID();

            return changed;
        }

        // ── 工业级组件折叠头 ──
        bool DrawComponentHeader(const char* title, bool* enabled, bool* removeComponent, std::function<void()> customMenu = nullptr) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
            float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
            
            // PushID 防止同名组件碰撞
            ImGui::PushID(title);

            if (enabled && !(*enabled)) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                                       ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap |
                                       ImGuiTreeNodeFlags_FramePadding;

            bool open = ImGui::TreeNodeEx("##header", flags, "%s", title);
            
            if (enabled && !(*enabled)) ImGui::PopStyleColor();

            // 动态对齐右侧按钮，防偏移
            float windowWidth = ImGui::GetWindowWidth();
            
            if (enabled) {
                ImGui::SameLine(windowWidth - lineHeight * 2.5f - 10.0f);
                ImGui::Checkbox("##enabled", enabled);
            }

            ImGui::SameLine(windowWidth - lineHeight - 8.0f);
            if (ImGui::Button(ICON_FA_GEAR, ImVec2(lineHeight, lineHeight))) {
                ImGui::OpenPopup("ComponentSettings");
            }

            bool remove = false;
            if (ImGui::BeginPopup("ComponentSettings")) {
                if (removeComponent && ImGui::MenuItem(ICON_FA_TRASH " Remove Component")) {
                    remove = true;
                }
                if (customMenu) customMenu();
                ImGui::EndPopup();
            }
            if (removeComponent) *removeComponent = remove;

            ImGui::PopID();
            ImGui::PopStyleVar();
            return open;
        }
    }

    // ============================================================
    // 内置组件绘制器
    // ============================================================
    namespace {

        void DrawTransformWidget(GameObject* obj, const DrawContext& ctx) {
            if (!UI::DrawComponentHeader(ICON_FA_ARROWS " Transform", nullptr, nullptr)) return;

            auto& transform = obj->GetTransform();
            Vec3 pos   = transform.GetPosition();
            Vec3 rot   = transform.GetRotation();
            Vec3 scale = transform.GetScale();

            if (UI::BeginPropertyGrid("TransformGrid")) {
                if (UI::DrawVec3Control("Position", pos)) {
                    transform.SetPosition(pos);
                    ctx.recordUndo();
                }
                if (UI::DrawVec3Control("Rotation", rot)) {
                    transform.SetRotation(rot);
                    ctx.recordUndo();
                }
                if (UI::DrawVec3Control("Scale", scale, 1.0f)) {
                    transform.SetScale(scale);
                    ctx.recordUndo();
                }
                UI::EndPropertyGrid();
            }
            ImGui::TreePop(); // TreeNodeEx 展开时必须 Pop
        }

        void DrawMeshRendererWidget(GameObject* obj, const DrawContext& ctx) {
            auto* mr = obj->GetComponent<MeshRendererComponent>();
            if (!mr) return;

            bool enabled = mr->IsEnabled();
            bool remove = false;
            
            bool open = UI::DrawComponentHeader(ICON_FA_CUBES " Mesh Renderer", &enabled, &remove);
            
            // 无论组件头是否展开，均需处理启用/禁用和删除逻辑
            if (enabled != mr->IsEnabled()) mr->SetEnabled(enabled);
            if (remove) {
                s_DeferredActions.push_back([obj]() { obj->RemoveComponent<MeshRendererComponent>(); });
            }

            if (open) {
                if (UI::BeginPropertyGrid("MeshRendererGrid")) {
                    UI::DrawLabel("Mesh");
                    std::string meshName = mr->TargetMesh ? "Mesh Selected" : "None";
                    ImGui::Button(meshName.c_str(), ImVec2(-1, 0));
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                            ctx.recordUndo(); // 处理拖入逻辑
                        }
                        ImGui::EndDragDropTarget();
                    }

                    UI::DrawLabel("Material");
                    std::string matName = mr->TargetMaterial ? "Material Selected" : "None";
                    ImGui::Button(matName.c_str(), ImVec2(-1, 0));

                    if (mr->TargetMaterial) {
                        UI::DrawLabel("Base Color");
                        // 修复指针转换警告，直接取地址
                        float* bc = &mr->TargetMaterial->BaseColor[0];
                        if (ImGui::ColorEdit4("##BaseColor", bc, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
                            ctx.recordUndo();
                        }
                    }

                    UI::EndPropertyGrid();
                }
                ImGui::TreePop();
            }
        }

        void DrawPhysicsWidget(GameObject* obj, const DrawContext& ctx) {
            auto* physics = obj->GetComponent<PhysicsComponent>();
            if (!physics) return;

            bool enabled = physics->IsEnabled();
            bool remove = false;
            
            bool open = UI::DrawComponentHeader(ICON_FA_BOWLING_BALL " Physics 2D", &enabled, &remove, [&]() {
                if (ImGui::MenuItem("Reset Body Velocity")) {
                    if (physics->GetBody()) physics->GetBody()->SetLinearVelocity({0, 0});
                }
            });

            if (enabled != physics->IsEnabled()) physics->SetEnabled(enabled);
            if (remove) {
                s_DeferredActions.push_back([obj]() { obj->RemoveComponent<PhysicsComponent>(); });
            }

            if (open) {
                if (UI::BeginPropertyGrid("PhysicsGrid")) {
                    auto* body = physics->GetBody();
                    
                    UI::DrawLabel("Body State");
                    ImGui::TextDisabled(body ? "Active" : "Uninitialized");

                    if (body) {
                        UI::DrawLabel("Body Type");
                        int type = static_cast<int>(body->GetType());
                        const char* typeNames[] = { "Static", "Kinematic", "Dynamic" };
                        if (ImGui::Combo("##BodyType", &type, typeNames, 3)) {
                            // 设置刚体类型
                            ctx.recordUndo();
                        }

                        UI::DrawLabel("Linear Damping");
                        float linDamp = body->GetLinearDamping();
                        if (ImGui::DragFloat("##LinDamp", &linDamp, 0.01f, 0.0f, 10.0f)) {
                            body->SetLinearDamping(linDamp);
                            ctx.recordUndo();
                        }

                        UI::DrawLabel("Angular Damping");
                        float angDamp = body->GetAngularDamping();
                        if (ImGui::DragFloat("##AngDamp", &angDamp, 0.01f, 0.0f, 10.0f)) {
                            body->SetAngularDamping(angDamp);
                            ctx.recordUndo();
                        }
                    }
                    UI::EndPropertyGrid();
                }
                ImGui::TreePop();
            }
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

    void InspectorPanel::OnImGui() {
        if (!m_Visible) return;

        if (!m_BuiltinsRegistered) {
            RegisterBuiltins();
            m_BuiltinsRegistered = true;
        }

        ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);
        ImGui::Begin(ICON_FA_INFO_CIRCLE " Inspector", &m_Visible);

        DrawToolbar();

        if (!m_Target && m_MultiTargets.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("No entity selected.");
            ImGui::End();
            return;
        }

        GameObject* obj = m_Target;
        if (!obj) {
            ImGui::End();
            return;
        }

        // ── 绘制实体头部 (Entity Header) ──
        DrawHeader(obj);
        ImGui::Separator();
        ImGui::Spacing();

        DrawContext ctx = MakeDrawContext(obj);

        // ── 绘制 Transform (永远在最上面) ──
        DrawTransformComponent(obj, ctx);
        ImGui::Spacing();

        // ── 绘制其余所有组件 ──
        obj->ForEachComponent([this, obj, &ctx](Component& comp) {
            const size_t typeId = typeid(comp).hash_code();
            auto it = m_DrawerRegistry.find(typeId);

            ImGui::PushID((void*)typeId); // 防止组件之间的 ID 冲突
            if (it != m_DrawerRegistry.end()) {
                const auto& drawer = it->second;
                if (drawer.drawFn) {
                    drawer.drawFn(obj, ctx);
                }
            } else {
                // 如果没有注册特定的 Drawer，使用默认警告块
                bool enabled = comp.IsEnabled();
                bool remove = false;
                if (UI::DrawComponentHeader(comp.GetTypeDisplayName(), &enabled, &remove)) {
                    ImGui::Indent();
                    ImGui::TextDisabled("No custom editor available.");
                    ImGui::Unindent();
                    ImGui::TreePop(); // 必须 Pop
                }
                if (enabled != comp.IsEnabled()) comp.SetEnabled(enabled);
                
                if (remove) {
                    Engine::Log::Warn("Cannot safely remove unregistered component via Editor.");
                }
            }
            ImGui::PopID();
            ImGui::Spacing();
        });

        // ── 添加组件按钮 ──
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // 居中绘制按钮
        float buttonWidth = 150.0f;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f);
        if (ImGui::Button("Add Component", ImVec2(buttonWidth, 30))) {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (ImGui::BeginPopup("AddComponentPopup")) {
            DrawAddComponentMenu();
            ImGui::EndPopup();
        }

        if (m_DebugMode) DrawDebugInfo(obj);

        ImGui::End();

        // ── 统一在帧末尾执行延迟清理，绝对防止迭代器失效！ ──
        for (auto& action : s_DeferredActions) {
            action();
        }
        s_DeferredActions.clear();
    }

    void InspectorPanel::DrawToolbar() {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));

        if (m_Locked) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
        if (ImGui::Button(m_Locked ? ICON_FA_LOCK " Locked" : ICON_FA_UNLOCK " Lock")) ToggleLocked();
        if (m_Locked) ImGui::PopStyleColor();

        ImGui::SameLine();

        if (m_DebugMode) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
        if (ImGui::Button(ICON_FA_BUG " Debug")) ToggleDebugMode();
        if (m_DebugMode) ImGui::PopStyleColor();

        ImGui::PopStyleVar();
        ImGui::Separator();
    }

    void InspectorPanel::DrawHeader(GameObject* obj) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 8));
        
        // Active Checkbox
        bool active = obj->IsActive();
        if (ImGui::Checkbox("##Active", &active)) {
            obj->SetActive(active);
        }
        
        ImGui::SameLine();
        
        // Name Input
        char nameBuf[256];
        std::strncpy(nameBuf, obj->GetName().c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';

        ImGui::PushItemWidth(-1);
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // 假设有大号字体
        if (ImGui::InputText("##Name", nameBuf, sizeof(nameBuf))) {
            obj->SetName(nameBuf);
        }
        ImGui::PopFont();
        ImGui::PopItemWidth();

        ImGui::PopStyleVar();
    }

    void InspectorPanel::DrawTransformComponent(GameObject* obj, const DrawContext& ctx) {
        DrawTransformWidget(obj, ctx);
    }

    void InspectorPanel::DrawAddComponentMenu() {
        ImGui::InputTextWithHint("##search", ICON_FA_MAGNIFYING_GLASS " Search...", m_AddComponentSearch, sizeof(m_AddComponentSearch));
        ImGui::Separator();

        std::string search(m_AddComponentSearch);
        std::transform(search.begin(), search.end(), search.begin(), ::tolower);

        for (auto& [typeId, drawer] : m_DrawerRegistry) {
            if (drawer.builtin && drawer.displayName != "Mesh Renderer" && drawer.displayName != "Physics") continue;

            std::string name = drawer.displayName;
            std::string lowerName = name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            
            if (!search.empty() && lowerName.find(search) == std::string::npos) continue;

            if (ImGui::Selectable(name.c_str())) {
                // 这里需要调用底层引擎实际为 GameObject 挂载对应组件的代码
                // obj->AddComponentByID(typeId);
                ImGui::CloseCurrentPopup();
            }
        }
    }

    void InspectorPanel::DrawDebugInfo(GameObject* obj) {
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FA_WRENCH " Developer Debug Info");

        if (ImGui::TreeNode("Object State")) {
            ImGui::Text("Entity ID: %u", obj->GetID());
            ImGui::Text("Global Active: %s", obj->IsActiveInHierarchy() ? "True" : "False");
            ImGui::Text("Children Count: %zu", obj->GetChildren().size());
            ImGui::TreePop();
        }
    }

    // ============================================================
    // 绘制器注册
    // ============================================================

    void InspectorPanel::RegisterDrawerByType(size_t typeId, ComponentDrawer drawer) {
        m_DrawerRegistry[typeId] = std::move(drawer);
    }

    void InspectorPanel::UnregisterDrawerByType(size_t typeId) {
        m_DrawerRegistry.erase(typeId);
    }

    void InspectorPanel::RegisterBuiltins() {
        // Physics
        ComponentDrawer physicsDrawer;
        physicsDrawer.displayName = "Physics";
        physicsDrawer.category = "Physics";
        physicsDrawer.orderInInspector = 20;
        physicsDrawer.builtin = true;
        physicsDrawer.drawFn = [](GameObject* obj, const DrawContext& ctx) { DrawPhysicsWidget(obj, ctx); };
        RegisterDrawerByType(typeid(PhysicsComponent).hash_code(), std::move(physicsDrawer));

        // MeshRenderer
        ComponentDrawer meshDrawer;
        meshDrawer.displayName = "Mesh Renderer";
        meshDrawer.category = "Rendering";
        meshDrawer.orderInInspector = 15;
        meshDrawer.builtin = true;
        meshDrawer.drawFn = [](GameObject* obj, const DrawContext& ctx) { DrawMeshRendererWidget(obj, ctx); };
        RegisterDrawerByType(typeid(MeshRendererComponent).hash_code(), std::move(meshDrawer));
    }

} // namespace Engine
