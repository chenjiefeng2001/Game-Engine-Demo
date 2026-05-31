#pragma once

#include "Engine/Core/RHI/MathTypes.h"

namespace Engine {

    /**
     * @brief 变换组件 — 描述游戏对象的位置、旋转、缩放
     *
     * RHI 原则：头文件只依赖 RHI/MathTypes.h（纯数据），不依赖 glm。
     * 所有数学运算在 .cpp 中使用 glm 实现。
     *
     * 支持：
     *   - 局部 / 世界变换矩阵
     *   - 父级层级变换链
     *   - 朝向向量 (Forward / Right / Up)
     */
    class TransformComponent {
    public:
        TransformComponent();
        explicit TransformComponent(const Vec3& position);
        TransformComponent(const Vec3& position,
                           const Vec3& rotation,
                           const Vec3& scale);

        // ── 访问器 ──
        const Vec3& GetPosition() const noexcept { return m_Position; }
        const Vec3& GetRotation() const noexcept { return m_Rotation; }
        const Vec3& GetScale()     const noexcept { return m_Scale; }

        void SetPosition(const Vec3& pos)     { m_Position = pos; m_Dirty = true; }
        void SetRotation(const Vec3& rot)     { m_Rotation = rot; m_Dirty = true; }
        void SetScale(const Vec3& s)          { m_Scale = s;    m_Dirty = true; }

        // ── 便捷设置 ──
        void SetPosition(float32 x, float32 y, float32 z) { SetPosition(Vec3(x, y, z)); }
        void SetRotation(float32 pitch, float32 yaw, float32 roll) { SetRotation(Vec3(pitch, yaw, roll)); }
        void SetScale(float32 uniform)                      { SetScale(Vec3(uniform, uniform, uniform)); }

        // ── 相对变换 ──
        void Translate(const Vec3& delta);
        void Rotate(const Vec3& eulerDelta);
        void ScaleBy(const Vec3& factor);

        // ── 矩阵 ──
        /** 局部变换矩阵 (T * R * S) —  model → parent 空间 */
        const Mat4& GetLocalMatrix();
        /** 世界变换矩阵 —  model → world 空间。如果无父级则等于局部矩阵 */
        const Mat4& GetWorldMatrix();

        /** RHI 接口：直接返回 float* 指针 */
        const float32* GetLocalMatrixData()  { return GetLocalMatrix().Data(); }
        const float32* GetWorldMatrixData()  { return GetWorldMatrix().Data(); }

        // ════════════════════════════════════════════════
        // 坐标空间变换
        // ════════════════════════════════════════════════

        /** 将点从 局部空间 → 世界空间 */
        Vec3 LocalToWorld(const Vec3& localPoint);
        /** 将方向向量从 局部空间 → 世界空间（忽略平移） */
        Vec3 LocalToWorldDir(const Vec3& localDir);
        /** 将点从 世界空间 → 局部空间 */
        Vec3 WorldToLocal(const Vec3& worldPoint);
        /** 将方向向量从 世界空间 → 局部空间（忽略平移） */
        Vec3 WorldToLocalDir(const Vec3& worldDir);

        /** 局部坐标系的三个基向量（在世界空间中） */
        Vec3 GetWorldForward();
        Vec3 GetWorldRight();
        Vec3 GetWorldUp();

        // ── 朝向向量 (从世界矩阵提取的快捷方式，等价于 GetWorldForward/Right/Up) ──
        Vec3 GetForward() const noexcept;
        Vec3 GetRight()   const noexcept;
        Vec3 GetUp()      const noexcept;

        // ── 层级 ──
        void SetParent(TransformComponent* parent) { m_Parent = parent; m_Dirty = true; }
        TransformComponent* GetParent() const noexcept { return m_Parent; }
        bool HasParent() const noexcept { return m_Parent != nullptr; }

    private:
        friend class GameObject;

        void RecalculateMatrices();

        // ── 数据（纯 POD，无第三方库类型）──
        Vec3 m_Position = { 0.0f, 0.0f, 0.0f };
        Vec3 m_Rotation = { 0.0f, 0.0f, 0.0f };
        Vec3 m_Scale    = { 1.0f, 1.0f, 1.0f };

        Mat4 m_LocalMatrix;   //  model → parent（局部空间）
        Mat4 m_WorldMatrix;   //  model → world（世界空间）

        bool m_Dirty = true;

        TransformComponent* m_Parent = nullptr;
    };

} // namespace Engine
