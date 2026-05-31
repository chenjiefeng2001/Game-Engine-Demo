#pragma once

#include "Engine/Core/RHI/MathTypes.h"

namespace Engine {

    /**
     * @brief 透视相机
     *
     * 支持 FOV、近远裁剪面、pitch/yaw 旋转和 LookAt。
     * RHI 原则：头文件只依赖 RHI/MathTypes.h（纯数据），不依赖 glm。
     */
    class PerspectiveCamera {
    public:
        PerspectiveCamera(float fovDegrees = 45.0f,
                          float aspectRatio = 16.0f / 9.0f,
                          float nearPlane = 0.1f,
                          float farPlane = 100.0f);

        // ── 位置与朝向 ──
        void SetPosition(const Vec3& pos)     { m_Position = pos; m_ViewDirty = true; }
        void SetRotation(float pitch, float yaw) { m_Pitch = pitch; m_Yaw = yaw; m_ViewDirty = true; }
        void LookAt(const Vec3& target, const Vec3& up = Vec3(0, 1, 0));

        const Vec3& GetPosition() const noexcept { return m_Position; }
        float GetPitch() const noexcept { return m_Pitch; }
        float GetYaw()   const noexcept { return m_Yaw; }

        // ── 投影参数 ──
        void SetFov(float fovDegrees);
        void SetAspectRatio(float aspect);
        void SetNearFar(float nearPlane, float farPlane);

        float GetFov()    const noexcept { return m_Fov; }
        float GetAspect() const noexcept { return m_AspectRatio; }
        float GetNear()   const noexcept { return m_Near; }
        float GetFar()    const noexcept { return m_Far; }

        // ── 矩阵 ──
        const Mat4& GetProjectionMatrix();
        const Mat4& GetViewMatrix();
        const float* GetViewProjectionMatrixPtr();

        // ════════════════════════════════════════════════
        // 坐标空间变换
        // ════════════════════════════════════════════════

        /**
         * @brief 将点从 世界空间 → 观察空间（相机空间）
         *
         * 观察空间的原点在相机位置，Z 轴指向相机前方。
         */
        Vec3 WorldToView(const Vec3& worldPoint) const;

        /**
         * @brief 将点从 观察空间 → 世界空间
         */
        Vec3 ViewToWorld(const Vec3& viewPoint) const;

        /**
         * @brief 将点从 世界空间 → 裁剪空间（NDC，-1 ~ 1）
         * @return Vec4(w/x, y, z, w)，其中 w 是透视除法前的齐次坐标
         */
        Vec4 WorldToClip(const Vec3& worldPoint) const;

        /**
         * @brief 将点从 裁剪空间 → 世界空间
         * @param clipPoint NDC 坐标 (x, y, z 均在 [-1, 1] 范围内)
         * @return 世界空间中的点
         */
        Vec3 ClipToWorld(const Vec3& clipPoint) const;

        // ── 朝向向量 ──
        Vec3 GetForward() const;
        Vec3 GetRight()   const;
        Vec3 GetUp()      const;

    private:
        void RecalculateProjection();
        void RecalculateView();

        Mat4 m_ProjectionMatrix;
        Mat4 m_ViewMatrix;
        Mat4 m_ViewProjectionMatrix;

        Vec3 m_Position = { 0.0f, 0.0f, 5.0f };
        float m_Pitch   = 0.0f;   // 上下旋转（绕 X 轴）
        float m_Yaw     = -90.0f; // 左右旋转（绕 Y 轴）

        float m_Fov         = 45.0f;
        float m_AspectRatio = 16.0f / 9.0f;
        float m_Near        = 0.1f;
        float m_Far         = 100.0f;

        bool m_ProjectionDirty = true;
        bool m_ViewDirty = true;
    };

} // namespace Engine
