#pragma once

/**
 * @file BlendSpace1D.h
 * @brief 1D 混合空间 — 沿一维参数线在多个动画片段之间线性插值
 *
 * 1D BlendSpace（一维混合空间）是游戏动画中最常用的混合技术之一。
 * 它将多个动画片段（AnimationLocalTimeline）沿一条一维参数轴排列，
 * 根据输入的参数值在相邻的两个片段之间进行线性混合。
 *
 * ════════════════════════════════════════════
 * 典型应用场景
 * ════════════════════════════════════════════
 *
 *   speed=0        speed=3        speed=6        speed=10
 *    ┌──────┐      ┌──────┐      ┌──────┐      ┌──────┐
 *    │ Idle │──────│ Walk │──────│ Run  │──────│Sprint│
 *    └──────┘      └──────┘      └──────┘      └──────┘
 *       t=0           t=0.5          t=0.8         t=1.0
 *
 *   speed=4.5 → 在 Walk(t=0.5) 和 Run(t=0.8) 之间混合
 *
 * ════════════════════════════════════════════
 * 使用示例
 * ════════════════════════════════════════════
 *
 *   BlendSpace1D blendSpace;
 *
 *   // 添加采样点（参数值, 动画片段）
 *   blendSpace.AddSample(0.0f, idleClip);
 *   blendSpace.AddSample(3.0f, walkClip);
 *   blendSpace.AddSample(6.0f, runClip);
 *   blendSpace.AddSample(10.0f, sprintClip);
 *
 *   // 每帧根据当前速度采样
 *   float currentSpeed = ...;
 *   blendSpace.Sample(skeleton, poseEvaluator, currentSpeed, deltaTime);
 *
 *   // 骨骼的蒙皮矩阵已更新，可以直接上传 GPU（通过渲染器接口）
 *
 * ════════════════════════════════════════════
 * 混合策略
 * ════════════════════════════════════════════
 *   对每个骨骼的每个通道：
 *     Translation → 1D LERP
 *     Rotation    → SLERP
 *     Scale       → 1D LERP
 *   等同于 BlendSkeletonPose 的扩展：在 N 个样本中找相邻两个做混合。
 */

#include "Engine/Animation/AnimationLocalTimeline.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Animation/AnimationPose.h"
#include "Engine/Animation/AnimationBlend.h"
#include <vector>
#include <memory>
#include <algorithm>

namespace Engine {

    // ============================================================
    // 1D 混合采样点
    // ============================================================
    struct BlendSpace1DSample {
        float32 parameterValue = 0.0f;                     ///< 参数值（如速度、方向等）
        std::shared_ptr<AnimationLocalTimeline> clip;       ///< 对应动画片段

        BlendSpace1DSample() = default;
        BlendSpace1DSample(float32 pv, std::shared_ptr<AnimationLocalTimeline> c)
            : parameterValue(pv), clip(std::move(c)) {}
    };

    // ============================================================
    // 1D 混合空间
    // ============================================================
    class BlendSpace1D {
    public:
        BlendSpace1D() = default;
        ~BlendSpace1D() = default;

        // 禁止拷贝
        BlendSpace1D(const BlendSpace1D&) = delete;
        BlendSpace1D& operator=(const BlendSpace1D&) = delete;

        // ── 采样点管理 ──

        /**
         * @brief 添加一个采样点
         * @param paramValue 参数值（采样点在一维轴上的位置）
         * @param clip       对应的动画片段
         *
         * 采样点会自动按 paramValue 排序。
         * 如果已存在相同参数值的采样点，会被替换。
         */
        void AddSample(float32 paramValue, std::shared_ptr<AnimationLocalTimeline> clip);

        /**
         * @brief 移除指定索引的采样点
         */
        void RemoveSample(int32 index);

        /** 清空所有采样点 */
        void Clear();

        /** 获取采样点数量 */
        size_t GetSampleCount() const noexcept { return m_Samples.size(); }

        /** 获取指定索引的采样点（只读） */
        const BlendSpace1DSample& GetSample(int32 index) const;

        /** 获取指定索引的采样点（可写） */
        BlendSpace1DSample& GetSample(int32 index);

        // ── 核心采样 ──

        /**
         * @brief 在给定参数值处采样 1D 混合空间
         * @param skeleton   输出骨骼（局部姿势矩阵和蒙皮矩阵会被更新）
         * @param poseEval   姿势求值器（用于将动画时间线求值到骨骼）
         * @param paramValue 当前参数值（如当前速度）
         * @param time       动画采样时间点（所有时间线在同一时间点求值）
         *
         * 行为：
         *   1. 找到 paramValue 相邻的两个采样点 (lo, hi)
         *   2. 计算混合权重 t = (paramValue - lo.param) / (hi.param - lo.param)
         *   3. 在两个动画片段之间做逐骨骼 LERP/SLERP 混合
         *   4. 更新 skeleton 的世界矩阵和蒙皮矩阵
         *
         * 边界处理：
         *   - paramValue ≤ 最小参数值 → 使用第一个采样点（纯态）
         *   - paramValue ≥ 最大参数值 → 使用最后一个采样点（纯态）
         *   - 只有一个采样点 → 直接使用它
         *   - 无采样点 → 不操作
         */
        void Sample(Skeleton& skeleton,
                    AnimationPose& poseEval,
                    float32 paramValue,
                    float32 time);

        // ── 参数范围 ──

        /** 获取所有采样点的最小参数值 */
        float32 GetMinParameter() const noexcept;

        /** 获取所有采样点的最大参数值 */
        float32 GetMaxParameter() const noexcept;

        /**
         * @brief 将参数值归一化到 [0, 1] 范围
         * @param paramValue 原始参数值
         * @return [0, 1] 范围内的归一化值
         *
         * 如果 min == max（只有一个采样点），返回 0.0f。
         */
        float32 NormalizeParameter(float32 paramValue) const;

    private:
        /**
         * @brief 内部：找到 paramValue 前后的两个采样点索引
         * @param paramValue 参数值
         * @param outLo      输出：低索引（≤ paramValue）
         * @param outHi      输出：高索引（≥ paramValue）
         * @return 找到的采样点数量用于混合（0=无, 1=单个, 2=两个）
         *
         * 返回 0：无采样点
         * 返回 1：只有一个采样点，或 paramValue 在范围外（outLo=outHi）
         * 返回 2：找到了两个相邻采样点
         */
        int32 FindNeighbors(float32 paramValue, int32& outLo, int32& outHi) const;

        // 采样点列表（按 parameterValue 升序排列）
        std::vector<BlendSpace1DSample> m_Samples;
    };

} // namespace Engine
