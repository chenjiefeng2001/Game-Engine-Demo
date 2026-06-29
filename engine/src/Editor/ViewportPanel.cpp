#include "Engine/Editor/ViewportPanel.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/Input.h"
#include "Engine/Core/Log.h"
#include "Engine/Editor/EditorTools.h"
#include "Engine/Editor/IconsFontAwesome6.h"

#include <ImGuizmo.h>
#include <glad/gl.h>
#include <imgui.h>
#include <imgui_internal.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace {
inline glm::vec3 ToGlm(const Engine::Vec3 &v) {
  return glm::vec3(v.x, v.y, v.z);
}
} // namespace

namespace Engine {

ViewportPanel::ViewportPanel(const std::string &name) {
  m_Config.Name = name;
  m_Camera = std::make_unique<EditorCamera>();
  // 设置非零初始尺寸，确保 Render3DScene 在 OnImGui 初始化 FBO 前不会崩溃
  m_RenderWidth = 1;
  m_RenderHeight = 1;
  m_Width = 1;
  m_Height = 1;
}
ViewportPanel::~ViewportPanel() { Cleanup(); }

void ViewportPanel::CycleGizmoTool() {
  switch (m_CurrentTool) {
  case ViewportTool::Select:
    m_CurrentTool = ViewportTool::Translate;
    break;
  case ViewportTool::Hand:
    m_CurrentTool = ViewportTool::Translate;
    break;
  case ViewportTool::Translate:
    m_CurrentTool = ViewportTool::Rotate;
    break;
  case ViewportTool::Rotate:
    m_CurrentTool = ViewportTool::Scale;
    break;
  case ViewportTool::Scale:
    m_CurrentTool = ViewportTool::Translate;
    break;
  }
}
void ViewportPanel::SetSnapMode(SnapMode mode) {
  if (m_Camera)
    m_Camera->SetSnapMode(mode);
}
SnapMode ViewportPanel::GetSnapMode() const {
  return m_Camera ? m_Camera->GetSnapMode() : SnapMode::None;
}
void ViewportPanel::InitResources(IGraphicsFactory *f, const std::string &,
                                  const std::string &) {
  m_Factory = f;
  if (f && m_GL)
    m_Overlay.Init(f, m_GL);
}
void ViewportPanel::Cleanup() {
  // 这是供析构函数使用的彻底清理
  if (m_GL && m_FBO_ID) {
    m_GL->DeleteFramebuffers(1, &m_FBO_ID);
    m_GL->DeleteTextures(1, &m_ColorTexture);
    m_GL->DeleteTextures(1, &m_IdTexture);
    m_GL->DeleteRenderbuffers(1, &m_DepthBuffer);
    m_FBO_ID = 0;
  }
  m_Overlay.Shutdown();
  m_GL = nullptr;
}

// ═══════════════════════════════════════════════════════════════
// 1. 工业级 MRT 帧缓冲 (RAII, 双附件 RGBA8 + R32I)
// ═══════════════════════════════════════════════════════════════
void ViewportPanel::InitMRTFramebuffer() {
  if (!m_GL || m_Width == 0 || m_Height == 0)
    return;

  // 🚨 核心修复：仅仅销毁现有的 FBO 资源，绝对不能调用 Cleanup()！
  if (m_FBO_ID) {
    m_GL->DeleteFramebuffers(1, &m_FBO_ID);
    m_GL->DeleteTextures(1, &m_ColorTexture);
    m_GL->DeleteTextures(1, &m_IdTexture);
    m_GL->DeleteRenderbuffers(1, &m_DepthBuffer);
    m_FBO_ID = 0;
  }

  m_GL->GenFramebuffers(1, &m_FBO_ID);
  m_GL->BindFramebuffer(GL_FRAMEBUFFER, m_FBO_ID);

  // Attachment 0: 颜色缓冲 RGBA8
  m_GL->GenTextures(1, &m_ColorTexture);
  m_GL->BindTexture(GL_TEXTURE_2D, m_ColorTexture);
  m_GL->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Width, m_Height, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, nullptr);
  m_GL->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  m_GL->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  m_GL->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, m_ColorTexture, 0);

  // Attachment 1: 实体 ID 缓冲 R32I (解决鼠标拾取)
  m_GL->GenTextures(1, &m_IdTexture);
  m_GL->BindTexture(GL_TEXTURE_2D, m_IdTexture);
  m_GL->TexImage2D(GL_TEXTURE_2D, 0, GL_R32I, m_Width, m_Height, 0,
                   GL_RED_INTEGER, GL_INT, nullptr);
  m_GL->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                      GL_NEAREST); // 必须是 NEAREST
  m_GL->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  m_GL->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                             GL_TEXTURE_2D, m_IdTexture, 0);

  // 深度缓冲
  m_GL->GenRenderbuffers(1, &m_DepthBuffer);
  m_GL->BindRenderbuffer(GL_RENDERBUFFER, m_DepthBuffer);
  m_GL->RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_Width,
                            m_Height);
  m_GL->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                GL_RENDERBUFFER, m_DepthBuffer);

  // 声明多目标渲染
  GLenum buffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
  m_GL->DrawBuffers(2, buffers);

  if (m_GL->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    ENGINE_LOG_ERROR("Viewport", "MRT Framebuffer incomplete!");
  }
  m_GL->BindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ═══════════════════════════════════════════════════════════════
