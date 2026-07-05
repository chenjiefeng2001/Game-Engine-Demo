#pragma once

/**
 * @file AnimationResource.h
 * @brief 动画共享资源 — 可被多个动画实例共享的只读数据
 *
 * AnimationResource 包含所有动画实例之间共享的只读数据：
 *   1. 骨骼（Skeleton）— 绑定姿势、骨骼层级
 *   2. 蒙皮网格（SkinnedMesh）— 顶点数据、骨骼绑定
 *   3. 动画片段库 — 命名的 AnimationLocalTimeline 集合
 *
 * 一个 AnimationResource 可被多个 AnimationInstance 引用，
 * 实现动画数据的复用，减少内存占用。
 */

#include "Engine/Animation/Skeleton.h"
#include "Engine/Animation/SkinnedMesh.h"
#include "Engine/Animation/AnimationLocalTimeline.h"
#include <string>
#include <memory>
#include <unordered_map>

namespace Engine {

    /**
     * @brief 动画共享资源
     *
     * 使用示例：
     * @code
     *   auto res = std::make_shared<AnimationResource>();
     *   res->skeleton = mySkeleton;
     *   res->skinnedMesh = myMesh;
     *   res->AddClip("walk", walkClip);
     *   res->AddClip("run",  runClip);
     *
     *   // 多个实例共享同一资源
     *   auto inst1 = std::make_shared<AnimationInstance>(res);
     *   auto inst2 = std::make_shared<AnimationInstance>(res);
     * @endcode
     */
    class AnimationResource {
    public:
        AnimationResource() = default;
        ~AnimationResource() = default;

        // ── 骨骼 ──
        std::shared_ptr<Skeleton> skeleton;         ///< 绑定姿势骨骼
        std::shared_ptr<Skeleton> bindPose;         ///< 绑定姿势副本（用于加法混合参考）

        // ── 蒙皮网格 ──
        std::shared_ptr<SkinnedMesh> skinnedMesh;   ///< 蒙皮网格数据

        // ── 动画片段库 ──
        void AddClip(const std::string& name, std::shared_ptr<AnimationLocalTimeline> clip) {
            m_Clips[name] = std::move(clip);
        }

        std::shared_ptr<AnimationLocalTimeline> GetClip(const std::string& name) const {
            auto it = m_Clips.find(name);
            return it != m_Clips.end() ? it->second : nullptr;
        }

        bool HasClip(const std::string& name) const {
            return m_Clips.find(name) != m_Clips.end();
        }

        void RemoveClip(const std::string& name) { m_Clips.erase(name); }
        void ClearClips() { m_Clips.clear(); }

        const std::unordered_map<std::string, std::shared_ptr<AnimationLocalTimeline>>&
        GetAllClips() const noexcept { return m_Clips; }

        // ── 便捷构造 ──

        /** 从现有数据创建共享资源 */
        static std::shared_ptr<AnimationResource> Create(
            std::shared_ptr<Skeleton> skel,
            std::shared_ptr<SkinnedMesh> mesh = nullptr) {
            auto res = std::make_shared<AnimationResource>();
            res->skeleton = std::move(skel);

            // 创建绑定姿势副本
            if (res->skeleton) {
                auto bindCopy = std::make_shared<Skeleton>(std::move(*res->skeleton));
                bindCopy->ResetToBindPose();
                res->bindPose = std::move(bindCopy);
            }

            res->skinnedMesh = std::move(mesh);
            return res;
        }

    private:
        std::unordered_map<std::string, std::shared_ptr<AnimationLocalTimeline>> m_Clips;
    };

} // namespace Engine
