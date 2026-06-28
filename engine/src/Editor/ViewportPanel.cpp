#include "Engine/Editor/ViewportPanel.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Editor/IconsFontAwesome6.h"
#include "Engine/Core/Input.h"
#include "Engine/Core/Log.h"

#include <glad/gl.h>
#include <imgui.h>
#include <ImGuizmo.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>

namespace Engine {

    ViewportPanel::ViewportPanel(const std::string& name) {
        m_Config.Name = name;
        m_Camera = std::make_unique<EditorCamera>();
    }

    ViewportPanel::~ViewportPanel() { Cleanup(); }

    void ViewportPanel::CycleGizmoTool() {
        switch (m_CurrentTool) {
            case ViewportTool::Select:    m_CurrentTool = ViewportTool::Translate; break;
            case ViewportTool::Hand:      m_CurrentTool = ViewportTool::Translate; break;
            case ViewportTool::Translate: m_CurrentTool = ViewportTool::Rotate;    break;
            case ViewportTool::Rotate:    m_CurrentTool = ViewportTool::Scale;     break;
            case ViewportTool::Scale:     m_CurrentTool = ViewportTool::Translate; break;
        }
    }

    void ViewportPanel::SetSnapMode(SnapMode mode) {
        if (m_Camera) m_Camera->SetSnapMode(mode);
    }
    SnapMode ViewportPanel::GetSnapMode() const {
        return m_Camera ? m_Camera->GetSnapMode() : SnapMode::None;
    }

    void ViewportPanel::InitResources(IGraphicsFactory* factory,
                                       const std::string& vertPath,
                                       const std::string& fragPath) {
        m_Factory = factory;
        if (!factory) return;
        if (m_GL) m_Overlay.Init(factory, m_GL);
        ENGINE_LOG_INFO("Viewport", "{}: resources initialized", m_Config.Name);
    }

    void ViewportPanel::Cleanup() {
        DestroyFBO();
        m_Overlay.Shutdown();
        m_GL = nullptr;
        ENGINE_LOG_INFO("Viewport", "{}: cleaned up", m_Config.Name);
    }

    // ═══════════════════════════════════════════════════════════════
    // MRT Framebuffer
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::DestroyFBO() {
        if (!m_GL) return;
        if (m_FBO) { m_GL->DeleteFramebuffers(1, &m_FBO); m_FBO = 0; }
        if (m_ColorTexture) { m_GL->DeleteTextures(1, &m_ColorTexture); m_ColorTexture = 0; }
        if (m_IdTexture) { m_GL->DeleteTextures(1, &m_IdTexture); m_IdTexture = 0; }
        if (m_DepthBuffer) { m_GL->DeleteRenderbuffers(1, &m_DepthBuffer); m_DepthBuffer = 0; }
    }

    void ViewportPanel::InitMRTFramebuffer() {
        if (!m_GL || m_Width <= 0.0f || m_Height <= 0.0f) return;
        DestroyFBO();

        GLsizei w = (std::max)(static_cast<GLsizei>(m_Width), 1);
        GLsizei h = (std::max)(static_cast<GLsizei>(m_Height), 1);

        m_GL->GenFramebuffers(1, &m_FBO);
        m_GL->BindFramebuffer(GL_FRAMEBUFFER, m_FBO);

        m_GL->GenTextures(1, &m_ColorTexture);
        m_GL->BindTexture(GL_TEXTURE_2D, m_ColorTexture);
        m_GL->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        m_GL->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_GL->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_GL->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorTexture, 0);