// 2. OnUpdate — 仅负责相机平滑更新 + 渲染（输入处理移至 OnImGui）
// ═══════════════════════════════════════════════════════════════
void ViewportPanel::OnUpdate(float32 dt, bool) {
  if (!m_Camera)
    return;

  // 仅执行平滑运动的更新（惯性等）
  m_Camera->OnUpdate(dt);

  // 实时渲染（仅在窗口尺寸有效时执行，防止 FBO/GL 未初始化时的崩溃）
  if (m_Width > 0 && m_Height > 0 && m_GL && m_Factory)
    Render3DScene();
}

// ═══════════════════════════════════════════════════════════════
// 3. 渲染闭环 (使用 m_RenderWidth/Height 匹配 FBO 物理尺寸)
// ═══════════════════════════════════════════════════════════════
void ViewportPanel::Render3DScene() {
  if (!m_GL || !m_GL->GenFramebuffers) return;

  // ── 处理 FBO 重建（立即执行，无延迟） ──
  if (m_NeedsFBOUpdate || !m_FBO_ID) {
    InitMRTFramebuffer();
    m_NeedsFBOUpdate = false;
  }

  // ── 绑定 FBO ──
  m_GL->BindFramebuffer(GL_FRAMEBUFFER, m_FBO_ID);

  // 【关键】：Viewport 必须等于 FBO 的物理大小（m_RenderWidth/Height）
  GLsizei fbW = (GLsizei)(m_RenderWidth > 0 ? m_RenderWidth : m_Width);
  GLsizei fbH = (GLsizei)(m_RenderHeight > 0 ? m_RenderHeight : m_Height);
  if (fbW == 0 || fbH == 0) { m_GL->BindFramebuffer(GL_FRAMEBUFFER, 0); return; }

  m_GL->Viewport(0, 0, fbW, fbH);
  m_GL->ClearColor(0.12f, 0.12f, 0.15f, 1.0f);
  m_GL->Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  int clearId = -1;
  m_GL->ClearBufferiv(GL_COLOR, 1, &clearId);
  m_GL->Enable(GL_DEPTH_TEST);

  // ── 渲染场景 ──
  if (m_Camera && m_SceneRenderCallback) {
    float aspect = (float)fbW / (float)fbH;
    glm::mat4 proj =
        (m_Camera->GetProjectionType() == CameraProjectionType::Orthographic)
            ? glm::ortho(-m_Camera->GetDistance() * 0.5f * aspect,
                         m_Camera->GetDistance() * 0.5f * aspect,
                         -m_Camera->GetDistance() * 0.5f,
                         m_Camera->GetDistance() * 0.5f, 0.1f, 1000.0f)
            : glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
    glm::mat4 view =
        glm::lookAt(ToGlm(m_Camera->GetPosition()),
                    ToGlm(m_Camera->GetFocusPoint()), glm::vec3(0, 1, 0));
    glm::mat4 vp = proj * view;

    Vec3 cp = m_Camera->GetPosition();
    m_SceneRenderCallback(glm::value_ptr(vp), &cp.x, false);

    if (m_Config.ShowGrid && m_Overlay.IsInitialized()) {
      m_GL->Enable(GL_BLEND);
      m_GL->BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      m_Overlay.DrawGrid(m_GL, glm::value_ptr(vp), &cp.x, m_Config);
      m_GL->Disable(GL_BLEND);
    }
  }

  // ── 解绑 FBO ──
  m_GL->BindFramebuffer(GL_FRAMEBUFFER, 0);

  // 【RenderDoc 兼容】：恢复主屏幕视口，否则 RenderDoc 和 ImGui 渲染会异常
  ImGuiIO& io = ImGui::GetIO();
  if (io.DisplaySize.x > 0 && io.DisplaySize.y > 0)
    m_GL->Viewport(0, 0, (GLsizei)io.DisplaySize.x, (GLsizei)io.DisplaySize.y);
}

