#pragma once

/**
 * @file AnimationRetarget.h
 * @brief 动画重定目标 — 在不同骨骼之间迁移动画姿势
 *
 * 重定目标（Retargeting）是将一个骨架的动画应用到另一个不同比例的骨架上。
 * 典型场景：将人类动画（如行走、跑步）从标准骨架迁移到不同体型的角色上。
 *
 * ════════════════════════════════════════════
 * 重定目标策略
 * ════════════════════════════════════════════
 *
 * 1. 骨骼名称映射 (Bone Mapping)
 *    通过名称将源骨骼映射到目标骨骼：
 *    "Hips" → "Hips", "LeftUpperArm" → "LeftArm", etc.
 *
 * 2. 平移缩放 (Translation Scaling)
 *    根据源/目标骨骼的长度比例缩放平移量：
 *    targetTrans = sourceTrans * (targetBoneLen / sourceBoneLen)
 *
 * 3. 旋转对齐 (Rotation Alignment)
 *    如果源和目标的绑定姿势不同，需要额外旋转补偿：
 *    finalRot = targetBindRot⁻¹ * sourceAnimRot * sourceBindRot
 *
 * 4. 根运动提取 (Root Motion)
 *    可选：将根骨骼的平移提取为独立的运动数据
 */

#include "Engine/Animation/Skeleton.h"
#include "Engine/Animation/AnimationBlend.h"
#include "Engine/Animation/AnimationPipeline.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine {

    /**
     * @brief 骨骼映射条目 — 描述源骨骼到目标骨骼的映射关系
     */
    struct BoneMappingEntry {
        std::string sourceBone;     ///< 源骨骼名称
        std::string targetBone;     ///< 目标骨骼名称
        float32     scaleFactor;    ///< 缩放因子（默认自动计算）

        BoneMappingEntry() = default;
        BoneMappingEntry(std::string src, std::string tgt, float32 scale = 1.0f)
            : sourceBone(std::move(src))
            , targetBone(std::move(tgt))
            , scaleFactor(scale) {}
    };

    /**
     * @brief 动画重定目标器
     *
     * 使用示例：
     * @code
     *   AnimationRetarget retarget;
     *
     *   // 设置源/目标骨骼
     *   retarget.SetSourceSkeleton(sourceSkel);
     *   retarget.SetTargetSkeleton(targetSkel);
     *
     *   // 自动建立映射（按名称匹配）
     *   retarget.AutoMapByName();
     *
     *   // 或手动映射
     *   retarget.AddMapping("Spine1", "Spine", 0.9f);
     *   retarget.AddMapping("Head",   "Head",  1.0f);
     *
     *   // 重定目标：将 sourcePose 转换为 targetPose
     *   retarget.RetargetPose(targetPose, sourcePose);
     *
     *   // 更新目标骨骼的世界矩阵和蒙皮矩阵
     *   targetSkel.UpdateWorldPoses();
     * @endcode
     */
    class AnimationRetarget {
    public:
        AnimationRetarget() = default;
        ~AnimationRetarget() = default;

        // ── 骨骼设置 ──

        void SetSourceSkeleton(const Skeleton* skeleton) { m_SourceSkel = skeleton; }
        void SetTargetSkeleton(const Skeleton* skeleton) { m_TargetSkel = skeleton; }
        void SetSourceSkeleton(const std::shared_ptr<Skeleton>& skeleton) { m_SourceSkel = skeleton.get(); }
        void SetTargetSkeleton(const std::shared_ptr<Skeleton>& skeleton) { m_TargetSkel = skeleton.get(); }

        const Skeleton* GetSourceSkeleton() const { return m_SourceSkel; }
        const Skeleton* GetTargetSkeleton() const { return m_TargetSkel; }

        // ── 映射管理 ──

        /**
         * @brief 添加骨骼映射
         * @param sourceBone 源骨骼名称
         * @param targetBone 目标骨骼名称
         * @param scaleFactor 手动缩放因子（0=自动计算）
         */
        void AddMapping(const std::string& sourceBone, const std::string& targetBone,
                        float32 scaleFactor = 0.0f);

        /**
         * @brief 按名称自动建立映射
         *
         * 对源骨骼中的每个名称，在目标骨骼中查找同名骨骼。
         * 若找到则自动建立映射并计算缩放因子。
         */
        void AutoMapByName();

        /** 清除所有映射 */
        void ClearMappings();

        /** 获取映射表 */
        const std::vector<BoneMappingEntry>& GetMappings() const { return m_Mappings; }

        // ── 核心重定目标 ──

        /**
         * @brief 重定目标姿势
         * @param outTarget 输出目标骨骼（localPoseMatrix 会被修改）
         * @param sourcePose 源姿势（PoseLocalData 格式）
         *
         * 对每个已映射的骨骼：
         *   1. 检查是否需要旋转补偿
         *   2. 缩放平移量
         *   3. 写入目标骨骼的 localPoseMatrix
         */
        void RetargetPose(Skeleton& outTarget, const PoseLocalData& sourcePose) const;

        /**
         * @brief 在两个 Skeleton 对象之间重定目标
         * @param outTarget 输出目标骨骼
         * @param sourceSkeleton 源骨骼（已更新动画姿势）
         */
        void RetargetSkeleton(Skeleton& outTarget, const Skeleton& sourceSkeleton) const;

        // ── 工具 ──

        /**
         * @brief 计算两骨骼的长度比
         * @param sourceName 源骨骼名称
         * @param targetName 目标骨骼名称
         * @return 长度比 = targetLen / sourceLen
         */
        float32 CalculateBoneScale(const std::string& sourceName,
                                    const std::string& targetName) const;

        /** 检查是否有有效映射 */
        bool HasMappings() const { return !m_Mappings.empty() && m_SourceSkel && m_TargetSkel; }

    private:
        const Skeleton* m_SourceSkel = nullptr;
        const Skeleton* m_TargetSkel = nullptr;

        std::vector<BoneMappingEntry> m_Mappings;

        // 缓存：源/目标骨骼索引映射（加速运行时）
        // m_SourceIndices[i] = source 侧的骨骼索引
        // m_TargetIndices[i] = target 侧的骨骼索引
        std::vector<int32> m_SourceIndices;
        std::vector<int32> m_TargetIndices;
    };

} // namespace Engine
