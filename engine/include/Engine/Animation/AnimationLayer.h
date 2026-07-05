#pragma once

/**
 * @file AnimationLayer.h
 * @brief 动画状态层 — 多层独立状态机 + 层混合优化
 *
 * ════════════════════════════════════════════
 * 层系统设计
 * ════════════════════════════════════════════
 *
 *   每个层拥有独立的状态机 + BlendMask + 融合权重。
 *   各层独立更新时间线，最后按权重+遮罩合并。
 *
 *   优化策略：
 *     1. 独立时间线 — 每层有自己的局部时间
 *     2. 混合树扁平化 — 当需要时可将多层融合为单棵 BlendTree
 *     3. 遮罩裁剪 — 只更新遮罩覆盖的骨骼
 *
 * ════════════════════════════════════════════
 * 使用示例
 * ════════════════════════════════════════════
 *
 *   AnimationLayerSystem layers(skeleton, resource);
 *
 *   // 创建层
 *   auto& upper = layers.AddLayer("UpperBody");
 *   upper.SetMask(upperBodyMask);
 *   upper.SetWeight(1.0f);
 *   upper.GetStateMachine().AddState("Aim", aimTree);
 *   upper.GetStateMachine().AddState("Attack", attackTree);
 *
 *   auto& lower = layers.AddLayer("LowerBody");
 *   lower.SetMask(lowerBodyMask);
 *   lower.GetStateMachine().AddState("Idle", idleTree);
 *   lower.GetStateMachine().AddState("Walk", walkTree);
 *
 *   // 每帧更新
 *   layers.Update(dt);
 *
 *   // 扁平化为单棵树（优化）
 *   layers.Flatten();
 */

#include "Engine/Animation/AnimStateMachine.h"
#include "Engine/Animation/BlendMask.h"
#include <string>
#include <memory>
#include <vector>

namespace Engine {

    // ============================================================
    // 单层数据
    // ============================================================
    struct AnimationLayerData {
        std::string                 name;
        float32                     weight  = 1.0f;
        std::shared_ptr<BlendMask>  mask;
        AnimStateMachine            stateMachine;
        PoseLocalData               localPose;

        AnimationLayerData() = default;
        explicit AnimationLayerData(std::string n) : name(std::move(n)) {}
        AnimationLayerData(AnimationLayerData&&) = default;
        AnimationLayerData& operator=(AnimationLayerData&&) = default;
    };

    // ============================================================
    // 层系统
    // ============================================================
    class AnimationLayerSystem {
    public:
        AnimationLayerSystem(Skeleton& skeleton, const AnimationResource& resource)
            : m_Skeleton(&skeleton), m_Resource(&resource) {}
        ~AnimationLayerSystem() = default;

        // ── 层管理 ──

        /** 添加层 */
        AnimationLayerData& AddLayer(const std::string& name,
                                      std::shared_ptr<BlendMask> mask = nullptr,
                                      float32 weight = 1.0f);

        /** 按名称获取层 */
        AnimationLayerData* GetLayer(const std::string& name);
        const AnimationLayerData* GetLayer(const std::string& name) const;

        /** 移除层 */
        void RemoveLayer(const std::string& name);
        void Clear();

        /** 获取所有层 */
        std::vector<AnimationLayerData>& GetLayers() { return m_Layers; }
        size_t GetLayerCount() const { return m_Layers.size(); }

        // ── 更新与混合 ──

        /**
         * @brief 更新所有层
         * @param dt 帧时间
         *
         * 每层独立更新状态机后，按 mask + weight 混合到 skeleton。
         */
        void Update(float32 dt);

        /**
         * @brief 扁平化：将多层融合为单棵树
         *
         * 创建一棵等效的 BlendTree，将各层的 StateMachine + BlendTree
         * 组合成一个统一的树结构。适用于最终确定后的优化。
         */
        void Flatten();

        /** 获取扁平化后的树 */
        BlendTree* GetFlattenedTree() { return m_FlattenedTree.get(); }

        // ── 配置 ──

        void SetGlobalWeight(float32 w) { m_GlobalWeight = w; }
        float32 GetGlobalWeight() const { return m_GlobalWeight; }

    private:
        Skeleton*                m_Skeleton;
        const AnimationResource* m_Resource;
        std::vector<AnimationLayerData> m_Layers;
        float32                  m_GlobalWeight = 1.0f;

        // 扁平化后的融合树（优化用）
        std::unique_ptr<BlendTree> m_FlattenedTree;
    };

} // namespace Engine
