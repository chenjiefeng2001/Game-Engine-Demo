#pragma once

/**
 * @file IK.h
 * @brief 逆运动学求解 — CCD (Cyclic Coordinate Descent) 迭代求解器
 *
 * ════════════════════════════════════════════
 * CCD IK 算法
 * ════════════════════════════════════════════
 *
 * CCD (Cyclic Coordinate Descent) 是一种迭代 IK 求解算法：
 *
 *   从链的末端开始，对每个关节：
 *     1. 计算从当前关节到末端执行器的向量 V1
 *     2. 计算从当前关节到目标位置的向量 V2
 *     3. 计算将 V1 旋转到 V2 的旋转轴和角度
 *     4. 将旋转施加到当前关节的 localPoseMatrix
 *
 *   从端到根遍历完整条链为一次迭代。
 *   重复迭代直到末端执行器足够接近目标。
 *
 * 优点：简单、健壮、易于实现关节约束
 * 缺点：对长链可能收敛慢，不能保证最优解
 *
 * ════════════════════════════════════════════
 * 使用示例
 * ════════════════════════════════════════════
 *
 *   // 定义 IK 链：从 Hips（根）到 RightFoot（末端）
 *   IKChain legChain;
 *   legChain.SetStartBoneIndex(skeleton.FindBoneIndex("Hips"));
 *   legChain.SetEndBoneIndex(skeleton.FindBoneIndex("RightFoot"));
 *
 *   // 设置目标位置（世界空间）
 *   Vec3 targetPos = {2.0f, 0.0f, 0.5f};
 *
 *   // 创建求解器并求解
 *   IKSolver solver;
 *   solver.SetMaxIterations(16);
 *   solver.SetTolerance(0.01f);
 *   solver.SolveCCD(skeleton, legChain, targetPos);
 *
 *   // 骨架的 localPoseMatrix 已被 IK 修改
 *   skeleton.UpdateWorldPoses();
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Animation/Skeleton.h"
#include <vector>
#include <functional>

namespace Engine {

    // ============================================================
    // 关节约束（可选）
    // ============================================================
    struct IKJointConstraint {
        bool  enabled      = false;        ///< 是否启用约束
        Vec3  minAngles    = {-180, -180, -180};  ///< 最小角度（度）
        Vec3  maxAngles    = { 180,  180,  180};  ///< 最大角度（度）

        IKJointConstraint() = default;

        /** 创建约束 */
        static IKJointConstraint Limit(float32 minPitch, float32 maxPitch,
                                        float32 minYaw, float32 maxYaw,
                                        float32 minRoll, float32 maxRoll) {
            IKJointConstraint c;
            c.enabled = true;
            c.minAngles = Vec3(minPitch, minYaw, minRoll);
            c.maxAngles = Vec3(maxPitch, maxYaw, maxRoll);
            return c;
        }

        /** 钳制角度到约束范围 */
        Vec3 Clamp(const Vec3& eulerDeg) const {
            return Vec3(
                std::max(minAngles.x, std::min(maxAngles.x, eulerDeg.x)),
                std::max(minAngles.y, std::min(maxAngles.y, eulerDeg.y)),
                std::max(minAngles.z, std::min(maxAngles.z, eulerDeg.z))
            );
        }
    };

    // ============================================================
    // IK 链 — 描述一组连续的骨骼
    // ============================================================
    class IKChain {
    public:
        IKChain() = default;
        ~IKChain() = default;

        // ── 链的构建 ──

        /**
         * @brief 设置起始骨骼（链的根部）
         * @param boneIndex 骨骼索引
         */
        void SetStartBoneIndex(int32 boneIndex) noexcept { m_StartBone = boneIndex; }
        int32 GetStartBoneIndex() const noexcept { return m_StartBone; }

        /**
         * @brief 设置末端执行器骨骼
         * @param boneIndex 骨骼索引
         */
        void SetEndBoneIndex(int32 boneIndex) noexcept { m_EndBone = boneIndex; }
        int32 GetEndBoneIndex() const noexcept { return m_EndBone; }

        /**
         * @brief 从 Skeleton 自动构建骨骼索引链
         * @param skeleton 骨骼层级
         *
         * 从末端骨骼开始向上遍历 parentIndex，直到到达起始骨骼。
         * 链包含从起始骨骼到末端骨骼（含）的所有骨骼。
         */
        void BuildFromSkeleton(const Skeleton& skeleton);

        /** 获取链中所有骨骼的索引（从起始到末端） */
        const std::vector<int32>& GetBoneIndices() const noexcept { return m_BoneIndices; }

        /** 获取链长度（骨骼数量） */
        size_t GetLength() const noexcept { return m_BoneIndices.size(); }

        /** 清空链 */
        void Clear() { m_BoneIndices.clear(); }

        // ── 约束 ──

        /** 设置链中某骨骼的约束 */
        void SetConstraint(int32 chainIndex, const IKJointConstraint& constraint);
        /** 获取链中某骨骼的约束 */
        const IKJointConstraint& GetConstraint(int32 chainIndex) const;

    private:
        int32                  m_StartBone = -1;
        int32                  m_EndBone   = -1;
        std::vector<int32>     m_BoneIndices;
        std::vector<IKJointConstraint> m_Constraints;  // 与 m_BoneIndices 一一对应
    };

    // ============================================================
    // IK 求解器
    // ============================================================
    class IKSolver {
    public:
        IKSolver() = default;
        ~IKSolver() = default;

        // 禁止拷贝
        IKSolver(const IKSolver&) = delete;
        IKSolver& operator=(const IKSolver&) = delete;

        // ── 配置 ──

        /** 设置最大迭代次数（默认 16） */
        void SetMaxIterations(int32 iters) noexcept { m_MaxIterations = iters; }
        int32 GetMaxIterations() const noexcept { return m_MaxIterations; }

        /** 设置收敛容差（单位：世界单位，默认 0.01） */
        void SetTolerance(float32 tol) noexcept { m_Tolerance = tol; }
        float32 GetTolerance() const noexcept { return m_Tolerance; }

        /**
         * @brief 设置每骨骼权重（0=该骨骼不动, 1=完全参与）
         * @param chainIndex 链中索引
         * @param weight     权重 [0, 1]
         */
        void SetBoneWeight(int32 chainIndex, float32 weight) {
            if (chainIndex >= static_cast<int32>(m_PerBoneWeights.size()))
                m_PerBoneWeights.resize(chainIndex + 1, 1.0f);
            m_PerBoneWeights[chainIndex] = weight;
        }

        // ── 核心求解 ──

        /**
         * @brief CCD IK 求解
         * @param skeleton 骨骼（localPoseMatrix 会被修改）
         * @param chain    IK 链定义
         * @param target   目标位置（世界空间）
         * @return 求解后末端执行器与目标的距离
         *
         * 求解后自动调用 skeleton.UpdateWorldPoses()。
         */
        float32 SolveCCD(Skeleton& skeleton,
                         const IKChain& chain,
                         const Vec3& target);

        /**
         * @brief 带末端朝向约束的 CCD IK 求解
         * @param skeleton     骨骼
         * @param chain        IK 链
         * @param target       目标位置（世界空间）
         * @param targetDir    目标朝向（世界空间方向向量，末端执行器应朝向此方向）
         * @param dirWeight    朝向权重 [0, 1]（0=仅位置，1=位置+朝向各50%）
         * @return 求解后末端执行器与目标的距离
         */
        float32 SolveCCDWithTargetDir(Skeleton& skeleton,
                                       const IKChain& chain,
                                       const Vec3& target,
                                       const Vec3& targetDir,
                                       float32 dirWeight = 0.5f);

        // ── 混合 ──

        /**
         * @brief 设置 IK 效果的全局混合权重
         * @param weight [0, 1]，0=不动，1=完全 IK
         *
         * 求解后，IK 会根据此权重在原始姿势和 IK 姿势之间混合。
         * 要求调用 SolveAndBlend 而非 SolveCCD。
         */
        void SetBlendWeight(float32 weight) noexcept { m_BlendWeight = weight; }
        float32 GetBlendWeight() const noexcept { return m_BlendWeight; }

        /**
         * @brief 求解 IK 并与原始姿势混合
         * @param skeleton           骨骼（会被修改）
         * @param chain              IK 链
         * @param target             目标位置
         * @param originalPoseSnapshot 求解前的骨骼局部姿势快照
         * @return 末端距离
         *
         * 求解后对链中每根骨骼：
         *   final = LERP(original, ikSolved, blendWeight)
         */
        float32 SolveAndBlend(Skeleton& skeleton,
                               const IKChain& chain,
                               const Vec3& target,
                               const std::vector<Mat4>& originalPoseSnapshot);

    private:
        /**
         * @brief 获取骨骼的世界空间位置（从 currentPoseMatrix 提取平移）
         */
        static Vec3 GetBoneWorldPosition(const Skeleton& skeleton, int32 boneIndex);

        /**
         * @brief 在局部空间对骨骼施加旋转
         * @param skeleton 骨骼对象
         * @param boneIndex 骨骼索引
         * @param axis 旋转轴（世界空间）
         * @param angle 旋转角度（弧度）
         * @param weight 权重 [0, 1]
         */
        static void RotateBoneWorld(Skeleton& skeleton, int32 boneIndex,
                                     const Vec3& axis, float32 angle,
                                     float32 weight = 1.0f);

        int32   m_MaxIterations = 16;
        float32 m_Tolerance     = 0.01f;
        float32 m_BlendWeight   = 1.0f;
        std::vector<float32> m_PerBoneWeights;
    };

} // namespace Engine
