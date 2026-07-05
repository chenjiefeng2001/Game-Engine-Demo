/**
 * @file AnimationInstance.cpp
 * @brief 动画实例实现 — 每实体状态管理
 */

#include "Engine/Animation/AnimationInstance.h"
#include "Engine/Animation/AnimationRetarget.h"
#include <algorithm>
#include <cmath>

namespace Engine {

    // ============================================================
    // 构造
    // ============================================================

    AnimationInstance::AnimationInstance(std::shared_ptr<AnimationResource> resource)
        : m_Resource(std::move(resource)) {

        if (m_Resource && m_Resource->skeleton) {
            size_t boneCount = m_Resource->skeleton->GetBoneCount();
            m_LocalPose.Resize(boneCount);
            m_GlobalPose.Resize(boneCount);
            m_PerBoneWeights = std::make_shared<BlendMask>();
            m_PerBoneWeights->Resize(boneCount);
            m_PerBoneWeights->SetAll(1.0f);
            m_PoseEval = std::make_unique<AnimationPose>(m_Resource->skeleton);
        }
    }

    void AnimationInstance::SetResource(std::shared_ptr<AnimationResource> resource) {
        m_Resource = std::move(resource);
        if (m_Resource && m_Resource->skeleton) {
            size_t boneCount = m_Resource->skeleton->GetBoneCount();
            m_LocalPose.Resize(boneCount);
            m_GlobalPose.Resize(boneCount);
            EnsurePerBoneWeights();
            m_PoseEval = std::make_unique<AnimationPose>(m_Resource->skeleton);
        }
    }

    // ============================================================
    // 片段状态
    // ============================================================

    void AnimationInstance::PlayClip(const std::string& clipName, AnimationLoopMode loop) {
        // 检查资源中是否有此片段
        if (!m_Resource || !m_Resource->GetClip(clipName)) return;

        // 查找是否已有此片段的 ClipState
        for (auto& cs : m_ClipStates) {
            if (cs.clipName == clipName) {
                cs.state = TimelineState::Playing;
                cs.loop = loop;
                cs.localTime = 0.0f;
                return;
            }
        }

        // 创建新的 ClipState
        ClipState cs(clipName);
        cs.state = TimelineState::Playing;
        cs.loop = loop;
        m_ClipStates.push_back(cs);
    }

    ClipState* AnimationInstance::FindClipState(const std::string& name) {
        for (auto& cs : m_ClipStates) {
            if (cs.clipName == name) return &cs;
        }
        return nullptr;
    }

    const ClipState* AnimationInstance::FindClipState(const std::string& name) const {
        for (const auto& cs : m_ClipStates) {
            if (cs.clipName == name) return &cs;
        }
        return nullptr;
    }

    // ============================================================
    // PerBoneWeights
    // ============================================================

    void AnimationInstance::EnsurePerBoneWeights() {
        if (!m_Resource || !m_Resource->skeleton) return;
        size_t boneCount = m_Resource->skeleton->GetBoneCount();
        if (!m_PerBoneWeights || m_PerBoneWeights->GetSize() != boneCount) {
            m_PerBoneWeights = std::make_shared<BlendMask>();
            m_PerBoneWeights->Resize(boneCount);
            m_PerBoneWeights->SetAll(1.0f);
        }
    }

    // ============================================================
    // Update
    // ============================================================

    void AnimationInstance::UpdateTimelines(float32 dt) {
        for (auto& cs : m_ClipStates) {
            if (cs.state != TimelineState::Playing) continue;

            // 推进时间
            cs.localTime += dt * cs.timeScale;

            // 获取片段时长
            auto clip = m_Resource ? m_Resource->GetClip(cs.clipName) : nullptr;
            float32 duration = clip ? clip->GetDuration() : 0.0f;

            if (duration <= 0.0f) continue;

            // 循环处理
            switch (cs.loop) {
                case AnimationLoopMode::Once:
                    if (cs.localTime >= duration) {
                        cs.localTime = duration;
                        cs.state = TimelineState::Stopped;
                    }
                    break;
                case AnimationLoopMode::Loop:
                    if (cs.localTime >= duration) {
                        cs.localTime = std::fmod(cs.localTime, duration);
                    }
                    break;
                case AnimationLoopMode::PingPong: {
                    float32 cycleTime = std::fmod(cs.localTime, duration * 2.0f);
                    if (cycleTime < duration)
                        cs.localTime = cycleTime;
                    else
                        cs.localTime = duration * 2.0f - cycleTime;
                    break;
                }
            }
        }
    }

    void AnimationInstance::Update(float32 dt) {
        UpdateTimelines(dt);
        ExtractPoses();
        BlendPoses();
        if (m_ConstraintsEnabled) {
            ApplyConstraints();
        }
        GenerateGlobalPose();
        GenerateMatrixPalette();
    }

