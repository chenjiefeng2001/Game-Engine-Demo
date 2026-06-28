#include "Engine/Editor/EditorCamera.h"
#include "Engine/Core/Input.h"
#include <GLFW/glfw3.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <cstring>
#include <algorithm>

namespace Engine {

    namespace {
        inline glm::vec3 ToGlm(const Vec3& v) { return glm::vec3(v.x, v.y, v.z); }
        inline Vec3 ToEngine(const glm::vec3& v) { return Vec3(v.x, v.y, v.z); }
        inline glm::mat4 ToGlmMat(const Mat4& m) { glm::mat4 r; std::memcpy(&r, m.data, 16*4); return r; }
        inline void StoreGlm(const glm::mat4& src, Mat4& dst) { std::memcpy(dst.data, &src, 16*4); }
        inline void DirectionToYawPitch(const glm::vec3& dir, float32& yaw, float32& pitch) {
            yaw = glm::degrees(std::atan2(dir.z, dir.x));
            pitch = glm::degrees(std::asin(dir.y));
        }
    }

    EditorCamera::EditorCamera() { Reset(); }

    void EditorCamera::Reset() {
        m_Position = {0,0,10}; m_FocusPoint = {0,0,0}; m_Distance = 10;
        m_Yaw = 0; m_Pitch = 0; m_OrbitYaw = 0; m_OrbitPitch = -20;
        m_FlySpeed = 5; m_SmoothPosition = {0,0,10};
        m_SmoothYaw = 0; m_SmoothPitch = 0; m_SmoothZoom = m_Distance;
        m_Dirty = true; m_SnapMode = SnapMode::None; m_GridSnapSize = 0.5f;
    }

