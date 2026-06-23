#include "Engine/Editor/EditorCamera.h"
#include "Engine/Core/Input.h"
#include <GLFW/glfw3.h>  // 用于原始的鼠标按钮和滚轮查询（绕过 Input 的 block 机制）
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cstring>
#include <algorithm>

namespace Engine {

    // ============================================================
    // 辅助：MathTypes ↔ glm 转换
    // ============================================================
    namespace {
        inline glm::vec3 ToGlm(const Vec3& v) {
            return glm::vec3(v.x, v.y, v.z);
        }

        inline Vec3 ToEngine(const glm::vec3& v) {
            return Vec3(v.x, v.y, v.z);
        }

        inline glm::mat4 ToGlmMat(const Mat4& m) {
            glm::mat4 result;
            std::memcpy(&result, m.data, sizeof(float32) * 16);
            return result;
        }

        inline void StoreGlm(const glm::mat4& src, Mat4& dst) {
            std::memcpy(dst.data, &src, sizeof(float32) * 16);
        }

        /** 从世界坐标 Y-up 视角方向计算偏航和俯仰 */
        inline void DirectionToYawPitch(const glm::vec3& dir, float32& yaw, float32& pitch) {
            yaw   = glm::degrees(std::atan2(dir.z, dir.x));
            pitch = glm::degrees(std::asin(dir.y));
        }
    }

    // ============================================================
    // 构造 / 析构
    // ============================================================

    EditorCamera::EditorCamera() {
        Reset();
    }

    void EditorCamera::Reset() {
        m_Position    = Vec3(0.0f, 0.0f, 10.0f);
        m_FocusPoint  = { 0.0f, 0.0f, 0.0f };
        m_Distance    = 10.0f;
        m_Yaw         = 0.0f;
        m_Pitch       = 0.0f;
        m_OrbitYaw    = 0.0f;
        m_OrbitPitch  = -20.0f;
        m_FlySpeed    = 5.0f;
        m_SmoothPosition = Vec3(0.0f, 0.0f, 10.0f);
        m_SmoothYaw   = 0.0f;
        m_SmoothPitch = 0.0f;
        m_SmoothZoom  = m_Distance;
        m_Dirty       = true;
    }

    // ============================================================
    // 视口配置
    // ============================================================

    void EditorCamera::SetViewportSize(float32 width, float32 height) {
        if (m_ViewportWidth != width || m_ViewportHeight != height) {
            m_ViewportWidth  = (std::max)(width, 1.0f);
            m_ViewportHeight = (std::max)(height, 1.0f);
            m_Dirty = true;
        }
    }

    // ============================================================
    // 输入处理
    // ============================================================