    // ============================================================
    // ExtractPoses — 从片段解压局部姿势
    // ============================================================

    void AnimationInstance::ExtractPoses() {
        if (!m_Resource || !m_Resource->skeleton) return;

        size_t boneCount = m_Resource->skeleton->GetBoneCount();
        if (m_LocalPose.GetBoneCount() != boneCount) {
            m_LocalPose.Resize(boneCount);
        }

        // 如果有重定目标源，从源提取
        if (m_RetargetSource) {
            // 从源实例提取姿势（递归确保源已更新）
            m_RetargetSource->ExtractPoses();
            const auto& srcPose = m_RetargetSource->GetLocalPose();

            // 应用重定目标
            AnimationRetarget retarget;
            retarget.SetSourceSkeleton(m_RetargetSource->GetResource()->skeleton.get());
            retarget.SetTargetSkeleton(m_Resource->skeleton.get());
            retarget.AutoMapByName();
            retarget.RetargetPose(*m_Resource->skeleton, srcPose);

            // 再读回到 m_LocalPose
            for (size_t i = 0; i < boneCount; ++i) {
                const Bone& bone = m_Resource->skeleton->GetBone(static_cast<int32>(i));
                m_LocalPose.translations[i] = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
                m_LocalPose.rotations[i]    = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
                m_LocalPose.scales[i]       = AnimationBlend::DecomposeScale(bone.localPoseMatrix);
            }
            return;
        }

        // 默认：从 BlendSpec 的第一个层提取姿势
        if (!m_BlendSpec.IsEmpty()) {
            const auto& layer = m_BlendSpec.layers[0];
            auto clip = m_Resource->GetClip(layer.clipName);

            if (clip) {
                // 查找此片段的 ClipState
                ClipState* cs = FindClipState(layer.clipName);
                float32 sampleTime = cs ? cs->localTime : 0.0f;

                // Seek 到指定时间并求值
                float32 savedTime = clip->GetLocalTime();
                clip->Seek(sampleTime);
                m_PoseEval->EvaluateFromTimeline(*clip);

                for (size_t i = 0; i < boneCount; ++i) {
                    const Bone& bone = m_Resource->skeleton->GetBone(static_cast<int32>(i));
                    m_LocalPose.translations[i] = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
                    m_LocalPose.rotations[i]    = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
                    m_LocalPose.scales[i]       = AnimationBlend::DecomposeScale(bone.localPoseMatrix);
                }

                clip->Seek(savedTime);
                return;
            }
        }

        // 无有效片段：使用绑定姿势
        for (size_t i = 0; i < boneCount; ++i) {
            const Bone& bone = m_Resource->skeleton->GetBone(static_cast<int32>(i));
            m_LocalPose.translations[i] = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
            m_LocalPose.rotations[i]    = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
            m_LocalPose.scales[i]       = AnimationBlend::DecomposeScale(bone.localPoseMatrix);
        }
    }

    // ============================================================
    // BlendPoses — 混合多个片段
    // ============================================================

    void AnimationInstance::BlendPoses() {
        if (!m_Resource || m_BlendSpec.GetLayerCount() <= 1)
            return;  // 无需混合

        size_t boneCount = m_LocalPose.GetBoneCount();
        if (boneCount == 0) return;

        // 从第二个层开始累加混合
        for (size_t layerIdx = 1; layerIdx < m_BlendSpec.GetLayerCount(); ++layerIdx) {
            const auto& layer = m_BlendSpec.layers[layerIdx];
            auto clip = m_Resource->GetClip(layer.clipName);
            if (!clip) continue;

            // 提取该层的姿势
            ClipState* cs = FindClipState(layer.clipName);
            float32 sampleTime = cs ? cs->localTime : 0.0f;

            float32 savedTime = clip->GetLocalTime();
            clip->Seek(sampleTime);
            m_PoseEval->EvaluateFromTimeline(*clip);

            if (layer.additive) {
                // ── 加法混合 ──
                for (size_t i = 0; i < boneCount; ++i) {
                    const Bone& bone = m_Resource->skeleton->GetBone(static_cast<int32>(i));
                    float32 w = layer.weight;
                    if (layer.mask) w *= layer.mask->GetBoneWeight(static_cast<int32>(i));

                    Vec3 addT = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
                    Quat addR = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
                    Vec3 addS = AnimationBlend::DecomposeScale(bone.localPoseMatrix);

                    // 参考姿势（绑定姿势）
                    Vec3 refT = m_LocalPose.translations[i];  // 简化：用当前值近似
                    Quat refR = m_LocalPose.rotations[i];
                    Vec3 refS = m_LocalPose.scales[i];

                    m_LocalPose.translations[i].x += (addT.x - refT.x) * w;
                    m_LocalPose.translations[i].y += (addT.y - refT.y) * w;
                    m_LocalPose.translations[i].z += (addT.z - refT.z) * w;

                    m_LocalPose.rotations[i] = AnimationBlend::SLERP(
                        m_LocalPose.rotations[i], addR, w);

                    m_LocalPose.scales[i].x += (addS.x - refS.x) * w;
                    m_LocalPose.scales[i].y += (addS.y - refS.y) * w;
                    m_LocalPose.scales[i].z += (addS.z - refS.z) * w;
                }
            } else {
                // ── LERP/SLERP 混合 ──
                for (size_t i = 0; i < boneCount; ++i) {
                    const Bone& bone = m_Resource->skeleton->GetBone(static_cast<int32>(i));
                    float32 w = layer.weight;
                    if (layer.mask) w *= layer.mask->GetBoneWeight(static_cast<int32>(i));

                    Vec3 blendT = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
                    Quat blendR = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
                    Vec3 blendS = AnimationBlend::DecomposeScale(bone.localPoseMatrix);

                    m_LocalPose.translations[i] = AnimationBlend::LERP(
                        m_LocalPose.translations[i], blendT, w);
                    m_LocalPose.rotations[i] = AnimationBlend::SLERP(
                        m_LocalPose.rotations[i], blendR, w);
                    m_LocalPose.scales[i] = AnimationBlend::LERP(
                        m_LocalPose.scales[i], blendS, w);
                }
            }

            clip->Seek(savedTime);
        }
    }

