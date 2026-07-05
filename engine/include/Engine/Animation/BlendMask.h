#pragma once

/**
 * @file BlendMask.h
 * @brief 混合遮罩 — 控制每个骨骼在动画混合中的参与度
 *
 * BlendMask 允许对骨骼动画做"分部混合"（Partial Blending），
 * 只对指定的骨骼或骨骼区域应用混合效果，其他骨骼保持原姿势。
 *
 * ════════════════════════════════════════════
 * 典型应用场景
 * ════════════════════════════════════════════
 *
 *   上半身瞄准 + 下半身行走：
 *     BlendMask mask;
 *     mask.SetAll(0.0f);                    // 默认全部使用行走
 *     mask.SetBoneWeightRecursive(skeleton, "Spine", 1.0f);   // 脊椎及以上用瞄准
 *     mask.SetBoneWeightRecursive(skeleton, "Head", 1.0f);    // 头部用瞄准
 *
 *   AnimationBlend::BlendSkeletonPoseMasked(
 *       output, walkPose, aimPose, mask, 1.0f);
 *
 * ════════════════════════════════════════════
 * 权重语义
 * ════════════════════════════════════════════
 *   weight = 0.0 → 完全使用姿势 A（基础姿势）
 *   weight = 1.0 → 完全使用姿势 B（混合姿势）
 *   weight = 0.5 → 50/50 混合
 *
 * 对于子骨骼，默认继承父骨骼的权重，但可以单独覆盖。
 */

#include "Engine/Types.h"
#include "Engine/Animation/Skeleton.h"
#include <vector>
#include <string>

namespace Engine {

    /**
     * @brief 混合遮罩 — 按骨骼控制混合权重
     *
     * 使用示例：
     * @code
     *   BlendMask mask;
     *   mask.Resize(skeleton.GetBoneCount());
     *   mask.SetAll(0.0f);
     *   mask.SetBoneWeightRecursive(skeleton, "Spine", 1.0f);
     *   mask.SetBoneWeightRecursive(skeleton, "Head", 1.0f);
     * @endcode
     */
    class BlendMask {
    public:
        BlendMask() = default;
        ~BlendMask() = default;

        // ── 大小管理 ──

        /** 调整遮罩大小以匹配骨骼数量 */
        void Resize(size_t boneCount) { m_Weights.assign(boneCount, 1.0f); }

        /** 获取当前遮罩大小 */
        size_t GetSize() const noexcept { return m_Weights.size(); }

        /** 检查遮罩是否为空 */
        bool IsEmpty() const noexcept { return m_Weights.empty(); }

        // ── 权重设置 ──

        /** 设置所有骨骼的权重为统一值 */
        void SetAll(float32 weight) {
            for (auto& w : m_Weights) w = weight;
        }

        /**
         * @brief 设置单根骨骼的权重
         * @param boneIndex 骨骼索引
         * @param weight    权重 [0, 1]
         */
        void SetBoneWeight(int32 boneIndex, float32 weight) {
            if (boneIndex >= 0 && boneIndex < static_cast<int32>(m_Weights.size()))
                m_Weights[boneIndex] = weight;
        }

        /**
         * @brief 设置骨骼及其所有子骨骼的权重（递归）
         * @param skeleton 骨骼层级（用于查找父子关系）
         * @param boneName 骨骼名称
         * @param weight   权重 [0, 1]
         */
        void SetBoneWeightRecursive(const Skeleton& skeleton,
                                     const std::string& boneName,
                                     float32 weight) {
            int32 idx = skeleton.FindBoneIndex(boneName);
            if (idx >= 0)
                SetBoneWeightRecursive(skeleton, idx, weight);
        }

        /**
         * @brief 按索引递归设置权重
         */
        void SetBoneWeightRecursive(const Skeleton& skeleton,
                                     int32 boneIndex,
                                     float32 weight) {
            if (boneIndex < 0 || boneIndex >= static_cast<int32>(m_Weights.size()))
                return;

            m_Weights[boneIndex] = weight;

            // 递归设置所有子骨骼
            size_t count = skeleton.GetBoneCount();
            for (size_t i = 0; i < count; ++i) {
                if (skeleton.GetBone(static_cast<int32>(i)).parentIndex == boneIndex) {
                    SetBoneWeightRecursive(skeleton, static_cast<int32>(i), weight);
                }
            }
        }

        // ── 权重获取 ──

        /** 获取指定骨骼的权重 */
        float32 GetBoneWeight(int32 boneIndex) const {
            if (boneIndex >= 0 && boneIndex < static_cast<int32>(m_Weights.size()))
                return m_Weights[boneIndex];
            return 0.0f;
        }

        /** 获取原始权重数组（只读） */
        const std::vector<float32>& GetWeights() const noexcept { return m_Weights; }

        /** 获取原始权重数组（可写） */
        std::vector<float32>& GetWeights() noexcept { return m_Weights; }

        // ── 工具 ──

        /** 反转所有权重: w' = 1 - w */
        void Invert() {
            for (auto& w : m_Weights) w = 1.0f - w;
        }

        /** 将所有权重钳制到 [0, 1] */
        void Clamp() {
            for (auto& w : m_Weights) {
                if (w < 0.0f) w = 0.0f;
                if (w > 1.0f) w = 1.0f;
            }
        }

        /** 创建全 1 的遮罩（全部混合） */
        static BlendMask Full(size_t boneCount) {
            BlendMask mask;
            mask.Resize(boneCount);
            mask.SetAll(1.0f);
            return mask;
        }

        /** 创建全 0 的遮罩（全部不混合） */
        static BlendMask None(size_t boneCount) {
            BlendMask mask;
            mask.Resize(boneCount);
            mask.SetAll(0.0f);
            return mask;
        }

    private:
        std::vector<float32> m_Weights;
    };

} // namespace Engine