    void EditorCamera::ProcessInput(float32 dt) {
        if (!m_Active) return;

        // ── 旋转锁定 → 忽略鼠标旋转输入 ──
        if (m_RotationLocked) {
            // 仍然处理平移和缩放
            IInput* input = Input::Get();
            if (!input) return;

            m_RightMouseDown  = input->IsMouseButtonDown(MouseCode::ButtonRight);
            m_MiddleMouseDown = input->IsMouseButtonDown(MouseCode::ButtonMiddle);
            m_AltHeld         = input->IsKeyDown(KeyCode::LeftAlt) ||
                                input->IsKeyDown(KeyCode::RightAlt);

            float32 mouseX = static_cast<float32>(input->GetMouseX());
            float32 mouseY = static_cast<float32>(input->GetMouseY());
            float32 deltaX = mouseX - m_LastMouseX;
            float32 deltaY = mouseY - m_LastMouseY;
            m_LastMouseX = mouseX;
            m_LastMouseY = mouseY;

            bool panMode = m_MiddleMouseDown || (m_AltHeld && input->IsMouseButtonDown(MouseCode::ButtonMiddle));
            if (panMode) {
                ProcessPanInput(dt);
                float32 panSpeed = m_Distance * 0.002f;
                m_PanOffset.x += -deltaX * panSpeed;
                m_PanOffset.y +=  deltaY * panSpeed;
                m_Dirty = true;
            }

            float32 scroll = input->GetScrollDelta();
            if (scroll != 0.0f) {
                m_SmoothZoom -= scroll * (m_Distance * 0.1f);
                m_SmoothZoom  = (std::max)(m_SmoothZoom, 0.1f);
                m_Dirty = true;
            }
            return;
        }

        // ── 鼠标状态 ──
        IInput* input = Input::Get();
        if (!input) return;

        m_RightMouseDown  = input->IsMouseButtonDown(MouseCode::ButtonRight);
        m_MiddleMouseDown = input->IsMouseButtonDown(MouseCode::ButtonMiddle);
        m_AltHeld         = input->IsKeyDown(KeyCode::LeftAlt) ||
                            input->IsKeyDown(KeyCode::RightAlt);

        float32 mouseX = static_cast<float32>(input->GetMouseX());
        float32 mouseY = static_cast<float32>(input->GetMouseY());

        float32 deltaX = mouseX - m_LastMouseX;
        float32 deltaY = mouseY - m_LastMouseY;
        m_LastMouseX = mouseX;
        m_LastMouseY = mouseY;

        // ── 模式判断 ──
        bool flyMode   = m_RightMouseDown && !m_AltHeld;
        bool orbitMode = m_AltHeld && input->IsMouseButtonDown(MouseCode::ButtonLeft);
        bool panMode   = m_MiddleMouseDown || (m_AltHeld && input->IsMouseButtonDown(MouseCode::ButtonMiddle));

        // ── 飞行模式（右键 + WASD） ──
        if (flyMode) {
            ProcessFlyInput(dt);
            // 鼠标移动 → 旋转视角
            m_Yaw   += deltaX * 0.2f;
            m_Pitch  = glm::clamp(m_Pitch - deltaY * 0.2f, -89.0f, 89.0f);
            m_Dirty = true;
        }

        // ── 轨道模式（Alt + 左键） ──
        if (orbitMode) {
            m_OrbitVelocityYaw   = deltaX * m_OrbitSpeed;
            m_OrbitVelocityPitch = deltaY * m_OrbitSpeed;
            m_OrbitYaw   += m_OrbitVelocityYaw;
            m_OrbitPitch  = glm::clamp(m_OrbitPitch - m_OrbitVelocityPitch, -89.0f, 89.0f);
            m_Dirty = true;
        }

        // ── 平移模式（中键 / Alt+中键） ──
        if (panMode) {
            ProcessPanInput(dt);
            // 计算屏幕平移 → 世界平移
            float32 panSpeed = m_Distance * 0.002f;
            m_PanOffset.x += -deltaX * panSpeed;
            m_PanOffset.y +=  deltaY * panSpeed;
            m_Dirty = true;
        }

        // ── 滚轮缩放 ──
        float32 scroll = input->GetScrollDelta();
        if (scroll != 0.0f) {
            m_SmoothZoom -= scroll * (m_Distance * 0.1f);
            m_SmoothZoom  = (std::max)(m_SmoothZoom, 0.1f);
            m_Dirty = true;
        }
    }

    // ============================================================
    // 飞行输入
    // ============================================================

    void EditorCamera::ProcessFlyInput(float32 dt) {
        IInput* input = Input::Get();
        if (!input) return;

        // 构建前/右方向（忽略俯仰的前方向）
        float32 yawRad = glm::radians(m_Yaw);
        glm::vec3 forward(std::cos(yawRad), 0.0f, std::sin(yawRad));
        glm::vec3 right(std::cos(yawRad + glm::half_pi<float32>()), 0.0f,
                        std::sin(yawRad + glm::half_pi<float32>()));
        glm::vec3 up(0.0f, 1.0f, 0.0f);

        glm::vec3 move(0.0f);
        if (input->IsKeyDown(KeyCode::W)) move += forward;
        if (input->IsKeyDown(KeyCode::S)) move -= forward;
        if (input->IsKeyDown(KeyCode::A)) move -= right;
        if (input->IsKeyDown(KeyCode::D)) move += right;
        if (input->IsKeyDown(KeyCode::Q)) move -= up;
        if (input->IsKeyDown(KeyCode::E)) move += up;

        float32 len = glm::length(move);
        if (len > 0.0f) {
            move /= len;
            glm::vec3 newPos = ToGlm(m_SmoothPosition) + move * m_FlySpeed * dt;
            m_SmoothPosition = ToEngine(newPos);
            m_Dirty = true;
        }
    }

    /// 鼠标滚轮缩放阻尼值
    namespace {
        constexpr float32 kZoomSmoothTime = 0.1f;
    }

    // ============================================================
    // Update — 每帧调用
    // ============================================================

    void EditorCamera::OnUpdate(float32 dt) {
        if (!m_Dirty && !m_Active) return;

        IInput* input = Input::Get();

        // ── 平滑阻尼更新 ──
        // 缩放：在正式相机模式下使用轨道距离 + 平滑
        if (m_Active) {
            // 来自飞行的位置平滑
            glm::vec3 targetPos = ToGlm(m_Position) + ToGlm(m_PanOffset);
            if (m_RightMouseDown && !m_AltHeld) {
                // 飞行模式：立即跟随
                m_Position = ToEngine(targetPos);
            } else if (m_AltHeld && input && input->IsMouseButtonDown(MouseCode::ButtonLeft)) {
                // 轨道模式：直接更新角度
                // 位置在 RecalculateMatrices 中根据轨道参数计算
            } else {
                // 非拖拽状态：平滑阻尼停稳
                Vec3 target = ToEngine(targetPos);
                SmoothDamp(m_Position, target, m_SmoothVelocity, 0.15f, dt);

                // 轨道阻尼
                m_OrbitVelocityYaw   *= 0.9f;
                m_OrbitVelocityPitch *= 0.9f;
                m_OrbitYaw   += m_OrbitVelocityYaw * dt;
                m_OrbitPitch += m_OrbitVelocityPitch * dt;

                // 缩放阻尼
                float32 targetZoom = m_SmoothZoom;
                m_Distance += (targetZoom - m_Distance) * (1.0f - std::exp(-10.0f * dt));
            }
        }

        // ── 重置偏移（应用后清除） ──
        m_PanOffset = { 0.0f, 0.0f, 0.0f };

        // ── 如果脏了，重算矩阵 ──
        if (m_Dirty) {
            RecalculateMatrices();
            m_Dirty = false;
        }
    }

