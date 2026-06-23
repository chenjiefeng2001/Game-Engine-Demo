#pragma once

/**
 * @file EditorCamera.h
 * @brief 编辑器相机 — 支持鼠标右键+WASD 飞行和 Alt+左键 轨道旋转
 *
 * 使用方式：
 * @code
 *   EditorCamera cam;
 *   cam.SetViewportSize(viewportWidth, viewportHeight);
 *
 *   // 每帧
 *   cam.SetActive(viewportHovered || viewportFocused);
 *   cam.ProcessInput(dt);  // 内部检查 Input 系统
 *   cam.OnUpdate(dt);
 *
 *   // 获取矩阵
 *   const float* vp = cam.GetViewProjectionMatrixPtr();
 * @endcode
 *
 * 行为：
 *   - 鼠标右键 + WASD：第一人称飞行（Y 轴向上约束）
 *   - Alt + 左键拖拽：围绕焦点轨道旋转
 *   - 鼠标滚轮：缩放（调整到焦点的距离）
 *   - 中键拖拽：平移（Pan）
 */

#include "Engine/Core/RHI/MathTypes.h"

namespace Engine {

    class EditorCamera {
    public:
        EditorCamera();
        ~EditorCamera() = default;

        EditorCamera(const EditorCamera&) = delete;
        EditorCamera& operator=(const EditorCamera&) = delete;

        // ── 生命周期 ──
        void OnUpdate(float32 dt);
        void ProcessInput(float32 dt);

        // ── 激活控制（仅在视口 hover/focus 时处理输入） ──
        void SetActive(bool active) { m_Active = active; }
        bool IsActive() const { return m_Active; }

        // ── 视口配置 ──
        void SetViewportSize(float32 width, float32 height);
        float32 GetViewportWidth() const { return m_ViewportWidth; }
        float32 GetViewportHeight() const { return m_ViewportHeight; }

        // ── 相机矩阵 ──
        const float32* GetViewMatrixPtr() const { return m_ViewMatrix.Data(); }
        const float32* GetProjectionMatrixPtr() const { return m_ProjectionMatrix.Data(); }
        const float32* GetViewProjectionMatrixPtr() const { return m_ViewProjectionMatrix.Data(); }

        // ── 焦点/位置 ──
        void SetFocusPoint(const Vec3& point) { m_FocusPoint = point; }
        const Vec3& GetFocusPoint() const { return m_FocusPoint; }
        void SetPosition(const Vec3& pos) { m_Position = pos; }
        const Vec3& GetPosition() const { return m_Position; }

        // ── 重置 ──
        void Reset();

    private:
        /** 重新计算所有矩阵 */
        void RecalculateMatrices();

        // ── 飞行控制 ──
        void ProcessFlyInput(float32 dt);
        void ProcessOrbitInput(float32 dt);
        void ProcessPanInput(float32 dt);
        void ProcessScrollInput(float32 dt);

        // ── 内插平滑 ──
        void SmoothDamp(Vec3& current, const Vec3& target, Vec3& velocity,
                        float32 smoothTime, float32 dt);

        // ── 状态 ──
        bool m_Active = false;

        // ── 视口尺寸（用于构造投影矩阵） ──
        float32 m_ViewportWidth  = 1280.0f;
        float32 m_ViewportHeight = 720.0f;

        // ── 第一人称飞行 ──
        Vec3   m_Position    = { 0.0f, 0.0f, 10.0f };
        float32 m_Yaw        = 0.0f;       // 水平旋转（绕 Y 轴）
        float32 m_Pitch      = 0.0f;       // 垂直旋转（绕 X 轴）
        float32 m_FlySpeed   = 5.0f;

        // ── 轨道控制 ──
        Vec3   m_FocusPoint  = { 0.0f, 0.0f, 0.0f };
        float32 m_Distance   = 10.0f;      // 相机到焦点的距离
        float32 m_OrbitSpeed = 0.5f;
        float32 m_OrbitYaw   = 0.0f;       // 轨道水平角
        float32 m_OrbitPitch = -20.0f;     // 轨道俯仰角（度）

        // ── 鼠标状态（用于判断 mode） ──
        bool m_RightMouseDown = false;
        bool m_MiddleMouseDown = false;
        bool m_AltHeld = false;
        float32 m_LastMouseX = 0.0f;
        float32 m_LastMouseY = 0.0f;

        // ── 轨道拖动惯性 ──
        float32 m_OrbitVelocityYaw   = 0.0f;
        float32 m_OrbitVelocityPitch = 0.0f;
        float32 m_PanVelocityX       = 0.0f;
        float32 m_PanVelocityY       = 0.0f;

        // ── Pan 状态 ──
        Vec3   m_PanOffset    = { 0.0f, 0.0f, 0.0f };

        // ── 矩阵 ──
        Mat4 m_ViewMatrix;
        Mat4 m_ProjectionMatrix;
        Mat4 m_ViewProjectionMatrix;

        // ── 平滑阻尼 ──
        Vec3 m_SmoothPosition;
        Vec3 m_SmoothVelocity;
        float32 m_SmoothYaw         = 0.0f;
        float32 m_SmoothPitch       = 0.0f;
        float32 m_SmoothYawVelocity = 0.0f;
        float32 m_SmoothPitchVelocity = 0.0f;
        float32 m_SmoothZoom        = 0.0f;
        float32 m_SmoothZoomVelocity = 0.0f;
        bool    m_Dirty             = true;   // 矩阵需要重新计算
    };

} // namespace Engine