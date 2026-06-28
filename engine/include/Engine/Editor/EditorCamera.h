#pragma once

#include "Engine/Core/RHI/MathTypes.h"

namespace Engine {

    enum class CameraProjectionType : uint8 {
        Perspective,
        Orthographic
    };

    // ── 吸附模式 ──
    enum class SnapMode : uint8 {
        None,
        Grid,
        Vertex,
        Surface
    };

    // ── 选择模式 ──
    enum class SelectionMode : uint8 {
        Object,     // 选择 GameObject
        Vertex,     // 选择网格顶点
        Edge,       // 选择网格边缘
        Face        // 选择网格面片
    };

    class EditorCamera {
    public:
        EditorCamera();
        ~EditorCamera() = default;

        EditorCamera(const EditorCamera&) = delete;
        EditorCamera& operator=(const EditorCamera&) = delete;

        void OnUpdate(float32 dt);
        void ProcessInput(float32 dt);

        void SetActive(bool active) { m_Active = active; }
        bool IsActive() const { return m_Active; }

        void SetViewportSize(float32 width, float32 height);
        float32 GetViewportWidth() const { return m_ViewportWidth; }
        float32 GetViewportHeight() const { return m_ViewportHeight; }

        const float32* GetViewMatrixPtr() const { return m_ViewMatrix.Data(); }
        const float32* GetProjectionMatrixPtr() const { return m_ProjectionMatrix.Data(); }
        const float32* GetViewProjectionMatrixPtr() const { return m_ViewProjectionMatrix.Data(); }

        void SetFocusPoint(const Vec3& point) { m_FocusPoint = point; }
        const Vec3& GetFocusPoint() const { return m_FocusPoint; }
        void SetPosition(const Vec3& pos) { m_Position = pos; }
        const Vec3& GetPosition() const { return m_Position; }
        float32 GetDistance() const { return m_Distance; }
        void SetDistance(float32 distance) { m_Distance = distance; m_Dirty = true; }

        void SetProjectionType(CameraProjectionType type) { m_ProjectionType = type; m_Dirty = true; }
        CameraProjectionType GetProjectionType() const { return m_ProjectionType; }

        void SetPitch(float32 pitch) { m_Pitch = pitch; m_Dirty = true; }
        void SetYaw(float32 yaw) { m_Yaw = yaw; m_Dirty = true; }
        float32 GetPitch() const { return m_Pitch; }
        float32 GetYaw() const { return m_Yaw; }

        void LockRotation(bool lock) { m_RotationLocked = lock; }
        bool IsRotationLocked() const { return m_RotationLocked; }

        float32 GetFlySpeed() const { return m_FlySpeed; }
        void SetFlySpeed(float32 speed) { m_FlySpeed = speed; }

        // ── 吸附系统 ──
        void SetSnapMode(SnapMode mode) { m_SnapMode = mode; }
        SnapMode GetSnapMode() const { return m_SnapMode; }
        void SetGridSnapSize(float32 size) { m_GridSnapSize = (std::max)(0.01f, size); }
        float32 GetGridSnapSize() const { return m_GridSnapSize; }

        /** 将位置吸附到最近的网格点 */
        Vec3 SnapToGrid(const Vec3& position) const;
        /** 将角度吸附到最近的整数度数 */
        float32 SnapAngle(float32 degrees) const;

        // ── 空间查询 ──
        /** 获取相机方向向量 */
        Vec3 GetForwardVector() const;
        Vec3 GetRightVector() const;
        Vec3 GetUpVector() const;

        /** 从视口屏幕坐标发射射线（NDC 坐标 [-1,1]） */
        void ScreenToRay(float ndcX, float ndcY, Vec3& outOrigin, Vec3& outDirection) const;

        void Reset();

    private:
        void RecalculateMatrices();
        void ProcessFlyInput(float32 dt);
        void ProcessOrbitInput(float32 dt);
        void ProcessPanInput(float32 dt);
        void ProcessScrollInput(float32 dt);
        void SmoothDamp(Vec3& current, const Vec3& target, Vec3& velocity,
                        float32 smoothTime, float32 dt);

        bool m_Active = false;
        CameraProjectionType m_ProjectionType = CameraProjectionType::Perspective;
        bool m_RotationLocked = false;

        float32 m_ViewportWidth  = 1280.0f;
        float32 m_ViewportHeight = 720.0f;

        Vec3   m_Position    = { 0.0f, 0.0f, 10.0f };
        float32 m_Yaw        = 0.0f;
        float32 m_Pitch      = 0.0f;
        float32 m_FlySpeed   = 5.0f;

        Vec3   m_FocusPoint  = { 0.0f, 0.0f, 0.0f };
        float32 m_Distance   = 10.0f;
        float32 m_OrbitSpeed = 0.5f;
        float32 m_OrbitYaw   = 0.0f;
        float32 m_OrbitPitch = -20.0f;

        bool m_RightMouseDown = false;
        bool m_MiddleMouseDown = false;
        bool m_AltHeld = false;
        float32 m_LastMouseX = 0.0f;
        float32 m_LastMouseY = 0.0f;

        float32 m_OrbitVelocityYaw   = 0.0f;
        float32 m_OrbitVelocityPitch = 0.0f;
        float32 m_PanVelocityX       = 0.0f;
        float32 m_PanVelocityY       = 0.0f;

        Vec3   m_PanOffset    = { 0.0f, 0.0f, 0.0f };

        Mat4 m_ViewMatrix;
        Mat4 m_ProjectionMatrix;
        Mat4 m_ViewProjectionMatrix;

        Vec3 m_SmoothPosition;
        Vec3 m_SmoothVelocity;
        float32 m_SmoothYaw         = 0.0f;
        float32 m_SmoothPitch       = 0.0f;
        float32 m_SmoothYawVelocity = 0.0f;
        float32 m_SmoothPitchVelocity = 0.0f;
        float32 m_SmoothZoom        = 0.0f;
        float32 m_SmoothZoomVelocity = 0.0f;
        bool    m_Dirty             = true;

        // ── 吸附 ──
        SnapMode m_SnapMode = SnapMode::None;
        float32  m_GridSnapSize = 0.5f;
    };

} // namespace Engine