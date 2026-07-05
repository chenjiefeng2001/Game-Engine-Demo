#pragma once

/**
 * @file SkinningComponent.h
 * @brief 蒙皮组件 — 挂载到 GameObject 上，管理骨骼、蒙皮网格和动画姿势
 *
 * SkinningComponent 整合了 Skeleton、SkinnedMesh 和 AnimationPose，
 * 提供完整的骨骼动画驱动管线。
 *
 * ═══════════════════════════════════════════════════════════
 * 蒙皮矩阵管线（模型空间 → 绑定矩阵 → 当前姿势矩阵）
 * ═══════════════════════════════════════════════════════════
 *
 *   [模型空间·绑定姿势顶点]
 *          │
 *          ▼  × inverseBindMatrix
 *   [骨骼局部空间]
 *          │
 *          ▼  × currentPoseMatrix (骨骼世界矩阵)
 *   [模型空间·当前姿势顶点]
 *          │
 *          ▼  = skinningMatrix = currentPoseMatrix * inverseBindMatrix
 *
 * 最终蒙皮矩阵将顶点从 绑定姿势模型空间 直接变换到 当前姿势模型空间。
 *
 * ═══════════════════════════════════════════════════════════
 *
 * 使用示例：
 * @code
 *   auto obj = std::make_shared<GameObject>("Character");
 *   auto* skin = obj->AddComponent<SkinningComponent>();
 *   skin->SetSkeleton(mySkeleton);
 *   skin->SetSkinnedMesh(mySkinnedMesh);
 *   skin->SetAnimation(myClip);
 *
 *   // 每帧（OnUpdate 自动调用）：
 *   skin->AdvanceAnimation(dt);
 *
 *   // 获取蒙皮矩阵上传 GPU（通过渲染器接口）：
 * @endcode
 */

#include "Engine/Core/GameObject/Component.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Animation/SkinnedMesh.h"
#include "Engine/Animation/AnimationPose.h"
#include "Engine/Animation/AnimationLocalTimeline.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <memory>
#include <vector>

namespace Engine {

    /**
     * @brief 蒙皮组件 — 骨骼动画驱动的数据组件
     *
     * 蒙皮矩阵管线核心计算：
     *   skinningMatrix[i] = skeleton.bones[i].currentPoseMatrix
     *                      * skeleton.bones[i].inverseBindMatrix
     *
     * 渲染由外部的渲染器负责，本组件只提供数据和矩阵。
     */
    class SkinningComponent : public Component {
    public:
        SkinningComponent() = default;
        ~SkinningComponent() override = default;

        // 禁止拷贝
        SkinningComponent(const SkinningComponent&) = delete;
        SkinningComponent& operator=(const SkinningComponent&) = delete;

        // ── 生命周期 ──
        void OnUpdate(float32 dt) override;

        // ── 骨骼 ──
        void SetSkeleton(std::shared_ptr<Skeleton> skeleton);
        std::shared_ptr<Skeleton> GetSkeleton() const noexcept { return m_Skeleton; }
        bool HasSkeleton() const noexcept { return m_Skeleton != nullptr; }

        // ── 蒙皮网格 ──
        void SetSkinnedMesh(std::shared_ptr<SkinnedMesh> mesh);
        std::shared_ptr<SkinnedMesh> GetSkinnedMesh() const noexcept { return m_SkinnedMesh; }
        bool HasSkinnedMesh() const noexcept { return m_SkinnedMesh != nullptr; }

        // ── 动画 ──
        void SetAnimation(std::shared_ptr<AnimationLocalTimeline> animation);
        std::shared_ptr<AnimationLocalTimeline> GetAnimation() const noexcept { return m_Animation; }

        /** 推进动画并更新蒙皮矩阵（每帧调用） */
        void AdvanceAnimation(float32 dt);

        /** 立即求值当前动画姿势并更新蒙皮矩阵 */
        void EvaluatePose();

        // ── 蒙皮矩阵（核心输出） ──

        /**
         * @brief 获取蒙皮矩阵数组
         *
         * 每个矩阵 = bone.currentPoseMatrix * bone.inverseBindMatrix
         * 将顶点从 绑定姿势模型空间 变换到 当前姿势模型空间。
         */
        const std::vector<Mat4>& GetSkinningMatrices() const noexcept {
            return m_SkinningMatrices;
        }

        /** 获取蒙皮矩阵的 float* 指针（用于 glUniformMatrix4fv） */
        const float* GetSkinningMatrixData() const {
            return m_SkinningMatrices.empty()
                       ? nullptr
                       : m_SkinningMatrices[0].Data();
        }

        /** 获取矩阵数量（= 骨骼数量） */
        uint32 GetMatrixCount() const noexcept {
            return static_cast<uint32>(m_SkinningMatrices.size());
        }

        // ── 可见性 ──
        void SetVisible(bool visible) noexcept { m_Visible = visible; }
        bool IsVisible() const noexcept { return m_Visible; }

    private:
        std::shared_ptr<Skeleton>                m_Skeleton;
        std::shared_ptr<SkinnedMesh>             m_SkinnedMesh;
        std::shared_ptr<AnimationLocalTimeline>  m_Animation;
        std::unique_ptr<AnimationPose>           m_PoseEvaluator;

        // 蒙皮矩阵缓存（与 Skeleton 骨骼一一对应）
        std::vector<Mat4> m_SkinningMatrices;

        bool m_Visible = true;
    };

} // namespace Engine