    void EditorCamera::SetViewportSize(float32 w, float32 h) {
        if (m_ViewportWidth != w || m_ViewportHeight != h) {
            m_ViewportWidth = (std::max)(w, 1.0f); m_ViewportHeight = (std::max)(h, 1.0f); m_Dirty = true;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 空间查询
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

    void EditorCamera::ScreenToRay(float ndcX, float ndcY, Vec3& outOrigin, Vec3& outDirection) const {
        // 从 NDC [-1,1] 计算射线
        float32 aspect = m_ViewportWidth / m_ViewportHeight;
        glm::mat4 proj;
        if (m_ProjectionType == CameraProjectionType::Orthographic) {
            float32 zoom = m_Distance * 0.5f;
            proj = glm::ortho(-zoom*aspect, zoom*aspect, -zoom, zoom, 0.1f, 1000.0f);
        } else {
            proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
        }
        glm::mat4 view = glm::make_mat4(m_ViewMatrix.Data());
        glm::mat4 invVP = glm::inverse(proj * view);

        glm::vec4 nearPoint = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
        glm::vec4 farPoint  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        nearPoint /= nearPoint.w; farPoint /= farPoint.w;

        outOrigin = ToEngine(glm::vec3(nearPoint));
        outDirection = ToEngine(glm::normalize(glm::vec3(farPoint) - glm::vec3(nearPoint)));
    }

    // ═══════════════════════════════════════════════════════════════
    // 吸附系统
    // ═══════════════════════════════════════════════════════════════
    Vec3 EditorCamera::SnapToGrid(const Vec3& pos) const {
        if (m_SnapMode != SnapMode::Grid || m_GridSnapSize <= 0.0f) return pos;
        float32 s = m_GridSnapSize;
        return Vec3(
            std::round(pos.x / s) * s,
            std::round(pos.y / s) * s,
            std::round(pos.z / s) * s
        );
    }
    float32 EditorCamera::SnapAngle(float32 degrees) const {
        if (m_SnapMode != SnapMode::Grid) return degrees;
        float32 s = 15.0f; // 15度步进
        return std::round(degrees / s) * s;
    }

    // ═══════════════════════════════════════════════════════════════
    // 输入处理
    // ═══════════════════════════════════════════════════════════════
    void EditorCamera::ProcessInput(float32 dt) {
        if (!m_Active) return;
        if (m_RotationLocked) {
            IInput* input = Input::Get(); if (!input) return;
            m_RightMouseDown = input->IsMouseButtonDown(MouseCode::ButtonRight);
            m_MiddleMouseDown = input->IsMouseButtonDown(MouseCode::ButtonMiddle);
            m_AltHeld = input->IsKeyDown(KeyCode::LeftAlt) || input->IsKeyDown(KeyCode::RightAlt);
            float32 mx = (float32)input->GetMouseX(), my = (float32)input->GetMouseY();
            float32 dx = mx - m_LastMouseX, dy = my - m_LastMouseY;
            m_LastMouseX = mx; m_LastMouseY = my;
            bool pan = m_MiddleMouseDown || (m_AltHeld && input->IsMouseButtonDown(MouseCode::ButtonMiddle));
            if (pan) { float32 ps = m_Distance * 0.002f; m_PanOffset.x += -dx*ps; m_PanOffset.y += dy*ps; m_Dirty = true; }
            float32 sc = input->GetScrollDelta();
            if (sc != 0) { m_SmoothZoom -= sc * m_Distance * 0.1f; m_SmoothZoom = (std::max)(m_SmoothZoom, 0.1f); m_Dirty = true; }
            return;
        }
        IInput* input = Input::Get(); if (!input) return;
        m_RightMouseDown = input->IsMouseButtonDown(MouseCode::ButtonRight);
        m_MiddleMouseDown = input->IsMouseButtonDown(MouseCode::ButtonMiddle);
        m_AltHeld = input->IsKeyDown(KeyCode::LeftAlt) || input->IsKeyDown(KeyCode::RightAlt);
        float32 mx = (float32)input->GetMouseX(), my = (float32)input->GetMouseY();
        float32 dx = mx - m_LastMouseX, dy = my - m_LastMouseY;
        m_LastMouseX = mx; m_LastMouseY = my;

        bool fly = m_RightMouseDown && !m_AltHeld;
        bool orbit = m_AltHeld && input->IsMouseButtonDown(MouseCode::ButtonLeft);
        bool pan = m_MiddleMouseDown || (m_AltHeld && input->IsMouseButtonDown(MouseCode::ButtonMiddle));

        if (fly) { ProcessFlyInput(dt); m_Yaw += dx*0.2f; m_Pitch = glm::clamp(m_Pitch - dy*0.2f, -89.0f, 89.0f); m_Dirty = true; }
        if (orbit) { m_OrbitVelocityYaw = dx*m_OrbitSpeed; m_OrbitVelocityPitch = dy*m_OrbitSpeed; m_OrbitYaw += m_OrbitVelocityYaw; m_OrbitPitch = glm::clamp(m_OrbitPitch - m_OrbitVelocityPitch, -89.0f, 89.0f); m_Dirty = true; }
        if (pan) { float32 ps = m_Distance * 0.002f; m_PanOffset.x += -dx*ps; m_PanOffset.y += dy*ps; m_Dirty = true; }
        float32 sc = input->GetScrollDelta();
        if (sc != 0) { m_SmoothZoom -= sc * m_Distance * 0.1f; m_SmoothZoom = (std::max)(m_SmoothZoom, 0.1f); m_Dirty = true; }
    }

    void EditorCamera::ProcessFlyInput(float32 dt) {
        IInput* input = Input::Get(); if (!input) return;
        float32 yawRad = glm::radians(m_Yaw), pitchRad = glm::radians(m_Pitch);
        glm::vec3 forward(std::cos(pitchRad)*std::cos(yawRad), std::sin(pitchRad), std::cos(pitchRad)*std::sin(yawRad));
        forward = glm::normalize(forward);
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0,1,0)));
        glm::vec3 up(0,1,0);
        glm::vec3 move(0);
        float32 speed = m_FlySpeed * dt;
        if (input->IsKeyDown(KeyCode::LeftShift)) speed *= 3.0f;
        if (input->IsKeyDown(KeyCode::W)) move += forward;
        if (input->IsKeyDown(KeyCode::S)) move -= forward;
        if (input->IsKeyDown(KeyCode::A)) move -= right;
        if (input->IsKeyDown(KeyCode::D)) move += right;
        if (input->IsKeyDown(KeyCode::E)) move += up;
        if (input->IsKeyDown(KeyCode::Q)) move -= up;
        float32 len = glm::length(move);
        if (len > 0.0f) {
            move /= len;
            glm::vec3 cp = ToGlm(m_Position);
            glm::vec3 np = cp + move * speed;
            m_Position = ToEngine(np);
            m_FocusPoint = ToEngine(np + forward * m_Distance);
            m_SmoothPosition = m_Position;
            m_Dirty = true;
        }
    }

    void EditorCamera::OnUpdate(float32 dt) {
        if (!m_Dirty && !m_Active) return;
        IInput* input = Input::Get();
        if (m_Active) {
            glm::vec3 targetPos = ToGlm(m_Position) + ToGlm(m_PanOffset);
            if (m_RightMouseDown && !m_AltHeld) m_Position = ToEngine(targetPos);
            else if (m_AltHeld && input && input->IsMouseButtonDown(MouseCode::ButtonLeft)) {}
            else {
                Vec3 t = ToEngine(targetPos);
                SmoothDamp(m_Position, t, m_SmoothVelocity, 0.15f, dt);
                m_OrbitVelocityYaw *= 0.9f; m_OrbitVelocityPitch *= 0.9f;
                m_OrbitYaw += m_OrbitVelocityYaw * dt; m_OrbitPitch += m_OrbitVelocityPitch * dt;
                float32 tz = m_SmoothZoom; m_Distance += (tz - m_Distance) * (1.0f - std::exp(-10.0f * dt));
            }
        }
        m_PanOffset = {0,0,0};
        if (m_Dirty) { RecalculateMatrices(); m_Dirty = false; }
    }

    void EditorCamera::RecalculateMatrices() {
        float32 aspect = m_ViewportWidth / m_ViewportHeight;
        glm::mat4 proj;
        if (m_ProjectionType == CameraProjectionType::Orthographic) {
            float32 zoom = m_Distance * 0.5f;
            proj = glm::ortho(-zoom*aspect, zoom*aspect, -zoom, zoom, 0.1f, 1000.0f);
        } else proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
        StoreGlm(proj, m_ProjectionMatrix);

        glm::mat4 view(1.0f); glm::vec3 eye(0.0f);
        if (m_RightMouseDown && !m_AltHeld) {
            eye = ToGlm(m_Position) + ToGlm(m_PanOffset);
            float32 yr = glm::radians(m_Yaw), pr = glm::radians(m_Pitch);
            glm::vec3 fwd(std::cos(pr)*std::cos(yr), std::sin(pr), std::cos(pr)*std::sin(yr));
            view = glm::lookAt(eye, eye + fwd, glm::vec3(0,1,0));
        } else {
            float32 yr = glm::radians(m_OrbitYaw), pr = glm::radians(m_OrbitPitch);
            glm::vec3 offset(m_Distance*std::cos(pr)*std::cos(yr), m_Distance*std::sin(pr), m_Distance*std::cos(pr)*std::sin(yr));
            glm::vec3 focus = ToGlm(m_FocusPoint) + ToGlm(m_PanOffset);
            eye = focus + offset;
            view = glm::lookAt(eye, focus, glm::vec3(0,1,0));
            m_Position = ToEngine(eye);
        }
        StoreGlm(view, m_ViewMatrix);
        glm::mat4 vp = proj * view;
        StoreGlm(vp, m_ViewProjectionMatrix);
    }

    void EditorCamera::ProcessPanInput(float32) {}
    void EditorCamera::ProcessOrbitInput(float32) {}
    void EditorCamera::ProcessScrollInput(float32) {}

    void EditorCamera::SmoothDamp(Vec3& current, const Vec3& target, Vec3& velocity, float32 smoothTime, float32 dt) {
        glm::vec3 cur = ToGlm(current), tgt = ToGlm(target), vel = ToGlm(velocity);
        float32 omega = 2.0f / smoothTime;
        glm::vec3 diff = cur - tgt;
        float32 epsilon = 0.001f;
        glm::vec3 temp = vel + omega * diff;
        glm::vec3 damped = cur - diff * (1.0f - std::exp(-omega * dt * 0.5f));
        if (glm::length(diff) < epsilon && glm::length(vel) < epsilon) { current = target; velocity = {0,0,0}; return; }
        velocity = ToEngine(temp * std::exp(-omega * dt * 0.5f));
        current = ToEngine(damped);
    }

} // namespace Engine