// ═══════════════════════════════════════════════════════════════
// 4. ImGui 事件截获（工业级重构：先图后按钮，视角控制内置于 ImGui）
// ═══════════════════════════════════════════════════════════════
void ViewportPanel::OnImGui() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  std::string windowName = m_Config.Name;
  bool visible = true;
  ImGui::Begin(windowName.c_str(), &visible,
               ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);

  // 1. 更新基础边界
  ImVec2 vMin = ImGui::GetWindowContentRegionMin();
  ImVec2 off = ImGui::GetWindowPos();
  m_ViewportBounds[0] = {vMin.x + off.x, vMin.y + off.y};
  m_ViewportBounds[1] = {
      m_ViewportBounds[0].x + ImGui::GetContentRegionAvail().x,
      m_ViewportBounds[0].y + ImGui::GetContentRegionAvail().y};

  ImVec2 viewportSize = ImGui::GetContentRegionAvail();

  // 2. 检查尺寸变化
  uint32 newDispW = (uint32)viewportSize.x;
  uint32 newDispH = (uint32)viewportSize.y;
  if (newDispW > 0 && newDispH > 0 &&
      (m_DisplayWidth != newDispW || m_DisplayHeight != newDispH ||
       m_Width == 0 || m_Height == 0)) {
    m_DisplayWidth = newDispW;
    m_DisplayHeight = newDispH;
    m_RenderWidth = (uint32)(m_DisplayWidth * m_ResolutionScale);
    m_RenderHeight = (uint32)(m_DisplayHeight * m_ResolutionScale);
    if (m_RenderWidth < 1) m_RenderWidth = 1;
    if (m_RenderHeight < 1) m_RenderHeight = 1;
    // 同步 m_Width/m_Height 供 OnUpdate 中的 Render3DScene 使用
    //（OnUpdate 执行在 OnImGui 之前，需要用上一次的值）
    m_Width = m_RenderWidth;
    m_Height = m_RenderHeight;
    m_NeedsFBOUpdate = true;
    if (m_Camera)
      m_Camera->SetViewportSize((float)m_RenderWidth, (float)m_RenderHeight);
  }

  // 3. 先绘制 3D 纹理底图
  if (m_ColorTexture) {
    ImGui::SetCursorScreenPos(m_ViewportBounds[0]);
    ImGui::Image((ImTextureID)(uint64)m_ColorTexture,
                 ImVec2((float)m_DisplayWidth, (float)m_DisplayHeight),
                 ImVec2(0, 1), ImVec2(1, 0));
  }

  // 4. 【交互层】：InvisibleButton 覆盖渲染区
  ImGui::SetCursorScreenPos(m_ViewportBounds[0]);
  ImGui::SetNextItemAllowOverlap();
  ImGui::InvisibleButton(
      "##VPInputLayer", ImVec2((float)m_DisplayWidth, (float)m_DisplayHeight),
      ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
          ImGuiButtonFlags_MouseButtonMiddle);

  // 5. 【状态捕获】：立即读取按钮状态
  m_Hovered = ImGui::IsItemHovered();
  m_Focused = ImGui::IsWindowFocused();

  // ── 视角控制：极简逻辑，委托 EditorCamera 处理一切 ──
  if (m_Camera) {
    bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    bool middleDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    bool isInteracting = rightDown || middleDown;
    bool canControl = (m_Hovered || m_Focused || isInteracting) &&
                      !ImGuizmo::IsOver() && !ImGuizmo::IsUsing();
    m_Camera->SetActive(canControl);
    if (canControl) {
      m_Camera->ProcessInput(ImGui::GetIO().DeltaTime);
    }
  }

  // ── 右键菜单：BeginPopupContextItem 绑定到 InvisibleButton ──
  if (ImGui::BeginPopupContextItem("##VPCtxMenu", ImGuiPopupFlags_MouseButtonRight)) {
    HandleViewportContextMenu();
    ImGui::EndPopup();
  }

  // ── 左键拾取 ──
  if (m_Hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
      !ImGuizmo::IsOver()) {
    ImVec2 mp = ImGui::GetMousePos();
    HandleMousePicking(
        (int)(mp.x - m_ViewportBounds[0].x),
        (int)(mp.y - m_ViewportBounds[0].y));
  }

  // 6. 绘制覆盖层
  DrawGizmos();
  DrawOverlay();
  DrawViewportStats();

  // 7. 键盘快捷键（右键飞行模式下跳过 WASD/QE 工具切换，避免与相机移动冲突）
  if (m_Focused) {
    bool isFlying = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (ImGui::IsKeyPressed(ImGuiKey_F))
      FocusOnSelected();
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && m_SceneDeleteCallback &&
        !m_SelectedObject.expired())
      m_SceneDeleteCallback();
    if (!isFlying) {
      if (ImGui::IsKeyPressed(ImGuiKey_Q))
        m_CurrentTool = ViewportTool::Select;
      if (ImGui::IsKeyPressed(ImGuiKey_W))
        m_CurrentTool = ViewportTool::Translate;
      if (ImGui::IsKeyPressed(ImGuiKey_E))
        m_CurrentTool = ViewportTool::Rotate;
      if (ImGui::IsKeyPressed(ImGuiKey_R))
        m_CurrentTool = ViewportTool::Scale;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_G))
      m_Config.ShowGrid = !m_Config.ShowGrid;
    if (ImGui::IsKeyPressed(ImGuiKey_Tab))
      CycleGizmoTool();
    if (m_Camera)
      HandleCameraBookmarkShortcuts(*m_Camera);
  }

  ImGui::End();
  ImGui::PopStyleVar();
}