        m_GL->GenTextures(1, &m_IdTexture);
        m_GL->BindTexture(GL_TEXTURE_2D, m_IdTexture);
        m_GL->TexImage2D(GL_TEXTURE_2D, 0, GL_R32I, w, h, 0, GL_RED_INTEGER, GL_INT, nullptr);
        m_GL->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        m_GL->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        m_GL->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_IdTexture, 0);

        m_GL->GenRenderbuffers(1, &m_DepthBuffer);
        m_GL->BindRenderbuffer(GL_RENDERBUFFER, m_DepthBuffer);
        m_GL->RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        m_GL->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_DepthBuffer);

        GLenum drawBufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        m_GL->DrawBuffers(2, drawBufs);

        if (m_GL->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            ENGINE_LOG_ERROR("Viewport", "MRT FBO incomplete!");
            DestroyFBO();
        }
        m_GL->BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // ═══════════════════════════════════════════════════════════════
    // OnUpdate — 简化状态管理
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::OnUpdate(float32 dt, bool isFocusedViewport) {
        if (!m_Camera) return;

        bool isRightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        bool isMiddleDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        bool isGizmoUsing = ImGuizmo::IsUsing();

        // 飞行模式（右键）优先
        if (isRightDown && m_Hovered) {
            m_Camera->SetActive(true);
        }
        // 手型/中键 -> 关闭相机
        else if ((m_CurrentTool == ViewportTool::Hand && ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_Hovered) || isMiddleDown) {
            m_Camera->SetActive(false);
        }
        else {
            // Hover 且不在 Gizmo 使用时激活相机
            m_Camera->SetActive(m_Hovered && !isGizmoUsing);
        }

        // ProcessInput 只在相机激活且非 Gizmo 使用时调用
        if (m_Camera->IsActive() && !isGizmoUsing) {
            m_Camera->ProcessInput(dt);
        }
        m_Camera->OnUpdate(dt);

        Render3DScene();
    }

    // ═══════════════════════════════════════════════════════════════
    // OnImGui
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::OnImGui() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        bool visible = true;
        ImGui::Begin(m_Config.Name.c_str(), &visible,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        UpdateBoundsAndFocus();
        Draw3DImage();

        if (m_LayerDrawCallback && m_Camera) {
            m_LayerDrawCallback(m_Camera.get(), ImGui::GetIO().DeltaTime);
        }

        HandleCameraNavigation();
        DrawGizmos();
        DrawOverlay();
        HandleDragAndDrop();
        HandleMousePicking();
        HandleViewportInteraction();

        ImGui::End();
        ImGui::PopStyleVar();
    }

    // ═══════════════════════════════════════════════════════════════
    // 1. 边界与尺寸
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::UpdateBoundsAndFocus() {
        ImVec2 sz = ImGui::GetContentRegionAvail();
        ImVec2 vMin = ImGui::GetWindowContentRegionMin();
        ImVec2 vMax = ImGui::GetWindowContentRegionMax();
        ImVec2 off = ImGui::GetWindowPos();
        m_ViewportBounds[0] = { vMin.x + off.x, vMin.y + off.y };
        m_ViewportBounds[1] = { vMax.x + off.x, vMax.y + off.y };
        m_Hovered = ImGui::IsWindowHovered();
        m_Focused = ImGui::IsWindowFocused();

        if ((m_Width != sz.x || m_Height != sz.y) && sz.x > 0 && sz.y > 0) {
            m_Width = sz.x; m_Height = sz.y;
            m_NeedsFBOUpdate = true;
            if (m_Camera) m_Camera->SetViewportSize(m_Width, m_Height);
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 2. FBO 显示
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::Draw3DImage() {
        if (m_ColorTexture && m_Width > 0 && m_Height > 0) {
            ImGui::Image((ImTextureID)(uint64)m_ColorTexture,
                         ImVec2(m_Width, m_Height), ImVec2(0, 1), ImVec2(1, 0));
            if (m_Hovered) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRect(m_ViewportBounds[0], m_ViewportBounds[1],
                            m_Style.SelectionBorderColor, 0.0f, 0, 1.5f);
            }
        } else {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(m_ViewportBounds[0], m_ViewportBounds[1], IM_COL32(30, 30, 35, 255));
            if (m_Width > 0 && m_Height > 0)
                dl->AddText(m_ViewportBounds[0], IM_COL32(100,100,110,255), ("Viewport: " + m_Config.Name).c_str());
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 3. 手型/中键平移
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::HandleCameraNavigation() {
        if (!m_Camera || !m_Hovered) return;

        ImGuiIO& io = ImGui::GetIO();
        bool handActive = (m_CurrentTool == ViewportTool::Hand && io.MouseDown[0] && m_Hovered) ||
                           ImGui::IsMouseDown(ImGuiMouseButton_Middle);

        if (handActive && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
            float sensitivity = m_Camera->GetDistance() * 0.001f;
            Vec3 pos = m_Camera->GetPosition();
            Vec3 focus = m_Camera->GetFocusPoint();
            float yawRad = glm::radians(m_Camera->GetYaw());
            float pitchRad = glm::radians(m_Camera->GetPitch());
            glm::vec3 forward(std::cos(pitchRad)*std::cos(yawRad), std::sin(pitchRad), std::cos(pitchRad)*std::sin(yawRad));
            glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0,1,0)));
            glm::vec3 up = glm::normalize(glm::cross(right, forward));
            float dx = -io.MouseDelta.x * sensitivity;
            float dy = io.MouseDelta.y * sensitivity;
            pos.x += right.x*dx + up.x*dy; pos.y += right.y*dx + up.y*dy; pos.z += right.z*dx + up.z*dy;
            focus.x += right.x*dx + up.x*dy; focus.y += right.y*dx + up.y*dy; focus.z += right.z*dx + up.z*dy;
            m_Camera->SetPosition(pos);
            m_Camera->SetFocusPoint(focus);
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 4. ImGuizmo（修复旋转/缩放回写）
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::DrawGizmos() {
        auto selected = m_SelectedObject.lock();
        if (!selected || !m_Camera) return;

        ImGuizmo::OPERATION op;
        switch (m_CurrentTool) {
            case ViewportTool::Translate: op = ImGuizmo::TRANSLATE; break;
            case ViewportTool::Rotate:    op = ImGuizmo::ROTATE;    break;
            case ViewportTool::Scale:     op = ImGuizmo::SCALE;     break;
            default: return;
        }

        ImGuizmo::SetOrthographic(m_Camera->GetProjectionType() == CameraProjectionType::Orthographic);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
                          m_ViewportBounds[1].x - m_ViewportBounds[0].x,
                          m_ViewportBounds[1].y - m_ViewportBounds[0].y);

        // ★ 关键修复：始终使用相机矩阵（确保轨道/飞行模式下都能工作）
        glm::mat4 view = glm::make_mat4(m_Camera->GetViewMatrixPtr());
        glm::mat4 proj = glm::make_mat4(m_Camera->GetProjectionMatrixPtr());
        glm::mat4 model = glm::make_mat4(selected->GetTransform().GetWorldMatrix().Data());

        float snapVals[3] = { m_SnapValue, m_SnapValue, m_SnapValue };
        if (m_CurrentTool == ViewportTool::Rotate) {
            snapVals[0] = snapVals[1] = snapVals[2] = m_Style.GizmoSnapRotate;
        }
        bool snap = (m_SnapEnabled || (Input::Get() && Input::IsKeyDown(KeyCode::LeftCtrl)));

        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                             op, m_GizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
                             glm::value_ptr(model), nullptr,
                             snap ? snapVals : nullptr);

        if (ImGuizmo::IsUsing()) {
            glm::vec3 translation, scale, skew;
            glm::quat rotation;
            glm::vec4 perspective;
            glm::decompose(model, scale, rotation, translation, skew, perspective);

            auto& xform = selected->GetTransform();
            xform.SetPosition(Vec3(translation.x, translation.y, translation.z));

            // ★ 修复旋转：glm::eulerAngles 返回弧度，转为角度
            glm::vec3 euler = glm::degrees(glm::eulerAngles(rotation));
            xform.SetRotation(Vec3(euler.x, euler.y, euler.z));

            // ★ 修复缩放
            xform.SetScale(Vec3(scale.x, scale.y, scale.z));
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 5. 工具栏
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::DrawOverlay() {
        ImVec2 pos = ImVec2(m_ViewportBounds[0].x + 10, m_ViewportBounds[0].y + 10);
        ImGui::SetCursorScreenPos(pos);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, m_Style.ToolbarBgColor);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));

        if (ImGui::BeginChild("ViewportToolbar", ImVec2(700, m_Style.ToolbarHeight), false,
                              ImGuiWindowFlags_NoScrollbar)) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4,0));

            auto ToolBtn = [&](const char* icon, ViewportTool tool, const char* tip) {
                bool active = (m_CurrentTool == tool);
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f,0.5f,0.8f,1.0f));
                if (ImGui::Button(icon)) m_CurrentTool = tool;
                if (active) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                ImGui::SameLine();
            };

            ToolBtn(ICON_FA_MOUSE_POINTER, ViewportTool::Select,     "Select (Q)");
            ToolBtn(ICON_FA_HAND_POINTER,  ViewportTool::Hand,       "Pan (H)");
            ToolBtn(ICON_FA_ARROWS,        ViewportTool::Translate,  "Move (W)");
            ToolBtn(ICON_FA_ROTATE_LEFT,   ViewportTool::Rotate,     "Rotate (E)");
            ToolBtn(ICON_FA_EXPAND,        ViewportTool::Scale,      "Scale (R)");

            ImGui::Separator(); ImGui::SameLine();
            if (ImGui::Button(m_GizmoLocal ? ICON_FA_CUBE : ICON_FA_DOT_CIRCLE)) m_GizmoLocal = !m_GizmoLocal;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(m_GizmoLocal ? "Local" : "World");

            ImGui::SameLine(); ImGui::Separator(); ImGui::SameLine();

            if (ImGui::Button(ICON_FA_MAGNET)) m_SnapEnabled = !m_SnapEnabled;
            if (m_SnapEnabled) { ImGui::SameLine(); ImGui::SetNextItemWidth(50); ImGui::DragFloat("##snap",&m_SnapValue,0.01f,0.01f,100.0f,"%.2f"); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap (Ctrl)");

            ImGui::SameLine(); ImGui::Separator(); ImGui::SameLine();
            bool sg = m_Config.ShowGrid;
            if (ImGui::Button(ICON_FA_GRID)) m_Config.ShowGrid = !m_Config.ShowGrid;
            if (sg) { ImGui::SameLine(); ImGui::TextDisabled(ICON_FA_CHECK); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Grid (G)");
            ImGui::SameLine();

            ImGui::Separator(); ImGui::SameLine();

            if (m_Camera) {
                Vec3 cp = m_Camera->GetPosition();
                ImGui::TextDisabled("Cam: %.1f,%.1f,%.1f", cp.x, cp.y, cp.z);
                ImGui::SameLine();
                // ★ 修复：Reset 后设置 Select 工具确保可操作
                if (ImGui::SmallButton(ICON_FA_CROSSHAIRS)) {
                    m_Camera->SetPosition({0,5,10});
                    m_Camera->SetFocusPoint({0,0,0});
                    m_Camera->SetActive(true);
                    m_CurrentTool = ViewportTool::Select;
                    ENGINE_LOG_INFO("Viewport", "Camera reset to (0,5,10), tool=Select");
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Camera");
                ImGui::SameLine();
            }

            ImGui::Separator(); ImGui::SameLine();
            ImGui::TextDisabled(ICON_FA_CAMERA); ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            float speed = m_Camera ? m_Camera->GetFlySpeed() : 10.0f;
            if (ImGui::SliderFloat("##speed",&speed,1,50,"%.1f",ImGuiSliderFlags_Logarithmic)) {
                if (m_Camera) m_Camera->SetFlySpeed(speed);
            }

            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    // ═══════════════════════════════════════════════════════════════
    // 6. 拖拽
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::HandleDragAndDrop() {
        if (!ImGui::BeginDragDropTarget()) return;
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            const char* path = (const char*)p->Data;
            if (!path) { ImGui::EndDragDropTarget(); return; }
            std::string assetPath(path);
            bool isMesh = assetPath.ends_with(".obj") || assetPath.ends_with(".fbx") || assetPath.ends_with(".gltf");
            float dp[3] = {0,0,5};
            if (m_Camera) {
                Vec3 pv = m_Camera->GetPosition(), fv = m_Camera->GetFocusPoint();
                Vec3 fwd = {fv.x-pv.x, fv.y-pv.y, fv.z-pv.z};
                float len = std::sqrt(fwd.x*fwd.x+fwd.y*fwd.y+fwd.z*fwd.z);
                if (len>.001f) { fwd.x/=len; fwd.y/=len; fwd.z/=len; }
                dp[0]=pv.x+fwd.x*5; dp[1]=pv.y+fwd.y*5; dp[2]=pv.z+fwd.z*5;
            }
            if (isMesh && m_DropAssetCallback) m_DropAssetCallback(assetPath, dp);
        }
        ImGui::EndDragDropTarget();
    }

    // ═══════════════════════════════════════════════════════════════
    // 7. 拾取
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::HandleMousePicking() {
        static uint64 s_lastPickFrame = 0;
        uint64 thisFrame = ImGui::GetFrameCount();
        if (thisFrame == s_lastPickFrame) return;
        if (!m_Hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGuizmo::IsOver() || ImGui::GetIO().KeyAlt) return;
        if (m_CurrentTool == ViewportTool::Hand) return;
        s_lastPickFrame = thisFrame;
        ImVec2 mouse = ImGui::GetMousePos();
        float lx = mouse.x - m_ViewportBounds[0].x, ly = mouse.y - m_ViewportBounds[0].y;
        int px = (int)lx, py = (int)(m_Height - ly);
        if (px<0||px>=(int)m_Width||py<0||py>=(int)m_Height||!m_FBO||!m_GL) return;
        m_GL->BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        m_GL->ReadBuffer(GL_COLOR_ATTACHMENT1);
        int32 entityId = -1;
        m_GL->ReadPixels(px,py,1,1,GL_RED_INTEGER,GL_INT,&entityId);
        m_GL->ReadBuffer(GL_COLOR_ATTACHMENT0);
        m_GL->BindFramebuffer(GL_FRAMEBUFFER,0);
        if (entityId > 65535) entityId = -1;
        if (m_PickCallback) { float ndcX = (lx/m_Width)*2-1, ndcY = 1-(ly/m_Height)*2; m_PickCallback(ndcX,ndcY,entityId); }
    }

    // ═══════════════════════════════════════════════════════════════
    // 8. 快捷键 + 右键菜单
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::HandleViewportInteraction() {
        // 右键菜单
        if (m_Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGuizmo::IsOver())
            ImGui::OpenPopup("ViewportContextMenu");
        if (ImGui::BeginPopup("ViewportContextMenu")) {
            auto AddMI = [&](const char* l, const char* t) { if (ImGui::MenuItem(l)) { if (m_SceneCreateCallback) m_SceneCreateCallback(t); } };
            if (ImGui::BeginMenu(ICON_FA_PLUS " Add Entity")) {
                AddMI(ICON_FA_CUBE " Cube","Cube"); AddMI(ICON_FA_CIRCLE " Sphere","Sphere"); AddMI(ICON_FA_GRID " Plane","Plane");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(ICON_FA_SUN " Add Light")) { AddMI("Point Light","PointLight"); AddMI("Directional Light","DirectionalLight"); ImGui::EndMenu(); }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_TRASH " Delete","Del",false,!m_SelectedObject.expired())) { if (m_SceneDeleteCallback) m_SceneDeleteCallback(); }
            ImGui::EndPopup();
        }
        if (!m_Focused) return;

        if (ImGui::IsKeyPressed(ImGuiKey_F)) FocusOnSelected();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && m_SceneDeleteCallback && !m_SelectedObject.expired()) m_SceneDeleteCallback();

        // ★ 工具切换（Use the keyboard top row — these do NOT conflict with WASD camera movement）
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) m_CurrentTool = ViewportTool::Select;
        if (ImGui::IsKeyPressed(ImGuiKey_H)) m_CurrentTool = ViewportTool::Hand;
        if (ImGui::IsKeyPressed(ImGuiKey_W)) m_CurrentTool = ViewportTool::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) m_CurrentTool = ViewportTool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) m_CurrentTool = ViewportTool::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_G)) m_Config.ShowGrid = !m_Config.ShowGrid;
        if (ImGui::IsKeyPressed(ImGuiKey_Tab)) CycleGizmoTool();

        // ★ 方向键 → 相机平移（独立于 WASD 不冲突）
        ImGuiIO& io = ImGui::GetIO();
        if (m_Camera && m_Hovered) {
            float moveSpeed = m_Camera->GetFlySpeed() * io.DeltaTime;
            if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
                Vec3 p = m_Camera->GetPosition(), f = m_Camera->GetFocusPoint();
                p.y += moveSpeed; f.y += moveSpeed;
                m_Camera->SetPosition(p); m_Camera->SetFocusPoint(f);
            }
            if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
                Vec3 p = m_Camera->GetPosition(), f = m_Camera->GetFocusPoint();
                p.y -= moveSpeed; f.y -= moveSpeed;
                m_Camera->SetPosition(p); m_Camera->SetFocusPoint(f);
            }
            if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
                float yawRad = glm::radians(m_Camera->GetYaw());
                float pitchRad = glm::radians(m_Camera->GetPitch());
                glm::vec3 fwd(std::cos(pitchRad)*std::cos(yawRad),std::sin(pitchRad),std::cos(pitchRad)*std::sin(yawRad));
                glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0,1,0)));
                Vec3 p = m_Camera->GetPosition(), fp = m_Camera->GetFocusPoint();
                p.x -= right.x*moveSpeed; p.z -= right.z*moveSpeed;
                fp.x -= right.x*moveSpeed; fp.z -= right.z*moveSpeed;
                m_Camera->SetPosition(p); m_Camera->SetFocusPoint(fp);
            }
            if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
                float yawRad = glm::radians(m_Camera->GetYaw());
                float pitchRad = glm::radians(m_Camera->GetPitch());
                glm::vec3 fwd(std::cos(pitchRad)*std::cos(yawRad),std::sin(pitchRad),std::cos(pitchRad)*std::sin(yawRad));
                glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0,1,0)));
                Vec3 p = m_Camera->GetPosition(), fp = m_Camera->GetFocusPoint();
                p.x += right.x*moveSpeed; p.z += right.z*moveSpeed;
                fp.x += right.x*moveSpeed; fp.z += right.z*moveSpeed;
                m_Camera->SetPosition(p); m_Camera->SetFocusPoint(fp);
            }
        }
    }

    void ViewportPanel::FocusOnSelected() {
        auto sel = m_SelectedObject.lock();
        if (!sel || !m_Camera) return;
        Vec3 pos = sel->GetTransform().GetPosition();
        m_Camera->SetFocusPoint(pos);
        m_Camera->SetDistance(5.0f);
        ENGINE_LOG_INFO("Viewport", "Focused: {}", sel->GetName());
    }

    // ═══════════════════════════════════════════════════════════════
    // 3D 场景 MRT 渲染
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::Render3DScene() {
        if (m_NeedsFBOUpdate) { InitMRTFramebuffer(); m_NeedsFBOUpdate = false; }
        if (!m_FBO || !m_GL) return;

        m_GL->BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        m_GL->Viewport(0,0,(GLsizei)m_Width,(GLsizei)m_Height);
        m_GL->ClearColor(0.12f,0.12f,0.15f,1.0f);

        int clearId = -1;
        m_GL->ClearBufferiv(GL_COLOR,1,&clearId);
        m_GL->Clear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        m_GL->Enable(GL_DEPTH_TEST);
        m_GL->Enable(GL_BLEND);
        m_GL->BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

        if (m_Config.CurrentMode == ViewMode::Wireframe)
            m_GL->PolygonMode(GL_FRONT_AND_BACK,GL_LINE);
        else
            m_GL->PolygonMode(GL_FRONT_AND_BACK,GL_FILL);

        if (m_Camera) {
            float aspect = (m_Height>0) ? m_Width/m_Height : 1.778f;
            glm::mat4 proj = (m_Camera->GetProjectionType() == CameraProjectionType::Orthographic)
                ? glm::ortho(-m_Camera->GetDistance()*0.5f*aspect, m_Camera->GetDistance()*0.5f*aspect,
                             -m_Camera->GetDistance()*0.5f, m_Camera->GetDistance()*0.5f, 0.1f, 1000.0f)
                : glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
            glm::mat4 view = glm::lookAt(
                glm::vec3(m_Camera->GetPosition().x,m_Camera->GetPosition().y,m_Camera->GetPosition().z),
                glm::vec3(m_Camera->GetFocusPoint().x,m_Camera->GetFocusPoint().y,m_Camera->GetFocusPoint().z),
                glm::vec3(0,1,0));
            glm::mat4 vp = proj * view;

            if (m_SceneRenderCallback) {
                Vec3 cp = m_Camera->GetPosition();
                m_SceneRenderCallback(glm::value_ptr(vp),&cp.x);
            }
            if (m_Overlay.IsInitialized() && m_Config.ShowGrid) {
                Vec3 cp = m_Camera->GetPosition();
                float cf[3] = {cp.x,cp.y,cp.z};
                m_Overlay.DrawGrid(m_GL,glm::value_ptr(vp),cf,m_Config);
            }
        }

        m_GL->Disable(GL_BLEND);
        if (m_Config.CurrentMode == ViewMode::Wireframe)
            m_GL->PolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        m_GL->BindFramebuffer(GL_FRAMEBUFFER,0);
    }

} // namespace Engine