#pragma once

/**
 * @file Constraint.h
 * @brief 动画约束系统 — 约束基类与所有约束类型
 *
 * ════════════════════════════════════════════
 * 约束类型
 * ════════════════════════════════════════════
 *
 *   PointConstraint   — 位置约束：将骨骼位置约束到目标位置
 *   OrientConstraint  — 旋转约束：将骨骼旋转约束到目标旋转
 *   ScaleConstraint   — 缩放约束：将骨骼缩放约束到目标缩放
 *   ParentConstraint  — 父级约束：全变换约束（T+R+S），带逐轴偏移保持
 *   LookAtConstraint  — 注视约束：使骨骼的 Z 轴（或指定轴）指向目标
 *   AimConstraint     — 瞄准约束：使骨骼的指定轴对齐到目标方向
 *
 * ════════════════════════════════════════════
 * 使用示例
 * ════════════════════════════════════════════
 *
 *   // 创建点约束：角色的手跟随目标
 *   auto point = std::make_unique<PointConstraint>("hand_follow");
 *   point->SetAffectedBone(handBoneIndex);
 *   point->AddTarget(ConstraintTarget::FromLocator("target_loc"));
 *
 *   // 创建父级约束：物体附着在角色手上
 *   auto parent = std::make_unique<ParentConstraint>("attach");
 *   parent->SetAffectedBone(objectBoneIndex);
 *   parent->AddTarget(ConstraintTarget::FromLocator("character_hand"));
 *   parent->SetMaintainOffset(true);
 *
 *   // 创建注视约束：头部看向目标
 *   auto look = std::make_unique<LookAtConstraint>("head_look");
 *   look->SetAffectedBone(headBoneIndex);
 *   look->AddTarget(ConstraintTarget::FromLocator("look_target"));
 *   look->SetUpAxis(Vec3(0, 1, 0));
 *
 * ════════════════════════════════════════════
 * 与动画管线集成
 * ════════════════════════════════════════════
 *
 * 约束系统在动画管线的 PostProcess 阶段执行。
 * 此时骨骼已有动画姿势的局部变换，约束在此基础上
 * 对指定骨骼进行修正，实现 IK 风格的效果。
 */

#include "Engine/Animation/ConstraintTarget.h"
#include "Engine/Animation/Locator.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Animation/AnimationPipeline.h" // PoseLocalData
#include <memory>
#include <vector>
#include <functional>

namespace Engine {

    // ============================================================
    // 约束类型枚举
    // ============================================================
    enum class ConstraintType : uint8 {
        Point,      ///< 位置约束：将骨骼位置约束到目标位置
        Orient,     ///< 旋转约束：将骨骼旋转约束到目标旋转
        Scale,      ///< 缩放约束：将骨骼缩放约束到目标缩放
        Parent,     ///< 父级约束：全变换约束，保持偏移
        LookAt,     ///< 注视约束：使骨骼指向目标位置
        Aim,        ///< 瞄准约束：对齐指定轴到目标方向
    };

    // ============================================================
    // 约束基类
    // ============================================================
    class IConstraint {
    public:
        IConstraint() = default;
        explicit IConstraint(std::string name) : m_Name(std::move(name)) {}
        virtual ~IConstraint() = default;

        // ── 类型 ──
        virtual ConstraintType GetType() const = 0;

        // ── 名称 ──
        void SetName(const std::string& n) { m_Name = n; }
        const std::string& GetName() const noexcept { return m_Name; }

        // ── 启用/禁用 ──
        void SetEnabled(bool enabled) noexcept { m_Enabled = enabled; }
        bool IsEnabled() const noexcept { return m_Enabled; }

        // ── 受影响的骨骼 ──
        void SetAffectedBone(int32 boneIndex) noexcept { m_AffectedBone = boneIndex; }
        int32 GetAffectedBone() const noexcept { return m_AffectedBone; }

        // ── 目标管理 ──
        void AddTarget(const ConstraintTarget& target) { m_Targets.push_back(target); }
        void ClearTargets() { m_Targets.clear(); }
        size_t GetTargetCount() const noexcept { return m_Targets.size(); }
        ConstraintTarget& GetTarget(size_t i) { return m_Targets[i]; }
        const ConstraintTarget& GetTarget(size_t i) const { return m_Targets[i]; }

        // ── 偏移保持模式 ──
        /**
         * @brief 设置是否保持初始偏移
         *
         * 当启用时，约束会在第一次求值时记录骨骼与目标之间的偏移，
         * 后续帧都将保持该偏移。对于 ParentConstraint 等非常有用，
         * 可使物体"附着"在目标上同时保持相对位置。
         */
        void SetMaintainOffset(bool maintain) noexcept { m_MaintainOffset = maintain; }
        bool GetMaintainOffset() const noexcept { return m_MaintainOffset; }

