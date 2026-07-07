#pragma once

#include "Engine/Core/RHI/MathTypes.h"

namespace Engine {

    /**
     * @brief 变换组件 — 描述游戏对象的位置、旋转、缩放
     *
     * RHI 原则：头文件只依赖 RHI/MathTypes.h（纯数据），不依赖 glm。
     * 所有数学运算在 .cpp 中使用 glm 实现。
     *
     * 关键设计变更（v4.0）：
     *   - 内部使用四元数 (m_RotationQuat) 存储旋转，彻底避免万向锁
     *   - 欧拉角访问 (GetRotationEuler/SetRotationEuler) 通过四元数转换
     *   - 物理同步通过 GetRotationQuat/SetRotationQuat 直通，无额外开销
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

        // ── 位置访问器 ──
        const Vec3& GetPosition() const noexcept { return m_Position; }
        void SetPosition(const Vec3& pos)     { m_Position = pos; m_Dirty = true; }
        void SetPosition(float32 x, float32 y, float32 z) { SetPosition(Vec3(x, y, z)); }

        // ── 缩放访问器 ──
        const Vec3& GetScale() const noexcept { return m_Scale; }
        void SetScale(const Vec3& s)          { m_Scale = s;    m_Dirty = true; }
        void SetScale(float32 uniform)        { SetScale(Vec3(uniform, uniform, uniform)); }

        // ── 旋转访问器（内部使用四元数，避免万向锁）──

        /// 获取四元数旋转（物理同步/矩阵计算使用，无万向锁）
        const Quat& GetRotationQuat() const noexcept { return m_RotationQuat; }

        /// 获取欧拉角旋转（Editor/脚本使用，可能有万向锁）
        Vec3 GetRotationEuler() const;

        /// 设置四元数旋转（物理同步使用）
        void SetRotationQuat(const Quat& q) { m_RotationQuat = q; m_EulerDirty = true; m_Dirty = true; }

        /// 设置欧拉角旋转（Editor/脚本使用）
        void SetRotationEuler(const Vec3& euler);

        // ── 向后兼容（委托到四元数接口）──
        Vec3 GetRotation() const { return GetRotationEuler(); }
        void SetRotation(const Vec3& rot) { SetRotationEuler(rot); }
        void SetRotation(float32 pitch, float32 yaw, float32 roll) { SetRotationEuler(Vec3(pitch, yaw, roll)); }

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
        Vec3 m_Position       = { 0.0f, 0.0f, 0.0f };
        Quat m_RotationQuat   = Quat::Identity();  ///< 内部使用四元数，避免万向锁
        Vec3 m_Scale          = { 1.0f, 1.0f, 1.0f };

        /// 欧拉角缓存（Editor/脚本读取用，惰性计算）
        mutable Vec3  m_CachedEuler = { 0.0f, 0.0f, 0.0f };
        mutable bool  m_EulerDirty  = false;

        Mat4 m_LocalMatrix;   //  model → parent（局部空间）
        Mat4 m_WorldMatrix;   //  model → world（世界空间）

        bool m_Dirty = true;

        TransformComponent* m_Parent = nullptr;
    };

} // namespace Engine