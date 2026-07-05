#pragma once

/**
 * @file AnimationPose.h
 * @brief 动画姿势求值器 — 将动画时间线上的轨道数据应用到骨骼上
 *
 * AnimationPose 连接 AnimationLocalTimeline 和 Skeleton：
 *   1. 从 AnimationLocalTimeline 的浮点轨道中读取骨骼变换数据
 *   2. 计算每个骨骼的局部姿势矩阵（平移 + 旋转 + 缩放）
 *   3. 写入 Skeleton 的 localPoseMatrix
 *   4. 调用 Skeleton::UpdateWorldPoses() 计算世界矩阵和蒙皮矩阵
 *
 * 命名约定：轨道名称格式为 "BoneName.property"
 *   例如: "Hips.position", "Spine.rotation", "Head.scale"
 *
 * 属性后缀：
 *   - .position: Vec3 轨道，骨骼局部平移
 *   - .rotation: Vec3 轨道，骨骼局部旋转（欧拉角，度）
 *   - .scale:    Vec3 轨道，骨骼局部缩放
 */

#include "Engine/Animation/Skeleton.h"
#include "Engine/Animation/AnimationLocalTimeline.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <string>
#include <memory>

namespace Engine {

    /**
     * @brief 动画姿势求值器
     *
     * 使用示例：
     * @code
     *   AnimationPose pose(skeleton);
     *
     *   // 创建动画时间线，轨道命名遵循 "BoneName.property" 约定
     *   auto anim = std::make_shared<AnimationLocalTimeline>("Walk");
     *   anim->GetFloatTrack("Hips.position")->AddKeyFrame({0.0f, Vec3(0,0,0)});
     *   // ... 更多轨道
     *
     *   // 每帧：
     *   anim->Update(dt);
     *   pose.Evaluate(*anim);  // 将动画数据写入骨骼
     *   skeleton.UpdateWorldPoses();  // 计算世界矩阵和蒙皮矩阵
     *
     *   // 上传蒙皮矩阵到 GPU（通过渲染器接口）
     * @endcode
     */
    class AnimationPose {
    public:
        explicit AnimationPose(std::shared_ptr<Skeleton> skeleton)
            : m_Skeleton(std::move(skeleton)) {}

        ~AnimationPose() = default;

        // 禁止拷贝
        AnimationPose(const AnimationPose&) = delete;
        AnimationPose& operator=(const AnimationPose&) = delete;

        // ── 骨骼 ──
        void SetSkeleton(std::shared_ptr<Skeleton> skeleton) { m_Skeleton = skeleton; }
        std::shared_ptr<Skeleton> GetSkeleton() const noexcept { return m_Skeleton; }
        bool HasSkeleton() const noexcept { return m_Skeleton != nullptr; }

        // ── 姿势求值 ──

        /**
         * @brief 从动画时间线求值并更新骨骼的局部姿势矩阵
         *
         * 扫描 timeLine 中的所有浮点轨道，按命名约定解析出
         * 骨骼名称和属性，更新对应骨骼的 localPoseMatrix。
         *
         * @param timeLine 动画时间线（包含浮点轨道）
         *
         * 注意：此函数仅更新 localPoseMatrix，不计算世界矩阵。
         * 需要后续调用 Skeleton::UpdateWorldPoses()。
         */
        void EvaluateFromTimeline(const AnimationLocalTimeline& timeLine);

        /**
         * @brief 从外部直接设置骨骼的局部姿势
         * @param boneIndex 骨骼索引
         * @param position  局部平移
         * @param rotation  局部旋转（欧拉角，度）
         * @param scale     局部缩放
         */
        void SetBoneLocalPose(int32 boneIndex,
                              const Vec3& position,
                              const Vec3& rotation,
                              const Vec3& scale);

        /**
         * @brief 从外部直接设置骨骼的局部变换矩阵
         * @param boneIndex 骨骼索引
         * @param localMatrix 局部变换矩阵
         */
        void SetBoneLocalMatrix(int32 boneIndex, const Mat4& localMatrix);

        /**
         * @brief 计算所有骨骼的局部姿势矩阵并更新世界矩阵
         *
         * 便捷方法，等价于：
         *   EvaluateFromTimeline(timeLine);
         *   m_Skeleton->UpdateWorldPoses();
         */
        void EvaluateAndUpdate(const AnimationLocalTimeline& timeLine);

        // ── 辅助工具 ──

        /**
         * @brief 从平移/旋转/缩放构建局部变换矩阵
         * @param pos     平移
         * @param rotEuler 旋转（欧拉角，度）
         * @param scale   缩放
         * @param out     输出矩阵
         *
         * 使用 glm 实现：T * R * S
         */
        static void ComposeMatrix(const Vec3& pos, const Vec3& rotEuler,
                                  const Vec3& scale, Mat4& out);

    private:
        std::shared_ptr<Skeleton> m_Skeleton;
    };

} // namespace Engine