        /**
         * @brief 设置初始偏移值（手动模式）
         * @param offsetPos  位置偏移
         * @param offsetRot  旋转偏移
         * @param offsetScale 缩放偏移
         */
        void SetInitialOffset(const Vec3& pos, const Quat& rot, const Vec3& scale) {
            m_HasOffset = true;
            m_OffsetPosition = pos;
            m_OffsetRotation = rot;
            m_OffsetScale = scale;
        }

        // ── 约束强度 ──
        void SetStrength(float32 strength) noexcept { m_Strength = strength; }
        float32 GetStrength() const noexcept { return m_Strength; }

        // ── 主要求值接口 ──

        /**
         * @brief 求值约束
         * @param skeleton  目标骨骼（读取/写入局部姿势）
         * @param locators  可用的定位器列表（用于 Locator 类型目标）
         * @param pose      局部姿势数据（可选的输入/输出）
         *
         * 约束将修改受影响骨骼的 localPoseMatrix 或直接修改 pose。
         */
        virtual void Evaluate(Skeleton& skeleton,
                              const std::vector<Locator>& locators,
                              PoseLocalData& pose) = 0;

        // ── 解析骨骼名称目标 ──
        /**
         * @brief 解析骨骼名称引用为索引
         * @param skeleton 骨骼对象
         *
         * 对使用 FromBoneName 创建的目标，将 locatorName 作为骨骼名称
         * 解析为实际的 boneIndex。
         */
        void ResolveBoneNames(const Skeleton& skeleton);

    protected:
        // ── 工具方法 ──

        /**
         * @brief 计算目标的实际世界空间变换
         * @param target   约束目标描述
         * @param skeleton 骨骼（用于 Bone 类型目标）
         * @param locators 定位器列表（用于 Locator 类型目标）
         * @param outPos   输出位置
         * @param outRot   输出旋转
         * @param outScale 输出缩放
         */
        static void ResolveTargetTransform(
            const ConstraintTarget& target,
            const Skeleton& skeleton,
            const std::vector<Locator>& locators,
            Vec3& outPos, Quat& outRot, Vec3& outScale);

        /**
         * @brief 多目标加权平均位置
         */
        static Vec3 ComputeWeightedPosition(
            const std::vector<ConstraintTarget>& targets,
            const Skeleton& skeleton,
            const std::vector<Locator>& locators);

        /**
         * @brief 多目标加权平均旋转（使用 SLERP 链）
         */
        static Quat ComputeWeightedRotation(
            const std::vector<ConstraintTarget>& targets,
            const Skeleton& skeleton,
            const std::vector<Locator>& locators);

        /**
         * @brief 将骨骼 localPoseMatrix 分解为 T/R/S
         */
        static void DecomposeBoneMatrix(const Bone& bone,
                                        Vec3& pos, Quat& rot, Vec3& scale);

        /**
         * @brief 将 T/R/S 组合为骨骼 localPoseMatrix
         */
        static void ComposeBoneMatrix(Bone& bone,
                                      const Vec3& pos, const Quat& rot, const Vec3& scale);

        /**
         * @brief 矩阵分解（从 4x4 变换矩阵提取 T/R/S）
         */
        static void DecomposeMatrix(const Mat4& m, Vec3& pos, Quat& rot, Vec3& scale);

        /**
         * @brief 组合变换矩阵（从 T/R/S 构建 4x4 矩阵）
         */
        static Mat4 ComposeMatrix(const Vec3& pos, const Quat& rot, const Vec3& scale);

        // ── 成员 ──
        std::string m_Name;
        bool m_Enabled = true;
        bool m_MaintainOffset = false;
        bool m_HasOffset = false;
        int32 m_AffectedBone = -1;
        float32 m_Strength = 1.0f;

        // 初始偏移值
        Vec3 m_OffsetPosition{0, 0, 0};
        Quat m_OffsetRotation{0, 0, 0, 1};
        Vec3 m_OffsetScale{1, 1, 1};

        // 目标列表
        std::vector<ConstraintTarget> m_Targets;
    };

    // ============================================================
    // PointConstraint — 位置约束
    // ============================================================
    /**
     * @brief 将骨骼位置约束到目标位置
     *
     * 骨骼的平移被覆盖为目标位置（加权平均），
     * 旋转和缩放保持不变。
     */
    class PointConstraint : public IConstraint {
    public:
        using IConstraint::IConstraint;
        ConstraintType GetType() const override { return ConstraintType::Point; }

        void Evaluate(Skeleton& skeleton,
                      const std::vector<Locator>& locators,
                      PoseLocalData& pose) override;
    };

    // ============================================================
    // OrientConstraint — 旋转约束
    // ============================================================
    /**
     * @brief 将骨骼旋转约束到目标旋转
     *
     * 骨骼的旋转被覆盖为目标旋转（加权 SLERP），
     * 位置和缩放保持不变。
     */
    class OrientConstraint : public IConstraint {
    public:
        using IConstraint::IConstraint;
        ConstraintType GetType() const override { return ConstraintType::Orient; }

