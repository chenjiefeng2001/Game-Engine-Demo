#include "Engine/Editor/EditorCamera.h"
#include <imgui.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cstring>

namespace Engine {

    namespace {
        inline glm::vec3 ToGlm(const Vec3& v) { return glm::vec3(v.x, v.y, v.z); }
        inline Vec3 ToEngine(const glm::vec3& v) { return Vec3(v.x, v.y, v.z); }
        inline void StoreGlm(const glm::mat4& src, Mat4& dst) { std::memcpy(dst.data, &src, sizeof(float32) * 16); }
    }

    EditorCamera::EditorCamera() { Reset(); }

    void EditorCamera::Reset() {
        m_FocusPoint  = { 0.0f, 0.0f, 0.0f };
        m_Distance    = 10.0f;
        m_SmoothZoom  = m_Distance;
        m_Yaw         = 45.0f;
        m_Pitch       = -30.0f;
        m_FlySpeed    = 5.0f;
        m_Dirty       = true;
        m_SnapMode    = SnapMode::None;
        m_GridSnapSize = 0.5f;
    }

    void EditorCamera::SetViewportSize(float32 width, float32 height) {
        if (m_ViewportWidth != width || m_ViewportHeight != height) {
            m_ViewportWidth  = (std::max)(width, 1.0f);
            m_ViewportHeight = (std::max)(height, 1.0f);
            m_Dirty = true;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 空间查询（由 RecalculateMatrices 同步 m_Position，可直接读取）
    // ═══════════════════════════════════════════════════════════════
    Vec3 EditorCamera::GetForwardVector() const {
        float32 yr = glm::radians(m_Yaw), pr = glm::radians(m_Pitch);
        return ToEngine(glm::normalize(glm::vec3(std::cos(pr)*std::cos(yr), std::sin(pr), std::cos(pr)*std::sin(yr))));
    }
    Vec3 EditorCamera::GetRightVector() const {
        Vec3 fwd = GetForwardVector();
        return ToEngine(glm::normalize(glm::cross(ToGlm(fwd), glm::vec3(0,1,0))));
    }
    Vec3 EditorCamera::GetUpVector() const {
        Vec3 fwd = GetForwardVector(), right = GetRightVector();
        return ToEngine(glm::normalize(glm::cross(ToGlm(right), ToGlm(fwd))));
    }

    // ═══════════════════════════════════════════════════════════════
    // 核心交互输入（完全基于 ImGui，焦点驱动数学模型）
    // ═══════════════════════════════════════════════════════════════
    void EditorCamera::ProcessInput(float32 dt) {
        if (!m_Active) return;

        float deltaX, deltaY, scroll;
        bool rightDown, middleDown, leftDown, altHeld, shiftHeld;
        bool keyW, keyS, keyA, keyD, keyE, keyQ;
        bool keyUp, keyDown, keyLeft, keyRight;

        if (m_ExternalInput) {
            // Phase 7: Use external input from Bridge/Avalonia
            const auto& ext = m_ExternalInputState;
            deltaX = ext.mouseDeltaX;
            deltaY = ext.mouseDeltaY;
            scroll = ext.mouseWheel;
            rightDown = ext.rightDown;
            middleDown = ext.middleDown;
            leftDown = ext.leftDown;
            altHeld = ext.altHeld;
            shiftHeld = ext.shiftHeld;
            keyW = ext.keyW; keyS = ext.keyS;
            keyA = ext.keyA; keyD = ext.keyD;
            keyE = ext.keyE; keyQ = ext.keyQ;
            keyUp = ext.keyUp; keyDown = ext.keyDown;
            keyLeft = ext.keyLeft; keyRight = ext.keyRight;
        } else {
            // Legacy: Read from ImGui
            ImGuiIO& io = ImGui::GetIO();
            deltaX = io.MouseDelta.x;
            deltaY = io.MouseDelta.y;
            scroll = io.MouseWheel;
            rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
            middleDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
            leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            altHeld = io.KeyAlt;
            shiftHeld = io.KeyShift;
            keyW = ImGui::IsKeyDown(ImGuiKey_W);
            keyS = ImGui::IsKeyDown(ImGuiKey_S);
            keyA = ImGui::IsKeyDown(ImGuiKey_A);
            keyD = ImGui::IsKeyDown(ImGuiKey_D);
            keyE = ImGui::IsKeyDown(ImGuiKey_E);
            keyQ = ImGui::IsKeyDown(ImGuiKey_Q);
            keyUp = ImGui::IsKeyDown(ImGuiKey_UpArrow);
            keyDown = ImGui::IsKeyDown(ImGuiKey_DownArrow);
            keyLeft = ImGui::IsKeyDown(ImGuiKey_LeftArrow);
            keyRight = ImGui::IsKeyDown(ImGuiKey_RightArrow);
        }

        // ── 计算当前相机的局部方向系（基于欧拉角） ──
        float yawRad = glm::radians(m_Yaw);
        float pitchRad = glm::radians(m_Pitch);
        glm::vec3 forward = glm::normalize(glm::vec3(
            cos(pitchRad) * cos(yawRad),
            sin(pitchRad),
            cos(pitchRad) * sin(yawRad)));
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 up = glm::normalize(glm::cross(right, forward));

        // ── 1. 滚轮缩放（修改平滑目标值，避免与 Distance 死锁） ──
        if (scroll != 0.0f) {
            m_SmoothZoom -= scroll * m_SmoothZoom * 0.2f;
            m_SmoothZoom = std::clamp(m_SmoothZoom, 0.1f, 10000.0f);
            m_Dirty = true;
        }

        // ── 2. 中键平移 (Pan) — 移动 FocusPoint ──
        if (middleDown || (altHeld && leftDown)) {
            float panSpeed = m_Distance * 0.0015f;
            glm::vec3 panDelta = -right * (deltaX * panSpeed) + up * (deltaY * panSpeed);
            m_FocusPoint = ToEngine(ToGlm(m_FocusPoint) + panDelta);
            m_Dirty = true;
        }

        // ── 3. 右键漫游 (Fly Mode: 旋转 + WASD + 方向键) ──
        if (rightDown && !altHeld) {
            // 鼠标旋转
            m_Yaw += deltaX * 0.2f;
            m_Pitch -= deltaY * 0.2f;
            m_Pitch = std::clamp(m_Pitch, -89.0f, 89.0f);

            // 键盘移动（WASD + 方向键），焦点同步移动
            glm::vec3 move(0.0f);
            if (keyW || keyUp)    move += forward;
            if (keyS || keyDown)  move -= forward;
            if (keyA || keyLeft)  move -= right;
            if (keyD || keyRight) move += right;
            if (keyE) move += glm::vec3(0.0f, 1.0f, 0.0f);
            if (keyQ) move -= glm::vec3(0.0f, 1.0f, 0.0f);

            if (glm::length(move) > 0.0f) {
                float speed = m_FlySpeed * dt;
                if (shiftHeld) speed *= 3.0f;
                glm::vec3 moveDelta = glm::normalize(move) * speed;
                m_FocusPoint = ToEngine(ToGlm(m_FocusPoint) + moveDelta);
            }
            m_Dirty = true;
        }

        // ── 4. Alt + 左键（轨道旋转 Orbit） ──
        if (altHeld && leftDown) {
            m_Yaw += deltaX * 0.3f;
            m_Pitch -= deltaY * 0.3f;
            m_Pitch = std::clamp(m_Pitch, -89.0f, 89.0f);
            m_Dirty = true;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 更新与矩阵重算
    // ═══════════════════════════════════════════════════════════════
    void EditorCamera::OnUpdate(float32 dt) {
        // ── 缩放阻尼平滑过渡（指数衰减，防止回弹锁死） ──
        if (std::abs(m_SmoothZoom - m_Distance) > 0.001f) {
            m_Distance += (m_SmoothZoom - m_Distance) * (1.0f - std::exp(-15.0f * dt));
            m_Dirty = true;
        }

        if (m_Dirty) {
            RecalculateMatrices();
            m_Dirty = false;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 矩阵重算（焦点驱动：eye = focus - forward * distance）
    // ═══════════════════════════════════════════════════════════════
    void EditorCamera::RecalculateMatrices() {
        // 1. 投影矩阵
        float32 aspect = m_ViewportWidth / m_ViewportHeight;
        glm::mat4 proj;
        if (m_ProjectionType == CameraProjectionType::Orthographic) {
            float32 zoomLevel = m_Distance * 0.5f;
            proj = glm::ortho(-zoomLevel * aspect, zoomLevel * aspect,
                              -zoomLevel, zoomLevel, 0.1f, 1000.0f);
        } else {
            proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
        }
        StoreGlm(proj, m_ProjectionMatrix);

        // 2. 视图矩阵（焦点驱动：相机位置 = 焦点 - 前方向量 × 距离）
        float32 yawRad   = glm::radians(m_Yaw);
        float32 pitchRad = glm::radians(m_Pitch);

        glm::vec3 forward = glm::normalize(glm::vec3(
            cos(pitchRad) * cos(yawRad),
            sin(pitchRad),
            cos(pitchRad) * sin(yawRad)));

        glm::vec3 focus = ToGlm(m_FocusPoint);
        glm::vec3 eye = focus - forward * m_Distance;

        m_Position = ToEngine(eye);
        glm::mat4 view = glm::lookAt(eye, focus, glm::vec3(0.0f, 1.0f, 0.0f));
        StoreGlm(view, m_ViewMatrix);

        glm::mat4 vp = proj * view;
        StoreGlm(vp, m_ViewProjectionMatrix);
    }

    // ── 废弃（由焦点驱动模型替代） ──
    void EditorCamera::ProcessPanInput(float32) {}
    void EditorCamera::ProcessOrbitInput(float32) {}
    void EditorCamera::ProcessScrollInput(float32) {}
    void EditorCamera::ProcessPanDirect(float32, float32) {}
    void EditorCamera::SmoothDamp(Vec3&, const Vec3&, Vec3&, float32, float32) {}

} // namespace Engine