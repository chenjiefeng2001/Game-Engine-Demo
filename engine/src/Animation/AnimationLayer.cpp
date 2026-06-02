/**
 * @file AnimationLayer.cpp
 * @brief 动画状态层实现
 */

#include "Engine/Animation/AnimationLayer.h"
#include <algorithm>

namespace Engine {

    // ════════════════════════════════════════════
    // 层管理
    // ════════════════════════════════════════════

    AnimationLayerData& AnimationLayerSystem::AddLayer(const std::string& name,
                                                        std::shared_ptr<BlendMask> mask,
                                                        float32 weight) {
        AnimationLayerData layer(name);
        layer.weight = weight;
        layer.mask = mask ? std::move(mask) : nullptr;

        // 初始化姿势缓冲区
        if (m_Skeleton) {
            layer.localPose.Resize(m_Skeleton->GetBoneCount());
        }

        m_Layers.push_back(std::move(layer));
        return m_Layers.back();
    }

    AnimationLayerData* AnimationLayerSystem::GetLayer(const std::string& name) {
        for (auto& l : m_Layers) {
            if (l.name == name) return &l;
        }
        return nullptr;
    }

    const AnimationLayerData* AnimationLayerSystem::GetLayer(const std::string& name) const {
        for (const auto& l : m_Layers) {
            if (l.name == name) return &l;
        }
        return nullptr;
    }

    void AnimationLayerSystem::RemoveLayer(const std::string& name) {
        auto it = std::remove_if(m_Layers.begin(), m_Layers.end(),
            [&](const AnimationLayerData& l) { return l.name == name; });
        m_Layers.erase(it, m_Layers.end());
    }

    void AnimationLayerSystem::Clear() {
        m_Layers.clear();
        m_FlattenedTree.reset();
    }

    // ════════════════════════════════════════════
    // 更新与混合
    // ════════════════════════════════════════════

    void AnimationLayerSystem::Update(float32 dt) {
        if (!m_Skeleton) return;

        size_t boneCount = m_Skeleton->GetBoneCount();
        if (boneCount == 0) return;

        // 清零骨骼姿势（从绑定姿势开始）
        m_Skeleton->ResetToBindPose();

        // 逐层更新并累加
        for (auto& layer : m_Layers) {
            if (layer.weight <= 0.0f) continue;

            // 更新此层的状态机
            layer.stateMachine.Update(*m_Skeleton, *m_Resource, dt);

            // 提取此层的结果姿势
            for (size_t i = 0; i < boneCount; ++i) {
                const Bone& bone = m_Skeleton->GetBone(static_cast<int32>(i));
                layer.localPose.translations[i] = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
                layer.localPose.rotations[i]    = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
                layer.localPose.scales[i]       = AnimationBlend::DecomposeScale(bone.localPoseMatrix);
            }
        }

        // 如果有多个层，按 mask+weight 混合
        if (m_Layers.size() > 1) {
            // 重新从绑定姿势开始
            m_Skeleton->ResetToBindPose();
            PoseLocalData result;
            result.Resize(boneCount);
            for (size_t i = 0; i < boneCount; ++i) {
                result.translations[i] = AnimationBlend::DecomposeTranslation(
                    m_Skeleton->GetBone(static_cast<int32>(i)).localPoseMatrix);
                result.rotations[i] = AnimationBlend::DecomposeRotation(
                    m_Skeleton->GetBone(static_cast<int32>(i)).localPoseMatrix);
                result.scales[i] = AnimationBlend::DecomposeScale(
                    m_Skeleton->GetBone(static_cast<int32>(i)).localPoseMatrix);
            }

            // 从第一层开始累加
            bool first = true;
            for (const auto& layer : m_Layers) {
                if (layer.weight <= 0.0f) continue;

                for (size_t i = 0; i < boneCount; ++i) {
                    // 计算此骨骼的有效权重
                    float32 w = layer.weight;
                    if (layer.mask && i < static_cast<size_t>(layer.mask->GetSize())) {
                        w *= layer.mask->GetBoneWeight(static_cast<int32>(i));
                    }

                    if (first) {
                        result.translations[i] = {
                            layer.localPose.translations[i].x * w,
                            layer.localPose.translations[i].y * w,
                            layer.localPose.translations[i].z * w
                        };
                        result.rotations[i] = layer.localPose.rotations[i];
                        result.scales[i] = {
                            layer.localPose.scales[i].x * w,
                            layer.localPose.scales[i].y * w,
                            layer.localPose.scales[i].z * w
                        };
                    } else {
                        result.translations[i].x += layer.localPose.translations[i].x * w;
                        result.translations[i].y += layer.localPose.translations[i].y * w;
                        result.translations[i].z += layer.localPose.translations[i].z * w;
                        result.rotations[i] = AnimationBlend::SLERP(
                            result.rotations[i], layer.localPose.rotations[i], w);
                        result.scales[i].x += layer.localPose.scales[i].x * w;
                        result.scales[i].y += layer.localPose.scales[i].y * w;
                        result.scales[i].z += layer.localPose.scales[i].z * w;
                    }
                }
                first = false;
            }

            // 将结果写回骨骼
            for (size_t i = 0; i < boneCount; ++i) {
                Vec3 euler = AnimationBlend::ToEuler(result.rotations[i]);
                AnimationPose::ComposeMatrix(
                    result.translations[i], euler, result.scales[i],
                    m_Skeleton->GetBone(static_cast<int32>(i)).localPoseMatrix);
            }
        } else if (!m_Layers.empty()) {
            // 单层：直接使用该层结果
            const auto& layer = m_Layers.front();
            for (size_t i = 0; i < boneCount; ++i) {
                Vec3 euler = AnimationBlend::ToEuler(layer.localPose.rotations[i]);
                AnimationPose::ComposeMatrix(
                    layer.localPose.translations[i], euler, layer.localPose.scales[i],
                    m_Skeleton->GetBone(static_cast<int32>(i)).localPoseMatrix);
            }
        }

        // 更新世界矩阵和蒙皮矩阵
        m_Skeleton->UpdateWorldPoses();
    }

    // ════════════════════════════════════════════
    // 扁平化优化
    // ════════════════════════════════════════════

    void AnimationLayerSystem::Flatten() {
        if (!m_Skeleton || m_Layers.empty()) return;

        m_FlattenedTree = std::make_unique<BlendTree>();

        if (m_Layers.size() == 1) {
            // 单层：直接使用该层的状态机树
            const auto& layer = m_Layers.front();
            const auto& root = layer.stateMachine.GetCurrentState();
            (void)root;  // Flatten 完整实现需遍历状态机获取 BlendTree
        } else {
            // 多层：创建加权平均 BlendTree
            auto* avg = m_FlattenedTree->AddWeightedAvg();
            for (auto& layer : m_Layers) {
                // 将各层的 BlendTree 添加为输入
                // 实际实现需要遍历状态机获取当前 BlendTree
                avg->AddInput(layer.name, layer.weight, false);
            }
            m_FlattenedTree->SetRoot(
                std::unique_ptr<BlendNode>(static_cast<BlendNode*>(nullptr)));
        }
    }

} // namespace Engine


