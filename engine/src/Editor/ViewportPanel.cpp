#include "Engine/Editor/ViewportPanel.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Editor/IconsFontAwesome6.h"
#include "Engine/OpenGL/OpenGLFramebuffer.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include "Engine/Core/Input.h"
#include "Engine/Core/Log.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Engine {

    ViewportPanel::ViewportPanel(const std::string& name) {
        m_Config.Name = name;
        m_Camera = std::make_unique<EditorCamera>();
        m_GizmoType = 0; // 默认平移
    }

    ViewportPanel::~ViewportPanel() { Cleanup(); }

    void ViewportPanel::InitResources(IGraphicsFactory* factory,
                                       const std::string& vertPath,
                                       const std::string& fragPath) {
        m_Factory = factory;
        if (!factory) return;

        if (m_GL) {
            m_Overlay.Init(factory, m_GL);
        }

        ENGINE_LOG_INFO("Viewport", "{}: 3D resources initialized", m_Config.Name);
    }

    void ViewportPanel::InitFBO() {
        if (!m_GL) return;
        m_FBO = std::make_unique<OpenGLFramebuffer>(*m_GL);
        m_FBO->Resize(static_cast<uint32>((m_Width > 0) ? m_Width : 1280),
                      static_cast<uint32>((m_Height > 0) ? m_Height : 720));
    }

    void ViewportPanel::Cleanup() {
        m_FBO.reset();
        m_Scene.reset();
        m_Overlay.Shutdown();
        m_GL = nullptr;
        ENGINE_LOG_INFO("Viewport", "{}: OpenGL resources cleaned up", m_Config.Name);
    }

    void ViewportPanel::GetMouseViewportSpace(float& outNDCX, float& outNDCY) const {
        ImVec2 mousePos = ImGui::GetMousePos();
        float vpW = m_ViewportBounds[1].x - m_ViewportBounds[0].x;
        float vpH = m_ViewportBounds[1].y - m_ViewportBounds[0].y;
        if (vpW < 1.0f || vpH < 1.0f) { outNDCX = 0.0f; outNDCY = 0.0f; return; }

        float localX = mousePos.x - m_ViewportBounds[0].x;
        float localY = mousePos.y - m_ViewportBounds[0].y;
        outNDCX = (localX / vpW) * 2.0f - 1.0f;
        outNDCY = 1.0f - (localY / vpH) * 2.0f;
    }

    void ViewportPanel::GetViewportBounds(float& outMinX, float& outMinY, float& outMaxX, float& outMaxY) const {
        outMinX = m_ViewportBounds[0].x;
        outMinY = m_ViewportBounds[0].y;
        outMaxX = m_ViewportBounds[1].x;
        outMaxY = m_ViewportBounds[1].y;
    }

    void ViewportPanel::OnUpdate(float32 dt, bool isFocusedViewport) {
        if (!m_Camera) return;

        bool usingGizmo = ImGuizmo::IsUsing();
        bool canProcessInput = (m_Hovered || m_Focused) && isFocusedViewport && !usingGizmo;

        m_Camera->SetActive(canProcessInput);
        if (canProcessInput) {
            m_Camera->ProcessInput(dt);
        }
        m_Camera->OnUpdate(dt);

        Render3DScene();
    }

    void ViewportPanel::OnImGui() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        bool visible = true;
        ImGui::Begin(m_Config.Name.c_str(), &visible,
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);

        ImVec2 viewportMinRegion = ImGui::GetWindowContentRegionMin();
        ImVec2 viewportMaxRegion = ImGui::GetWindowContentRegionMax();
        ImVec2 viewportOffset = ImGui::GetWindowPos();
        m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
        m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };

        m_Hovered = ImGui::IsWindowHovered();
        m_Focused = ImGui::IsWindowFocused();

        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        float32 newW = viewportSize.x;
        float32 newH = viewportSize.y;
        if ((newW != m_Width || newH != m_Height) && newW > 0.0f && newH > 0.0f) {
            m_Width  = newW;
            m_Height = newH;
            m_NeedsFBOUpdate = true;
            if (m_Camera) m_Camera->SetViewportSize(m_Width, m_Height);
            if (m_ResizeCallback) m_ResizeCallback(static_cast<int32>(m_Width), static_cast<int32>(m_Height));
        }

        // 绘制 3D 画面
        if (m_FBO && m_FBO->IsValid() && m_Width > 0 && m_Height > 0) {
            uint32 texID = m_FBO->GetColorTextureID();
            ImGui::Image((ImTextureID)(uint64)texID, ImVec2(m_Width, m_Height),
                         ImVec2(0, 1), ImVec2(1, 0));
        } else {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pMin = ImGui::GetCursorScreenPos();
            ImVec2 pMax(pMin.x + m_Width, pMin.y + m_Height);
            if (m_Width > 0 && m_Height > 0) {
                dl->AddRectFilled(pMin, pMax, IM_COL32(30, 30, 35, 255));
                dl->AddText(pMin, IM_COL32(100, 100, 110, 255),
                            ("Viewport: " + m_Config.Name).c_str());
            }
        }

        // 资源拖拽
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                ENGINE_LOG_INFO("Viewport", "Dropped asset into scene");
            }
            ImGui::EndDragDropTarget();
        }

        DrawImGuizmo();
        HandleMousePicking();
        DrawViewportToolbarOverlay();

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void ViewportPanel::DrawViewportToolbarOverlay() {
        ImGui::SetCursorPos(ImVec2(10.0f, 10.0f));

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 0.85f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));

        if (ImGui::BeginChild("ViewportToolbar", ImVec2(240.0f, 32.0f), false, ImGuiWindowFlags_NoScrollbar)) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

            if (ImGui::Button(ICON_FA_ARROWS)) m_GizmoType = 0;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translate (W)");
            ImGui::SameLine();

            if (ImGui::Button(ICON_FA_ROTATE_LEFT)) m_GizmoType = 1;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate (E)");
            ImGui::SameLine();

            if (ImGui::Button(ICON_FA_EXPAND)) m_GizmoType = 2;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale (R)");
            ImGui::SameLine();

            ImGui::TextDisabled("|"); ImGui::SameLine();

            if (ImGui::Button(m_GizmoLocal ? ICON_FA_CUBE : ICON_FA_DOT_CIRCLE))
                m_GizmoLocal = !m_GizmoLocal;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(m_GizmoLocal ? "Local Space" : "World Space");
            ImGui::SameLine();

            ImGui::TextDisabled(ICON_FA_CAMERA);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60.0f);
            float speed = m_Camera ? m_Camera->GetFlySpeed() : 10.0f;
            if (ImGui::SliderFloat("##camspeed", &speed, 1.0f, 50.0f, "%.1f", ImGuiSliderFlags_Logarithmic)) {
                if (m_Camera) m_Camera->SetFlySpeed(speed);
            }

            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    void ViewportPanel::DrawImGuizmo() {
        auto selected = m_SelectedObject.lock();
        if (!selected || m_GizmoType < 0) return;

        ImGuizmo::SetOrthographic(m_Camera && m_Camera->GetProjectionType() == CameraProjectionType::Orthographic);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
                          m_ViewportBounds[1].x - m_ViewportBounds[0].x,
                          m_ViewportBounds[1].y - m_ViewportBounds[0].y);

        glm::mat4 cameraView(1.0f), cameraProj(1.0f);
        if (m_Camera) {
            cameraView = glm::make_mat4(m_Camera->GetViewMatrixPtr());
            cameraProj = glm::make_mat4(m_Camera->GetProjectionMatrixPtr());
        }

        glm::mat4 transformMatrix(1.0f);

        bool snap = m_SnapEnabled;
        float snapValue = m_SnapValue;
        if (m_GizmoType == 1) snapValue = 45.0f;
        float snapValues[3] = { snapValue, snapValue, snapValue };

        ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProj),
            (ImGuizmo::OPERATION)m_GizmoType,
            m_GizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
            glm::value_ptr(transformMatrix),
            nullptr,
            snap ? snapValues : nullptr);
    }

    void ViewportPanel::HandleMousePicking() {
        bool usingGizmo = ImGuizmo::IsUsing() || ImGuizmo::IsOver();
        if (m_Hovered && !usingGizmo && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (m_PickCallback) {
                float ndcX, ndcY;
                GetMouseViewportSpace(ndcX, ndcY);
                m_PickCallback(ndcX, ndcY);
            }
        }
    }

    void ViewportPanel::UpdateFBOIfNeeded() {
        if (!m_NeedsFBOUpdate) return;
        m_NeedsFBOUpdate = false;
        if (!m_GL) return;
        if (!m_FBO) {
            InitFBO();
            return;
        }
        m_FBO->Resize(static_cast<uint32>(m_Width), static_cast<uint32>(m_Height));
    }

    void ViewportPanel::Render3DScene() {
        // 确保 FBO 有效
        if (!m_GL) return;
        if (!m_FBO || !m_FBO->IsValid()) {
            InitFBO();
            if (!m_FBO || !m_FBO->IsValid()) return;
        }
        if (m_Width < 1.0f || m_Height < 1.0f) return;

        UpdateFBOIfNeeded();

        // ── 1. 绑定 FBO ──
        m_FBO->Bind();

        // ── 2. 设置 OpenGL 状态 ──
        m_GL->Viewport(0, 0, static_cast<int32>(m_Width), static_cast<int32>(m_Height));
        m_GL->ClearColor(0.15f, 0.15f, 0.15f, 1.0f);
        m_GL->Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_GL->Enable(GL_DEPTH_TEST);

        if (m_Config.CurrentMode == ViewMode::Wireframe) {
            m_GL->PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        } else {
            m_GL->PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        // ── 3. 构建相机矩阵 ──
        if (m_Camera) {
            float aspect = (m_Height > 0.0f) ? m_Width / m_Height : 1.778f;
            glm::mat4 proj;
            if (m_Camera->GetProjectionType() == CameraProjectionType::Orthographic) {
                float zoom = m_Camera->GetDistance() * 0.5f;
                proj = glm::ortho(-zoom * aspect, zoom * aspect, -zoom, zoom, 0.1f, 1000.0f);
            } else {
                proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
            }
            glm::mat4 view = glm::lookAt(
                glm::vec3(m_Camera->GetPosition().x, m_Camera->GetPosition().y, m_Camera->GetPosition().z),
                glm::vec3(m_Camera->GetFocusPoint().x, m_Camera->GetFocusPoint().y, m_Camera->GetFocusPoint().z),
                glm::vec3(0.0f, 1.0f, 0.0f));

            glm::mat4 viewProj = proj * view;

            // ── 4. 渲染场景（通过回调，外部 EngineEditor 注入具体实现） ──
            if (m_SceneRenderCallback) {
                Vec3 camPos = m_Camera->GetPosition();
                m_SceneRenderCallback(glm::value_ptr(viewProj), &camPos.x);
            }

            // ── 5. 渲染编辑器辅助层 ──
            if (m_Overlay.IsInitialized()) {
                Vec3 camPos = m_Camera->GetPosition();
                float camPosF[3] = { camPos.x, camPos.y, camPos.z };
                m_Overlay.DrawGrid(m_GL, glm::value_ptr(viewProj), camPosF, m_Config);
            }
        }

        // ── 6. 恢复管线状态 ──
        if (m_Config.CurrentMode == ViewMode::Wireframe && m_GL) {
            m_GL->PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
        if (m_RenderContext) {
            m_RenderContext->ResetPipelineState();
        }

        m_FBO->Unbind();
    }

} // namespace Engine