void ViewportPanel::FocusOnSelected() {
  auto sel = m_SelectedObject.lock();
  if (!sel || !m_Camera)
    return;
  Vec3 pos = sel->GetTransform().GetPosition();
  m_Camera->SetFocusPoint(pos);
  m_Camera->SetDistance(5.0f);
}

// ═══════════════════════════════════════════════════════════════
// 5. 鼠标拾取: 从 ID 附件读取像素 (GL_COLOR_ATTACHMENT1)
// ═══════════════════════════════════════════════════════════════
void ViewportPanel::HandleMousePicking(int localX, int localY) {
  // ── 分辨率缩放适配：将显示坐标转为渲染坐标（拾取在 FBO 的渲染空间中进行） ──
  if (m_ResolutionScale < 1.0f && m_DisplayWidth > 0) {
    float scaleX = (float)m_RenderWidth / (float)m_DisplayWidth;
    float scaleY = (float)m_RenderHeight / (float)m_DisplayHeight;
    localX = (int)(localX * scaleX);
    localY = (int)(localY * scaleY);
  }
  int pixelY = (int)m_Height - localY;
  if (localX < 0 || localX >= (int)m_Width ||
      pixelY < 0 || pixelY >= (int)m_Height ||
      !m_FBO_ID || !m_GL)
    return;

  m_GL->BindFramebuffer(GL_FRAMEBUFFER, m_FBO_ID);
  m_GL->ReadBuffer(GL_COLOR_ATTACHMENT1);
  int32 eid = -1;
  m_GL->ReadPixels(localX, pixelY, 1, 1, GL_RED_INTEGER, GL_INT, &eid);
  m_GL->ReadBuffer(GL_COLOR_ATTACHMENT0);
  m_GL->BindFramebuffer(GL_FRAMEBUFFER, 0);

  if (eid > 65535) eid = -1;
  if (m_PickCallback) {
    float ndcX = (localX / (float)m_Width) * 2 - 1;
    float ndcY = 1 - (localY / (float)m_Height) * 2;
    m_PickCallback(ndcX, ndcY, eid);
  }
}