    // ============================================================
    // ApplyConstraints — 应用动画约束
    // ============================================================

    void AnimationInstance::ApplyConstraints() {
        if (!m_Resource || !m_Resource->skeleton) return;

        Skeleton& skeleton = *m_Resource->skeleton;

        // 先将当前局部姿势写入骨骼（约束可能需要读取骨骼世界位置）
        size_t boneCount = m_LocalPose.GetBoneCount();
        for (size_t i = 0; i < boneCount; ++i) {
            Bone& bone = skeleton.GetBone(static_cast<int32>(i));
            Vec3 euler = AnimationBlend::ToEuler(m_LocalPose.rotations[i]);
            AnimationPose::ComposeMatrix(
                m_LocalPose.translations[i], euler, m_LocalPose.scales[i],
                bone.localPoseMatrix);
        }

        // 更新世界的姿势（约束如 LookAt 需要骨骼的世界空间位置）
        skeleton.UpdateWorldPoses();

        // 更新约束求解器的定位器
        m_ConstraintSolver.UpdateLocators(&skeleton);

        // 求值所有约束，直接修改 m_LocalPose
        m_ConstraintSolver.EvaluateAll(skeleton, m_LocalPose);
    }

    // ============================================================
    // GenerateGlobalPose — 生成全局姿势
    // ============================================================

    void AnimationInstance::GenerateGlobalPose() {
        if (!m_Resource || !m_Resource->skeleton) return;

        Skeleton& skeleton = *m_Resource->skeleton;
        size_t boneCount = skeleton.GetBoneCount();

        // 将 LocalPose 写入骨骼
        for (size_t i = 0; i < boneCount; ++i) {
            Bone& bone = skeleton.GetBone(static_cast<int32>(i));
            Vec3 euler = AnimationBlend::ToEuler(m_LocalPose.rotations[i]);
            AnimationPose::ComposeMatrix(
                m_LocalPose.translations[i], euler, m_LocalPose.scales[i],
                bone.localPoseMatrix);
        }

        // 递归计算世界矩阵
        skeleton.UpdateWorldPoses();

        // 提取全局姿势到 GlobalPoseData
        if (m_GlobalPose.GetBoneCount() != boneCount) {
            m_GlobalPose.Resize(boneCount);
        }

        for (size_t i = 0; i < boneCount; ++i) {
            const Bone& bone = skeleton.GetBone(static_cast<int32>(i));

            // 从 currentPoseMatrix 提取全局 T/R/S
            m_GlobalPose.translations[i] = AnimationBlend::DecomposeTranslation(bone.currentPoseMatrix);
            m_GlobalPose.rotations[i]    = AnimationBlend::DecomposeRotation(bone.currentPoseMatrix);
            m_GlobalPose.scales[i]       = AnimationBlend::DecomposeScale(bone.currentPoseMatrix);
        }
    }

    // ============================================================
    // GenerateMatrixPalette — 生成蒙皮矩阵调色板
    // ============================================================

    void AnimationInstance::GenerateMatrixPalette() {
        if (!m_Resource || !m_Resource->skeleton) return;

        // 从 Skeleton 获取已计算的蒙皮矩阵
        m_MatrixPalette = m_Resource->skeleton->GetSkinningMatrices();

        // 矩阵调色板已可上传至 GPU
    }

} // namespace Engine
