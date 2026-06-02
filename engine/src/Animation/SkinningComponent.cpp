/**
 * @file SkinningComponent.cpp
 * @brief 蒙皮组件实现 — 蒙皮矩阵管线计算
 *
 * 蒙皮矩阵管线：
 *   1. SetSkeleton 时计算 inverseBindMatrix（绑定姿势的逆矩阵）
 *   2. AdvanceAnimation 时从动画时间线更新骨骼的局部姿势矩阵
 *   3. Skeleton::UpdateWorldPoses() 计算每个骨骼的 currentPoseMatrix（世界矩阵）
 *   4. 对每个骨骼：skinningMatrix = currentPoseMatrix * inverseBindMatrix
 *   5. 输出蒙皮矩阵数组供 GPU 使用
 */

#include "Engine/Animation/SkinningComponent.h"
#include "Engine/Core/GameObject/GameObject.h"

namespace Engine {

    // ============================================================
    // 生命周期
    // ============================================================

    void SkinningComponent::OnUpdate(float32 dt) {
        Component::OnUpdate(dt);

        // 自动推进动画（如果有的话）
        if (m_Animation && m_Animation->IsPlaying()) {
            AdvanceAnimation(dt);
        }
    }

    // ============================================================
    // 骨骼 — 设置时自动计算 inverseBindMatrix
    // ============================================================

    void SkinningComponent::SetSkeleton(std::shared_ptr<Skeleton> skeleton) {
        m_Skeleton = std::move(skeleton);
        m_PoseEvaluator = std::make_unique<AnimationPose>(m_Skeleton);

        // 初始化为绑定姿势
        if (m_Skeleton) {
            m_Skeleton->ResetToBindPose();
            m_SkinningMatrices = m_Skeleton->GetSkinningMatrices();
        }
    }

    // ============================================================
    // 蒙皮网格
    // ============================================================

    void SkinningComponent::SetSkinnedMesh(std::shared_ptr<SkinnedMesh> mesh) {
        m_SkinnedMesh = std::move(mesh);
    }

    // ============================================================
    // 动画
    // ============================================================

    void SkinningComponent::SetAnimation(std::shared_ptr<AnimationLocalTimeline> animation) {
        m_Animation = std::move(animation);
    }

    // ============================================================
    // 核心管线：推进动画 → 求值姿势 → 计算蒙皮矩阵
    // ============================================================

    void SkinningComponent::AdvanceAnimation(float32 dt) {
        if (!m_Animation || !m_Skeleton)
            return;

        // 1. 推进动画时间线（计算轨道值）
        m_Animation->AdvanceTime(dt);

        // 2. 求值当前姿势并更新蒙皮矩阵
        EvaluatePose();
    }

    void SkinningComponent::EvaluatePose() {
        if (!m_Skeleton || !m_PoseEvaluator)
            return;

        // 1. 从动画时间线求值骨骼的局部姿势矩阵（localPoseMatrix）
        //    轨道命名约定：轨道名称 = "BoneName.property"
        //    如 "Hips.position", "Spine.rotation", "Head.scale"
        if (m_Animation) {
            m_PoseEvaluator->EvaluateFromTimeline(*m_Animation);
        }

        // 2. 递归计算世界矩阵和蒙皮矩阵
        //    - 对每个骨骼：currentPoseMatrix = parentWorld * localPoseMatrix
        //    - 对每个骨骼：skinningMatrix = currentPoseMatrix * inverseBindMatrix
        m_Skeleton->UpdateWorldPoses();

        // 3. 同步蒙皮矩阵缓存
        m_SkinningMatrices = m_Skeleton->GetSkinningMatrices();
    }

} // namespace Engine