// ═══════════════════════════════════════════════════════════════
// 6. 右键上下文菜单（由 BeginPopupContextItem 驱动，不需手动判定右键）
// ═══════════════════════════════════════════════════════════════
void ViewportPanel::HandleViewportContextMenu() {
  auto selected = m_SelectedObject.lock();

  if (selected) {
    ImGui::TextDisabled("Entity: %s", selected->GetName().c_str());
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_CROSSHAIRS " Focus", "F"))
      FocusOnSelected();
    if (ImGui::MenuItem(ICON_FA_TRASH " Delete", "Del")) {
      if (m_SceneDeleteCallback) m_SceneDeleteCallback();
    }
  } else {
    ImGui::TextDisabled("Viewport Options");
    ImGui::Separator();
    if (ImGui::BeginMenu(ICON_FA_PLUS " Add...")) {
      if (ImGui::MenuItem("Cube"))
        if (m_SceneCreateCallback) m_SceneCreateCallback("Cube");
      if (ImGui::MenuItem("Point Light"))
        if (m_SceneCreateCallback) m_SceneCreateCallback("PointLight");
      ImGui::EndMenu();
    }
  }

  ImGui::Separator();
  if (ImGui::MenuItem(m_Config.ShowGrid ? "Hide Grid" : "Show Grid", "G")) {
    m_Config.ShowGrid = !m_Config.ShowGrid;
  }
}

// ═══════════════════════════════════════════════════════════════
// 8. Viewport 状态栏 (FPS, Draw Calls, GPU/CPU ms)
// ═══════════════════════════════════════════════════════════════
void ViewportPanel::DrawViewportStats() {
  ImVec2 pos(m_ViewportBounds[0].x + 10, m_ViewportBounds[1].y - 24);
  ImGui::SetCursorScreenPos(pos);

  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 0.85f));
  ImGui::Text("FPS: %.0f  |  GPU: %.1fms  |  CPU: %.1fms  |  DC: %u",
              ImGui::GetIO().Framerate,
              1000.0f / ImGui::GetIO().Framerate * 0.5f,
              ImGui::GetIO().DeltaTime * 1000.0f,
              m_GL ? 0u : 0u);
  ImGui::PopStyleColor();
}

