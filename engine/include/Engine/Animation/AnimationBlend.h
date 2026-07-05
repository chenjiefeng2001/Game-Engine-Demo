#pragma once

/**
 * @file AnimationBlend.h
 * @brief 动画混合工具 — LERP / SLERP 插值与姿势混合
 *
 * 提供两套核心能力：
 *
 * 1. 基础数学插值（LERP / SLERP）
 *    - LERP: 线性插值（浮点、向量、矩阵）
 *    - SLERP: 球面线性插值（四元数），用于旋转插值
 *
 * 2. 动画姿势混合（BlendPose）
 *    - 在两个骨骼姿势之间按权重混合（位置 LERP + 旋转 SLERP + 缩放 LERP）
 *    - 在两个 AnimationLocalTimeline 的求值结果之间混合
 *
 * ════════════════════════════════════════════
 * 使用示例
 * ════════════════════════════════════════════
 *
 * // 基础插值
 * float v = AnimationBlend::LERP(0.0f, 10.0f, 0.5f);       // → 5.0f
 * Vec3  p = AnimationBlend::LERP(Vec3(0,0,0), Vec3(10,0,0), 0.3f);
 * Quat  r = AnimationBlend::SLERP(Quat::Identity(), targetRot, 0.5f);
 *
 * // 骨骼姿势混合（在两个 Skeleton 之间）
 * AnimationBlend::BlendSkeletonPose(
 *     outputSkeleton,     // 输出
 *     skeletonPoseA,      // 源姿势 A（权重 0 时完全为此姿势）
 *     skeletonPoseB,      // 源姿势 B（权重 1 时完全为此姿势）
 *     0.3f                // 混合因子
 * );
 *
 * // 动画时间线求值混合
 * AnimationBlend::BlendTimelinePose(
 *     skeleton,           // 目标骨骼
 *     timelineA,          // 动画时间线 A
 *     timelineB,          // 动画时间线 B
 *     blendWeight,        // 混合因子
 *     localTime           // 两个时间线共享的求值时间
 * );
 *
 * ════════════════════════════════════════════
 * 混合策略说明
 * ════════════════════════════════════════════
 * 对骨骼的每个通道：
 *   Translation → LERP（线性插值平移）
 *   Rotation    → SLERP（球面插值旋转，用四元数）
 *   Scale       → LERP（线性插值缩放）
 *
 * 对矩阵的 LERP 是逐元素插值（不适合旋转矩阵），
 * 骨骼混合始终在分解后的通道上操作。
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Animation/Bone.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Animation/AnimationLocalTimeline.h"
#include "Engine/Animation/AnimationPose.h"
#include "Engine/Animation/BlendMask.h"

namespace Engine {

    // ============================================================
    // 动画混合工具（静态类）
    // ============================================================
    class AnimationBlend {
    public:
        AnimationBlend() = delete;

        // ════════════════════════════════════════════
        // 基础 LERP 插值（线性插值）
        // ════════════════════════════════════════════

        /** 标量线性插值: result = a + t * (b - a) */
        static float32 LERP(float32 a, float32 b, float32 t);

        /** Vec2 线性插值 */
        static Vec2 LERP(const Vec2& a, const Vec2& b, float32 t);

        /** Vec3 线性插值 */
        static Vec3 LERP(const Vec3& a, const Vec3& b, float32 t);

        /** Vec4 线性插值 */
        static Vec4 LERP(const Vec4& a, const Vec4& b, float32 t);

        /**
         * @brief 矩阵逐元素线性插值
         * @note 不保证结果矩阵是有效的旋转/刚体变换矩阵！
         *       用于非旋转矩阵的近似过渡，或仅作为预览。
         *       骨骼混合应使用分解后的 LERP+SLERP。
         */
        static Mat4 LERP(const Mat4& a, const Mat4& b, float32 t);

        // ════════════════════════════════════════════
        // SLERP（球面线性插值）
        // ════════════════════════════════════════════

        /**
         * @brief 四元数球面线性插值
         * @param a 起始四元数
         * @param b 目标四元数
         * @param t 插值因子 [0, 1]
         * @return 插值后的四元数（单位四元数）
         *
         * 沿四维球面上两点之间的最短弧进行恒速插值，
         * 保持旋转的几何正确性。
         * 当 a 和 b 非常接近时自动退化为 LERP 以避免除零。
         */
        static Quat SLERP(const Quat& a, const Quat& b, float32 t);

        /**
         * @brief 四元数 LERP（快速近似，不平滑速度）
         * @note 不会保持恒角速度，但速度快。
         *       结果自动归一化。
         */
        static Quat LERPQuat(const Quat& a, const Quat& b, float32 t);

        // ════════════════════════════════════════════
        // 四元数 ↔ 欧拉角 / 矩阵 转换
        // ════════════════════════════════════════════

        /** 欧拉角（度）→ 四元数 */
        static Quat FromEuler(const Vec3& eulerDeg);

        /** 四元数 → 欧拉角（度） */
        static Vec3 ToEuler(const Quat& q);

        /** 四元数 → 3×3 旋转矩阵（写入 Mat4 的左上 3×3，其余为单位阵） */
        static Mat4 ToMatrix(const Quat& q);

        /** 从 3×3 旋转矩阵提取四元数（从 Mat4 的左上角） */
        static Quat FromMatrix(const Mat4& m);

        // ════════════════════════════════════════════
        // 姿势混合（高级 API）
        // ════════════════════════════════════════════

        /**
         * @brief 在两个骨骼姿势之间混合
         * @param outSkeleton 输出骨骼（其 localPoseMatrix 会被覆盖）
         * @param poseA       源姿势 A 的骨骼（t=0 时完全为此姿势）
         * @param poseB       源姿势 B 的骨骼（t=1 时完全为此姿势）
         * @param t           混合因子 [0, 1]
         *
         * 对每个骨骼：
         *   1. 从 poseA/B 的 localPoseMatrix 分解出平移/旋转/缩放
         *   2. 平移 → LERP, 旋转 → SLERP, 缩放 → LERP
         *   3. 重组为新的 localPoseMatrix 写入 outSkeleton
         *   4. 调用 outSkeleton.UpdateWorldPoses() 更新蒙皮矩阵
         *
         * @note poseA 和 poseB 必须有相同的骨骼结构（相同数量的骨骼）
         */
        static void BlendSkeletonPose(Skeleton& outSkeleton,
                                      const Skeleton& poseA,
                                      const Skeleton& poseB,
                                      float32 t);

        /**
         * @brief 从两个动画时间线求值并混合
         * @param skeleton    目标骨骼（局部姿势矩阵会被覆盖）
         * @param poseEval    姿势求值器
         * @param timelineA   动画时间线 A
         * @param timelineB   动画时间线 B
         * @param t           混合因子 [0, 1]
         * @param localTime   求值时间点
         *
         * 此方法：
         *   1. 分别用 timelineA 和 timelineB 对 skeleton 求值
         *   2. 保存两次求值结果的骨骼局部姿势快照
         *   3. 对每个骨骼的 T/R/S 做 LERP/SLERP 混合
         *   4. 更新世界矩阵和蒙皮矩阵
         *
         * @note 此函数会复制骨骼姿势，若追求极致性能，
         *       可手动管理两个 Skeleton 副本并调用 BlendSkeletonPose。
         */
        static void BlendTimelinePose(Skeleton& skeleton,
                                      AnimationPose& poseEval,
                                      const AnimationLocalTimeline& timelineA,
                                      const AnimationLocalTimeline& timelineB,
                                      float32 t,
                                      float32 localTime);

        // ════════════════════════════════════════════
        // 分部混合（Masked Blending）
        // ════════════════════════════════════════════

        /**
         * @brief 带混合遮罩的骨骼姿势混合
         * @param outSkeleton 输出骨骼
         * @param poseA       基础姿势（mask=0 时完全使用此姿势）
         * @param poseB       混合姿势（mask=1 时完全使用此姿势）
         * @param mask        每骨骼混合权重 [0, 1]
         * @param t           全局混合因子（与 mask 叠加）
         *
         * 对每个骨骼 i：
         *   effectiveWeight = mask.GetWeight(i) * t
         *   result[i] = LERP/SLERP(poseA[i], poseB[i], effectiveWeight)
         */
        static void BlendSkeletonPoseMasked(Skeleton& outSkeleton,
                                             const Skeleton& poseA,
                                             const Skeleton& poseB,
                                             const BlendMask& mask,
                                             float32 t = 1.0f);

        // ════════════════════════════════════════════
        // 加法混合（Additive Blending）
        // ════════════════════════════════════════════

        /**
         * @brief 应用加法动画姿势
         *
         * 加法混合的计算方式：
         *   Translation: result = base + (additive - reference) * weight
         *   Rotation:    result = base * slerp(identity, ref^{-1} * add, weight)
         *   Scale:       result = base + (additive - reference) * weight
         *
         * @param basePose     基础姿势（会被修改为结果）
         * @param additivePose 加法姿势（要叠加的动画）
         * @param referencePose 参考姿势（加法姿势的"零位"，通常为绑定姿势）
         * @param weight        加法强度 [0, 1]
         *
         * 典型用途：呼吸、受击、武器后坐力等叠加效果
         */
        static void ApplyAdditivePose(Skeleton& basePose,
                                       const Skeleton& additivePose,
                                       const Skeleton& referencePose,
                                       float32 weight = 1.0f);

        /**
         * @brief 带遮罩的加法混合
         *
         * @param basePose     基础姿势
         * @param additivePose 加法姿势
         * @param referencePose 参考姿势
         * @param mask         每骨骼加法强度遮罩
         * @param weight       全局加法强度 [0, 1]
         */
        static void ApplyAdditivePoseMasked(Skeleton& basePose,
                                             const Skeleton& additivePose,
                                             const Skeleton& referencePose,
                                             const BlendMask& mask,
                                             float32 weight = 1.0f);

        // ── 矩阵分解工具（公开，供 BlendSpace2D 等使用） ──

        /** 从 localPoseMatrix 分解平移（矩阵的平移列） */
        static Vec3 DecomposeTranslation(const Mat4& m);

        /** 从 localPoseMatrix 分解旋转（四元数） */
        static Quat DecomposeRotation(const Mat4& m);

        /** 从 localPoseMatrix 分解缩放 */
        static Vec3 DecomposeScale(const Mat4& m);

    private:
        // 预留私有成员
    };

} // namespace Engine
