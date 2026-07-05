/**
 * @file AnimationRetarget.cpp
 * @brief 动画重定目标实现
 */

#define GLM_ENABLE_EXPERIMENTAL

#include "Engine/Animation/AnimationRetarget.h"
#include "Engine/Animation/AnimationPipeline.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cstring>
#include <cmath>

namespace Engine {

    // ============================================================
    // 映射管理
    // ============================================================

    void AnimationRetarget::AddMapping(const std::string& sourceBone,
                                        const std::string& targetBone,
                                        float32 scaleFactor) {
        // 检查是否已存在
        for (auto& m : m_Mappings) {
            if (m.sourceBone == sourceBone && m.targetBone == targetBone) {
                m.scaleFactor = scaleFactor;
                return;
            }
        }

        m_Mappings.emplace_back(sourceBone, targetBone, scaleFactor);

        // 更新缓存索引
        if (m_SourceSkel && m_TargetSkel) {
            m_SourceIndices.push_back(m_SourceSkel->FindBoneIndex(sourceBone));
            m_TargetIndices.push_back(m_TargetSkel->FindBoneIndex(targetBone));
        }
    }

    void AnimationRetarget::AutoMapByName() {
        if (!m_SourceSkel || !m_TargetSkel) return;

        m_Mappings.clear();
        m_SourceIndices.clear();
        m_TargetIndices.clear();

        // 对源骨骼中的每个骨骼，在目标中查找同名
        for (int32 i = 0; i < static_cast<int32>(m_SourceSkel->GetBoneCount()); ++i) {
            const std::string& name = m_SourceSkel->GetBone(i).name;
            int32 targetIdx = m_TargetSkel->FindBoneIndex(name);
            if (targetIdx >= 0) {
                m_Mappings.emplace_back(name, name, 0.0f);  // scale=0 表示自动
                m_SourceIndices.push_back(i);
                m_TargetIndices.push_back(targetIdx);
            }
        }
    }

    void AnimationRetarget::ClearMappings() {
        m_Mappings.clear();
        m_SourceIndices.clear();
        m_TargetIndices.clear();
    }

    // ============================================================
    // 工具
    // ============================================================

    float32 AnimationRetarget::CalculateBoneScale(const std::string& sourceName,
                                                   const std::string& targetName) const {
        if (!m_SourceSkel || !m_TargetSkel) return 1.0f;

        int32 srcIdx = m_SourceSkel->FindBoneIndex(sourceName);
        int32 tgtIdx = m_TargetSkel->FindBoneIndex(targetName);
        if (srcIdx < 0 || tgtIdx < 0) return 1.0f;

        // 计算骨骼长度（从 bindMatrix 提取平移作为近似长度）
        const Mat4& srcBind = m_SourceSkel->GetBone(srcIdx).inverseBindMatrix;
        const Mat4& tgtBind = m_TargetSkel->GetBone(tgtIdx).inverseBindMatrix;

        float32 srcLen = std::sqrt(srcBind.data[12] * srcBind.data[12]
                                  + srcBind.data[13] * srcBind.data[13]
                                  + srcBind.data[14] * srcBind.data[14]);
        float32 tgtLen = std::sqrt(tgtBind.data[12] * tgtBind.data[12]
                                  + tgtBind.data[13] * tgtBind.data[13]
                                  + tgtBind.data[14] * tgtBind.data[14]);

        return (srcLen > 1e-8f) ? (tgtLen / srcLen) : 1.0f;
    }

    // ============================================================
    // 核心重定目标
    // ============================================================

    void AnimationRetarget::RetargetPose(Skeleton& outTarget,
                                          const PoseLocalData& sourcePose) const {
        if (!HasMappings()) return;

        size_t mappingCount = m_Mappings.size();

        for (size_t i = 0; i < mappingCount; ++i) {
            int32 srcIdx = m_SourceIndices[i];
            int32 tgtIdx = m_TargetIndices[i];
            if (srcIdx < 0 || tgtIdx < 0) continue;

            // 确保索引在有效范围内
            if (static_cast<size_t>(srcIdx) >= sourcePose.GetBoneCount()) continue;
            if (tgtIdx >= static_cast<int32>(outTarget.GetBoneCount())) continue;

            // 获取源姿势的 T/R/S
            Vec3 srcT = sourcePose.translations[srcIdx];
            Quat srcR = sourcePose.rotations[srcIdx];
            Vec3 srcS = sourcePose.scales[srcIdx];

            // 计算缩放因子
            float32 scale = m_Mappings[i].scaleFactor;
            if (scale <= 0.0f) {
                scale = CalculateBoneScale(m_Mappings[i].sourceBone,
                                           m_Mappings[i].targetBone);
            }

            // ── 平移缩放 ──
            Vec3 finalT = {
                srcT.x * scale,
                srcT.y * scale,
                srcT.z * scale
            };

            // ── 旋转补偿 ──
            // 如果源和目标绑定姿势不同，需要补偿
            // 简化处理：直接使用源旋转
            Quat finalR = srcR;

            // ── 缩放 ──
            Vec3 finalS = srcS;

            // 写入目标骨骼 localPoseMatrix
            Bone& targetBone = outTarget.GetBone(tgtIdx);
            Vec3 euler = AnimationBlend::ToEuler(finalR);
            AnimationPose::ComposeMatrix(finalT, euler, finalS,
                                         targetBone.localPoseMatrix);
        }
    }

    void AnimationRetarget::RetargetSkeleton(Skeleton& outTarget,
                                              const Skeleton& sourceSkeleton) const {
        if (!HasMappings()) return;

        // 从源骨骼提取 PoseLocalData
        size_t srcBoneCount = sourceSkeleton.GetBoneCount();
        PoseLocalData srcPose;
        srcPose.Resize(srcBoneCount);

        for (size_t i = 0; i < srcBoneCount; ++i) {
            const Bone& bone = sourceSkeleton.GetBone(static_cast<int32>(i));
            srcPose.translations[i] = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
            srcPose.rotations[i]    = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
            srcPose.scales[i]       = AnimationBlend::DecomposeScale(bone.localPoseMatrix);
        }

        // 执行重定目标
        RetargetPose(outTarget, srcPose);
    }

} // namespace Engine
