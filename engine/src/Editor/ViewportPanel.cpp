#include "Engine/Editor/ViewportPanel.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Editor/IconsFontAwesome6.h"
#include "Engine/OpenGL/OpenGLFramebuffer.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include "Engine/Core/Input.h"
#include "Engine/Core/Log.h"

#include <imgui.h>
#include <ImGuizmo.h>

// glm 实验性扩展声明（必须在包含 glm 相关头文件之前）
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

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

    // ═══════════════════════════════════════════════════════════════
    // OnImGui — 工业级管线化渲染入口
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::OnImGui() {
        // 去除边距，使 3D 画面完全铺满窗口
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        bool visible = true;
        ImGui::Begin(m_Config.Name.c_str(), &visible,
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);

        // 严格按照层级和依赖顺序执行管线
        UpdateBoundsAndFocus();  // 必须最先执行，获取准确的屏幕坐标

        Draw3DImage();           // 底层：绘制 3D 画面（FBO）

        DrawGizmos();            // 中层：绘制 ImGuizmo 变换控件

        DrawOverlay();           // 顶层：绘制悬浮工具栏（使用绝对坐标，不再被遮挡）

        HandleDragAndDrop();     // 交互：处理资源拖拽
        HandleMousePicking();    // 交互：处理鼠标点击拾取

        ImGui::End();
        ImGui::PopStyleVar();
    }

    // ═══════════════════════════════════════════════════════════════
    // 1. 计算视口边界、焦点与动态缩放
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::UpdateBoundsAndFocus() {
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();

        // 【核心】：准确计算渲染区域在显示器屏幕上的绝对物理坐标
        ImVec2 vMin = ImGui::GetWindowContentRegionMin();
        ImVec2 vMax = ImGui::GetWindowContentRegionMax();
        ImVec2 vOffset = ImGui::GetWindowPos();

        m_ViewportBounds[0] = { vMin.x + vOffset.x, vMin.y + vOffset.y };
        m_ViewportBounds[1] = { vMax.x + vOffset.x, vMax.y + vOffset.y };

        m_Hovered = ImGui::IsWindowHovered();
        m_Focused = ImGui::IsWindowFocused();

        // 只有悬浮且没有其他 ImGui 控件（如按钮、拖拽条）被激活时，相机才可操控
        // 注意：ImGuizmo::IsUsing() 的检查在 OnUpdate 中完成
        bool canControlCamera = m_Hovered && !ImGui::IsAnyItemActive();
        if (m_Camera) m_Camera->SetActive(canControlCamera);

        // 检查并处理窗口尺寸变化
        if ((m_Width != viewportSize.x || m_Height != viewportSize.y) &&
            viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
            m_Width  = viewportSize.x;
            m_Height = viewportSize.y;
            m_NeedsFBOUpdate = true; // 标记需要重建 FBO
            if (m_Camera) m_Camera->SetViewportSize(m_Width, m_Height);
            if (m_ResizeCallback) m_ResizeCallback(static_cast<int32>(m_Width), static_cast<int32>(m_Height));
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 2. 绘制 3D FBO 画面
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::Draw3DImage() {
        if (m_FBO && m_FBO->IsValid() && m_Width > 0 && m_Height > 0) {
            uint32 texID = m_FBO->GetColorTextureID();
            // V 轴翻转，修正 OpenGL 纹理方向
            ImGui::Image((ImTextureID)(uint64)texID, ImVec2(m_Width, m_Height),
                         ImVec2(0, 1), ImVec2(1, 0));

            // 视口被悬浮时绘制一层微弱的发光边框，提升交互反馈
            if (m_Hovered) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRect(m_ViewportBounds[0], m_ViewportBounds[1],
                            IM_COL32(255, 200, 50, 180), 0.0f, 0, 1.5f);
            }
        } else {
            // Fallback 黑屏状态
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(m_ViewportBounds[0], m_ViewportBounds[1],
                              IM_COL32(30, 30, 35, 255));
            if (m_Width > 0 && m_Height > 0) {
                dl->AddText(m_ViewportBounds[0], IM_COL32(100, 100, 110, 255),
                            ("Viewport: " + m_Config.Name).c_str());
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 3. 绘制悬浮工具栏 (Overlay) — 使用屏幕绝对坐标解决遮挡
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::DrawOverlay() {
        // 【核心修复】：使用基于渲染画面物理边界的绝对坐标！
        // SetCursorScreenPos 忽略所有 Padding / 标题栏 / Tab 栏的影响，
        // 强制将下一个 UI 元素的起始位置锚定到 3D 画布的左上角。
        ImVec2 overlayPos = ImVec2(m_ViewportBounds[0].x + 10.0f,
                                    m_ViewportBounds[0].y + 10.0f);
        ImGui::SetCursorScreenPos(overlayPos);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 0.85f)); // 磨砂黑
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));

        // 创建一个自适应内容的紧凑型工具条
        if (ImGui::BeginChild("ViewportToolbar", ImVec2(280.0f, 32.0f), false,
                              ImGuiWindowFlags_NoScrollbar)) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); // 按钮去背景
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f); // 垂直居中微调

            // ── 变换模式按钮 ──
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

            // ── 局部/世界空间切换 ──
            if (ImGui::Button(m_GizmoLocal ? ICON_FA_CUBE : ICON_FA_DOT_CIRCLE))
                m_GizmoLocal = !m_GizmoLocal;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(m_GizmoLocal ? "Local Space" : "World Space");
            ImGui::SameLine();

            ImGui::TextDisabled("|"); ImGui::SameLine();

            // ── 相机速度滑条 ──
            ImGui::TextDisabled(ICON_FA_CAMERA); ImGui::SameLine();
            ImGui::SetNextItemWidth(60.0f);
            float speed = m_Camera ? m_Camera->GetFlySpeed() : 10.0f;
            if (ImGui::SliderFloat("##camspeed", &speed, 1.0f, 50.0f, "%.1f",
                                    ImGuiSliderFlags_Logarithmic)) {
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
    // 4. 绘制 ImGuizmo 变换控件
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::DrawGizmos() {
        auto selected = m_SelectedObject.lock();
        if (!selected || m_GizmoType < 0) return;

        ImGuizmo::SetOrthographic(m_Camera && m_Camera->GetProjectionType() == CameraProjectionType::Orthographic);
        ImGuizmo::SetDrawlist();

        // 严格约束在物理边界内
        ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
                          m_ViewportBounds[1].x - m_ViewportBounds[0].x,
                          m_ViewportBounds[1].y - m_ViewportBounds[0].y);

        // 从 EditorCamera 获取矩阵
        glm::mat4 cameraView = glm::make_mat4(m_Camera->GetViewMatrixPtr());
        glm::mat4 cameraProj = glm::make_mat4(m_Camera->GetProjectionMatrixPtr());

        // 从选中物体的 Transform 获取世界矩阵
        glm::mat4 transformMatrix(1.0f);
        {
            auto gameObj = std::static_pointer_cast<GameObject>(m_SelectedObject.lock());
            if (gameObj) {
                const Mat4& worldMat = gameObj->GetTransform().GetWorldMatrix();
                transformMatrix = glm::make_mat4(worldMat.Data());
            }
        }

        bool snap = Input::Get() && Input::IsKeyDown(KeyCode::LeftCtrl);
        if (!snap) snap = m_SnapEnabled;
        float snapValue = (m_GizmoType == 1) ? 45.0f : m_SnapValue; // 旋转用 45°
        float snapValues[3] = { snapValue, snapValue, snapValue };

        ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProj),
            (ImGuizmo::OPERATION)m_GizmoType,
            m_GizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
            glm::value_ptr(transformMatrix),
            nullptr,
            snap ? snapValues : nullptr);

        // 如果 Gizmo 被使用，回写变换
        if (ImGuizmo::IsUsing()) {
            auto gameObj = std::static_pointer_cast<GameObject>(m_SelectedObject.lock());
            if (gameObj) {
                glm::vec3 newPos, newScale, newSkew;
                glm::quat newRot;
                glm::vec4 newPerspective;
                glm::decompose(transformMatrix, newScale, newRot, newPos, newSkew, newPerspective);
                glm::vec3 euler = glm::eulerAngles(newRot);

                auto& transform = gameObj->GetTransform();
                transform.SetPosition(Vec3(newPos.x, newPos.y, newPos.z));
                transform.SetRotation(Vec3(glm::degrees(euler.x),
                                            glm::degrees(euler.y),
                                            glm::degrees(euler.z)));
                transform.SetScale(Vec3(newScale.x, newScale.y, newScale.z));
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 5. 处理资源拖拽到场景
    // ═══════════════════════════════════════════════════════════════
    void ViewportPanel::HandleDragAndDrop() {
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                ENGINE_LOG_INFO("Viewport", "Dropped asset into scene");
                // 处理实例化逻辑...
            }
            ImGui::EndDragDropTarget();
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 6. ID 缓冲区像素级拾取 — O(1) 精准点击
    // ═══════════════════════════════════════════════════════════════
    uint32 ViewportPanel::PickAtMouse(float mouseScreenX, float mouseScreenY) const {
        if (!m_FBO) return 0;
        return m_FBO->ReadPixelID(mouseScreenX, mouseScreenY,
                                   m_ViewportBounds[0].x,
                                   m_ViewportBounds[0].y);
    }

    void ViewportPanel::HandleMousePicking() {
        if (m_Hovered && !ImGuizmo::IsOver() && !ImGui::IsAnyItemHovered()) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (m_PickCallback) {
                    float ndcX, ndcY;
                    GetMouseViewportSpace(ndcX, ndcY);

                    // 使用 ID 缓冲区读取点击像素下的 GameObject ID
                    ImVec2 mousePos = ImGui::GetMousePos();
                    uint32 entityId = PickAtMouse(mousePos.x, mousePos.y);

                    m_PickCallback(ndcX, ndcY, entityId);
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 辅助：FBO 尺寸更新
    // ═══════════════════════════════════════════════════════════════
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

    // ═══════════════════════════════════════════════════════════════
    // 3D 场景渲染（每帧调用）
    // ═══════════════════════════════════════════════════════════════
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

        // ── 3. 构建相机矩阵（使用 EditorCamera） ──
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

            // ── 5. 渲染编辑器辅助层（网格/轴） ──
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