        void Evaluate(Skeleton& skeleton,
                      const std::vector<Locator>& locators,
                      PoseLocalData& pose) override;
    };

    // ============================================================
    // ScaleConstraint — 缩放约束
    // ============================================================
    /**
     * @brief 将骨骼缩放约束到目标缩放
     *
     * 骨骼的缩放被覆盖为目标缩放（加权平均），
     * 位置和旋转保持不变。
     */
    class ScaleConstraint : public IConstraint {
    public:
        using IConstraint::IConstraint;
        ConstraintType GetType() const override { return ConstraintType::Scale; }

        void Evaluate(Skeleton& skeleton,
                      const std::vector<Locator>& locators,
                      PoseLocalData& pose) override;
    };

    // ============================================================
    // ParentConstraint — 父级约束（全变换）
    // ============================================================
    /**
     * @brief 父级约束：全变换约束，保持偏移
     *
     * 骨骼的全变换（T+R+S）被约束到目标变换。
     * 当 MaintainOffset 启用时，保持骨骼与目标之间的初始相对变换。
     * 支持逐轴控制（constrainX/Y/Z）。
     *
     * 典型用途：物体附着到角色骨骼上。
     */
    class ParentConstraint : public IConstraint {
    public:
        using IConstraint::IConstraint;
        ConstraintType GetType() const override { return ConstraintType::Parent; }

        void Evaluate(Skeleton& skeleton,
                      const std::vector<Locator>& locators,
                      PoseLocalData& pose) override;

        /**
         * @brief 设置逐轴控制
         * @param x 是否约束 X 轴
         * @param y 是否约束 Y 轴
         * @param z 是否约束 Z 轴
         */
        void SetAxes(bool x, bool y, bool z) {
            for (auto& t : m_Targets) {
                t.constrainX = x;
                t.constrainY = y;
                t.constrainZ = z;
            }
        }
    };

    // ============================================================
    // LookAtConstraint — 注视约束
    // ============================================================
    /**
     * @brief 注视约束：使骨骼的指定轴指向目标位置
     *
     * 骨骼旋转使指定前方向（默认为 +Z）指向目标位置。
     * 可选的上方向向量用于控制扭转。
     *
     * 典型用途：头部/眼睛看向目标、炮塔瞄准。
     */
    class LookAtConstraint : public IConstraint {
    public:
        using IConstraint::IConstraint;
        ConstraintType GetType() const override { return ConstraintType::LookAt; }

        void Evaluate(Skeleton& skeleton,
                      const std::vector<Locator>& locators,
                      PoseLocalData& pose) override;

        /** 设置前方向轴（默认为 +Z: (0,0,1)） */
        void SetForwardAxis(const Vec3& axis) noexcept { m_ForwardAxis = axis; }
        const Vec3& GetForwardAxis() const noexcept { return m_ForwardAxis; }

        /** 设置上方向轴（默认为 +Y: (0,1,0)） */
        void SetUpAxis(const Vec3& axis) noexcept { m_UpAxis = axis; }
        const Vec3& GetUpAxis() const noexcept { return m_UpAxis; }

    private:
        Vec3 m_ForwardAxis{0, 0, 1};
        Vec3 m_UpAxis{0, 1, 0};
    };

    // ============================================================
    // AimConstraint — 瞄准约束
    // ============================================================
    /**
     * @brief 瞄准约束：使骨骼的指定轴对齐到目标方向
     *
     * 与 LookAt 不同的是，Aim 约束关注的是方向对齐而非指向一个点。
     * 它将骨骼的指定局部轴旋转到与目标方向（从骨骼指向目标）一致。
     *
     * 典型用途：角色手臂指向目标、灯光方向控制。
     */
    class AimConstraint : public IConstraint {
    public:
        using IConstraint::IConstraint;
        ConstraintType GetType() const override { return ConstraintType::Aim; }

        void Evaluate(Skeleton& skeleton,
                      const std::vector<Locator>& locators,
                      PoseLocalData& pose) override;

        /** 设置要对齐的局部轴（默认为 +Z: (0,0,1)） */
        void SetAimAxis(const Vec3& axis) noexcept { m_AimAxis = axis; }
        const Vec3& GetAimAxis() const noexcept { return m_AimAxis; }

        /** 设置上方向轴（默认为 +Y: (0,1,0)） */
        void SetUpAxis(const Vec3& axis) noexcept { m_UpAxis = axis; }
        const Vec3& GetUpAxis() const noexcept { return m_UpAxis; }

    private:
        Vec3 m_AimAxis{0, 0, 1};
        Vec3 m_UpAxis{0, 1, 0};
    };

} // namespace Engine