// ═══════════════════════════════════════════════════════════════
// 9. Gizmo & Toolbar
// ═══════════════════════════════════════════════════════════════
void ViewportPanel::DrawGizmos() {
  auto sel = m_SelectedObject.lock();
  if (!sel || !m_Camera)
    return;
  if (m_CurrentTool != ViewportTool::Translate &&
      m_CurrentTool != ViewportTool::Rotate &&
      m_CurrentTool != ViewportTool::Scale)
    return;

  ImGuizmo::OPERATION op;
  switch (m_CurrentTool) {
  case ViewportTool::Translate:
    op = ImGuizmo::TRANSLATE;
    break;
  case ViewportTool::Rotate:
    op = ImGuizmo::ROTATE;
    break;
  case ViewportTool::Scale:
    op = ImGuizmo::SCALE;
    break;
  default:
    return;
  }
  ImGuizmo::SetOrthographic(m_Camera->GetProjectionType() ==
                            CameraProjectionType::Orthographic);
  ImGuizmo::SetDrawlist();
  float vpW = m_ViewportBounds[1].x - m_ViewportBounds[0].x;
  float vpH = m_ViewportBounds[1].y - m_ViewportBounds[0].y;
  ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y, vpW, vpH);
  auto &xf = sel->GetTransform();
  glm::mat4 model = glm::make_mat4(xf.GetWorldMatrix().Data());
  glm::mat4 view = glm::make_mat4(m_Camera->GetViewMatrixPtr());
  glm::mat4 proj = glm::make_mat4(m_Camera->GetProjectionMatrixPtr());
  float sv[3] = {m_SnapValue, m_SnapValue, m_SnapValue};
  if (m_CurrentTool == ViewportTool::Rotate)
    sv[0] = sv[1] = sv[2] = 45.0f;
  bool snap =
      m_SnapEnabled || (Input::Get() && Input::IsKeyDown(KeyCode::LeftCtrl));
  ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op,
                       m_GizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
                       glm::value_ptr(model), nullptr, snap ? sv : nullptr);
  if (ImGuizmo::IsUsing()) {
    glm::vec3 t, s, sk;
    glm::quat r;
    glm::vec4 p;
    glm::decompose(model, s, r, t, sk, p);
    xf.SetPosition(Vec3(t.x, t.y, t.z));
    glm::vec3 euler = glm::degrees(glm::eulerAngles(r));
    xf.SetRotation(Vec3(euler.x, euler.y, euler.z));
    xf.SetScale(Vec3(s.x, s.y, s.z));
  }
}

void ViewportPanel::DrawOverlay() {
  ImVec2 pos(m_ViewportBounds[0].x + 10, m_ViewportBounds[0].y + 10);
  ImGui::SetCursorScreenPos(pos);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 0.85f));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
  if (ImGui::BeginChild("##VPToolbar", ImVec2(400, 32), false,
                        ImGuiWindowFlags_NoScrollbar)) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 0));

    auto TB = [&](const char *ic, ViewportTool tl, const char *tip) {
      bool a = (m_CurrentTool == tl);
      if (a)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
      if (ImGui::Button(ic))
        m_CurrentTool = tl;
      if (a)
        ImGui::PopStyleColor();
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
      ImGui::SameLine();
    };
    TB(ICON_FA_MOUSE_POINTER, ViewportTool::Select, "Select (Q)");
    TB(ICON_FA_HAND_POINTER, ViewportTool::Hand, "Pan (H)");
    TB(ICON_FA_ARROWS, ViewportTool::Translate, "Move (W)");
    TB(ICON_FA_ROTATE_LEFT, ViewportTool::Rotate, "Rotate (E)");
    TB(ICON_FA_EXPAND, ViewportTool::Scale, "Scale (R)");

    ImGui::Separator();
    ImGui::SameLine();
    if (ImGui::Button(m_GizmoLocal ? ICON_FA_CUBE : ICON_FA_DOT_CIRCLE))
      m_GizmoLocal = !m_GizmoLocal;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(m_GizmoLocal ? "Local" : "World");
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_MAGNET))
      m_SnapEnabled = !m_SnapEnabled;
    if (m_SnapEnabled) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(50);
      ImGui::DragFloat("##snap", &m_SnapValue, 0.01f, 0.01f, 100.0f, "%.2f");
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Snap");

    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_GRID))
      m_Config.ShowGrid = !m_Config.ShowGrid;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Grid (G)");

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
  }
  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

} // namespace Engine