    // ============================================================
    // 矩阵计算
    // ============================================================

    void EditorCamera::RecalculateMatrices() {
        // ── 投影矩阵（根据类型选择透视/正交） ──
        float32 aspect = m_ViewportWidth / m_ViewportHeight;
        glm::mat4 proj;

        if (m_ProjectionType == CameraProjectionType::Orthographic) {
            // 正交投影 — 三视图使用，无透视效果
            float32 zoomLevel = m_Distance * 0.5f;
            float32 left   = -zoomLevel * aspect;
            float32 right  =  zoomLevel * aspect;
            float32 bottom = -zoomLevel;
            float32 top    =  zoomLevel;
            proj = glm::ortho(left, right, bottom, top, 0.1f, 1000.0f);
        } else {
            // 透视投影 — 默认
            proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
        }
        StoreGlm(proj, m_ProjectionMatrix);

        // ── 视矩阵 ──
        glm::mat4 view(1.0f);
        glm::vec3 eye(0.0f);

        if (m_RightMouseDown && !m_AltHeld) {
            // 飞行模式：位置来自 m_Position + pan，朝向来自 yaw/pitch
            eye = ToGlm(m_Position) + ToGlm(m_PanOffset);

            float32 yawRad   = glm::radians(m_Yaw);
            float32 pitchRad = glm::radians(m_Pitch);
            glm::vec3 forward(
                std::cos(pitchRad) * std::cos(yawRad),
                std::sin(pitchRad),
                std::cos(pitchRad) * std::sin(yawRad)
            );
            view = glm::lookAt(eye, eye + forward, glm::vec3(0.0f, 1.0f, 0.0f));
        } else {
            // 轨道/空闲模式：围绕焦点旋转
            float32 yawRad   = glm::radians(m_OrbitYaw);
            float32 pitchRad = glm::radians(m_OrbitPitch);

            glm::vec3 offset(
                m_Distance * std::cos(pitchRad) * std::cos(yawRad),
                m_Distance * std::sin(pitchRad),
                m_Distance * std::cos(pitchRad) * std::sin(yawRad)
            );

            glm::vec3 focus = ToGlm(m_FocusPoint) + ToGlm(m_PanOffset);
            eye = focus + offset;
            view = glm::lookAt(eye, focus, glm::vec3(0.0f, 1.0f, 0.0f));

            // 同步 position（以便外部查询）
            m_Position = ToEngine(eye);
        }

        StoreGlm(view, m_ViewMatrix);

        // ── VP = 投影 * 视图 ──
        glm::mat4 vp = proj * view;
        StoreGlm(vp, m_ViewProjectionMatrix);
    }

    // ============================================================
    // Pan 输入
    // ============================================================

    void EditorCamera::ProcessPanInput(float32 /*dt*/) {
        // 由父级 ProcessInput 处理
    }

    void EditorCamera::ProcessOrbitInput(float32 /*dt*/) {
        // 由父级 ProcessInput 处理
    }

    void EditorCamera::ProcessScrollInput(float32 /*dt*/) {
        // 由父级 ProcessInput 处理
    }

    // ============================================================
    // 平滑阻尼
    // ============================================================

    void EditorCamera::SmoothDamp(Vec3& current, const Vec3& target,
                                  Vec3& velocity, float32 smoothTime, float32 dt) {
        glm::vec3 cur  = ToGlm(current);
        glm::vec3 tgt  = ToGlm(target);
        glm::vec3 vel  = ToGlm(velocity);

        float32 omega = 2.0f / smoothTime;
        glm::vec3 diff = cur - tgt;
        float32 epsilon = 0.001f;

        glm::vec3 temp = vel + omega * diff;
        glm::vec3 damped = cur - diff * (1.0f - std::exp(-omega * dt * 0.5f));

        if (glm::length(diff) < epsilon && glm::length(vel) < epsilon) {
            current = target;
            velocity = { 0.0f, 0.0f, 0.0f };
            return;
        }

        velocity = ToEngine(temp * std::exp(-omega * dt * 0.5f));
        current = ToEngine(damped);
    }

} // namespace Engine