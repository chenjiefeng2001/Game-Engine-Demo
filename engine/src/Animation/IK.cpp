/**
 * @file IK.cpp
 * @brief 逆运动学实现 — CCD (Cyclic Coordinate Descent) 求解器
 */

#define GLM_ENABLE_EXPERIMENTAL

#include "Engine/Animation/IK.h"
#include "Engine/Animation/AnimationBlend.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstring>

namespace Engine {

    // ════════════════════════════════════════════
    // IKChain
    // ════════════════════════════════════════════

    void IKChain::BuildFromSkeleton(const Skeleton& skeleton) {
        m_BoneIndices.clear();
        m_Constraints.clear();

        if (m_EndBone < 0 || m_StartBone < 0) return;

        // 从末端骨骼向上遍历到起始骨骼
        int32 current = m_EndBone;
        while (current >= 0 && current != m_StartBone) {
            m_BoneIndices.push_back(current);
            const Bone& bone = skeleton.GetBone(current);
            current = bone.parentIndex;
            if (current < 0) break;  // 到达根
        }
        // 添加起始骨骼
        if (current == m_StartBone) {
            m_BoneIndices.push_back(m_StartBone);
        }

        // 反转使索引从起始到末端排列
        std::reverse(m_BoneIndices.begin(), m_BoneIndices.end());

        // 初始化约束列表
        m_Constraints.resize(m_BoneIndices.size());
    }

    void IKChain::SetConstraint(int32 chainIndex, const IKJointConstraint& constraint) {
        if (chainIndex >= 0 && chainIndex < static_cast<int32>(m_Constraints.size())) {
            m_Constraints[chainIndex] = constraint;
        }
    }

    const IKJointConstraint& IKChain::GetConstraint(int32 chainIndex) const {
        return m_Constraints[chainIndex];
    }

    // ════════════════════════════════════════════
    // 辅助函数
    // ════════════════════════════════════════════

    Vec3 IKSolver::GetBoneWorldPosition(const Skeleton& skeleton, int32 boneIndex) {
        const Mat4& worldMat = skeleton.GetBone(boneIndex).currentPoseMatrix;
        return Vec3(worldMat.data[12], worldMat.data[13], worldMat.data[14]);
    }

    void IKSolver::RotateBoneWorld(Skeleton& skeleton, int32 boneIndex,
                                    const Vec3& axis, float32 angle,
                                    float32 weight) {
        if (weight <= 0.0f || std::abs(angle) < 1e-8f) return;

        Bone& bone = skeleton.GetBone(boneIndex);
        Mat4& localMat = bone.localPoseMatrix;

        // 将局部矩阵分解为 T/R/S
        Vec3 trans = AnimationBlend::DecomposeTranslation(localMat);
        Quat rot   = AnimationBlend::DecomposeRotation(localMat);
        Vec3 scale = AnimationBlend::DecomposeScale(localMat);

        // 将局部旋转转换为 glm
        glm::quat gLocalRot(rot.w, rot.x, rot.y, rot.z);

        // 当前骨骼的世界矩阵中提取的旋转（用于构建世界空间旋转）
        const Mat4& worldMat = bone.currentPoseMatrix;
        glm::mat4 gWorld = glm::make_mat4(worldMat.Data());
        glm::quat gWorldRot = glm::quat_cast(gWorld);

        // 构建世界空间增量旋转
        glm::vec3 gAxis(axis.x, axis.y, axis.z);
        float32 clampedAngle = angle * weight;
        if (std::abs(clampedAngle) < 1e-8f) return;
        glm::quat gDeltaWorld = glm::angleAxis(clampedAngle, gAxis);

        // 将世界空间旋转转换到局部空间:
        // localDelta = worldRot^{-1} * deltaWorld * worldRot * localRot^{-1} ... 
        // 更直接的方法：newLocalRot = localRot * (worldRot^{-1} * deltaWorld * worldRot)
        // 实际上：我们希望 worldRot_new = deltaWorld * worldRot
        // 因为 worldRot = parentWorld * localRot
        // 所以 localRot_new = localRot * (worldRot^{-1} * deltaWorld * worldRot)
        glm::quat gDeltaLocal = glm::inverse(gWorldRot) * gDeltaWorld * gWorldRot;
        glm::quat gNewLocalRot = gDeltaLocal * gLocalRot;

        // 归一化
        gNewLocalRot = glm::normalize(gNewLocalRot);

        // 重组局部矩阵
        Quat newRot(gNewLocalRot.x, gNewLocalRot.y, gNewLocalRot.z, gNewLocalRot.w);
        AnimationPose::ComposeMatrix(trans, AnimationBlend::ToEuler(newRot), scale,
                                     localMat);
    }

    // ════════════════════════════════════════════
    // CCD IK 求解
    // ════════════════════════════════════════════

    float32 IKSolver::SolveCCD(Skeleton& skeleton,
                                const IKChain& chain,
                                const Vec3& target) {
        const auto& boneIndices = chain.GetBoneIndices();
        size_t chainLen = boneIndices.size();
        if (chainLen < 2) return 0.0f;

        // 末端执行器骨骼索引
        int32 effectorIdx = boneIndices.back();

        for (int32 iter = 0; iter < m_MaxIterations; ++iter) {
            // 从末端前一个骨骼开始，向根方向遍历
            // 注意：我们不对末端骨骼本身施加旋转（它通常没有子骨骼来"旋转朝向"）
            for (int32 ci = static_cast<int32>(chainLen) - 2; ci >= 0; --ci) {
                int32 jointIdx = boneIndices[ci];
                Vec3 jointPos = GetBoneWorldPosition(skeleton, jointIdx);
                Vec3 effectorPos = GetBoneWorldPosition(skeleton, effectorIdx);

                // 从关节到末端执行器的向量
                Vec3 toEffector = {
                    effectorPos.x - jointPos.x,
                    effectorPos.y - jointPos.y,
                    effectorPos.z - jointPos.z
                };

                // 从关节到目标的向量
                Vec3 toTarget = {
                    target.x - jointPos.x,
                    target.y - jointPos.y,
                    target.z - jointPos.z
                };

                float32 lenEff = std::sqrt(toEffector.x * toEffector.x +
                                           toEffector.y * toEffector.y +
                                           toEffector.z * toEffector.z);
                float32 lenTgt = std::sqrt(toTarget.x * toTarget.x +
                                           toTarget.y * toTarget.y +
                                           toTarget.z * toTarget.z);

                if (lenEff < 1e-8f || lenTgt < 1e-8f) continue;

                // 归一化
                Vec3 dirEff = {toEffector.x / lenEff, toEffector.y / lenEff, toEffector.z / lenEff};
                Vec3 dirTgt = {toTarget.x / lenTgt, toTarget.y / lenTgt, toTarget.z / lenTgt};

                // 计算旋转轴 = cross(dirEff, dirTgt)
                float32 cx = dirEff.y * dirTgt.z - dirEff.z * dirTgt.y;
                float32 cy = dirEff.z * dirTgt.x - dirEff.x * dirTgt.z;
                float32 cz = dirEff.x * dirTgt.y - dirEff.y * dirTgt.x;
                float32 crossLen = std::sqrt(cx * cx + cy * cy + cz * cz);

                // 计算角度 = acos(dot(dirEff, dirTgt))
                float32 dot = dirEff.x * dirTgt.x + dirEff.y * dirTgt.y + dirEff.z * dirTgt.z;
                dot = std::max(-1.0f, std::min(1.0f, dot));
                float32 angle = std::acos(dot);

                // 如果角度太小则跳过
                if (std::abs(angle) < 0.001f || crossLen < 1e-8f) continue;

                Vec3 axis = {cx / crossLen, cy / crossLen, cz / crossLen};

                // 获取骨骼权重
                float32 boneWeight = 1.0f;
                if (ci < static_cast<int32>(m_PerBoneWeights.size()))
                    boneWeight = m_PerBoneWeights[ci];

                // 应用旋转
                RotateBoneWorld(skeleton, jointIdx, axis, angle, boneWeight);

                // 重新计算世界矩阵（当前骨骼及其子骨骼）
                // 注意：只需要更新从 jointIdx 开始的子树
                // 但简单起见，这里全量更新
                skeleton.UpdateWorldPoses();
            }

            // 检查收敛
            Vec3 finalEffectorPos = GetBoneWorldPosition(skeleton, effectorIdx);
            float32 dx = finalEffectorPos.x - target.x;
            float32 dy = finalEffectorPos.y - target.y;
            float32 dz = finalEffectorPos.z - target.z;
            float32 dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < m_Tolerance) {
                return dist;
            }
        }

        // 最终距离
        Vec3 finalEffectorPos = GetBoneWorldPosition(skeleton, effectorIdx);
        float32 dx = finalEffectorPos.x - target.x;
        float32 dy = finalEffectorPos.y - target.y;
        float32 dz = finalEffectorPos.z - target.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // ════════════════════════════════════════════
    // CCD IK + 末端朝向约束
    // ════════════════════════════════════════════

    float32 IKSolver::SolveCCDWithTargetDir(Skeleton& skeleton,
                                              const IKChain& chain,
                                              const Vec3& target,
                                              const Vec3& targetDir,
                                              float32 dirWeight) {
        // 先做位置 CCD
        float32 posDist = SolveCCD(skeleton, chain, target);

        if (dirWeight <= 0.0f || chain.GetLength() < 2)
            return posDist;

        // 对末端骨骼施加朝向约束
        const auto& boneIndices = chain.GetBoneIndices();
        int32 effectorIdx = boneIndices.back();
        int32 parentIdx = boneIndices[boneIndices.size() - 2];

        Vec3 effectorPos = GetBoneWorldPosition(skeleton, effectorIdx);

        // 计算末端执行器的当前朝向（从父骨骼到末端骨骼的方向）
        Vec3 parentPos = GetBoneWorldPosition(skeleton, parentIdx);
        Vec3 currentDir = {
            effectorPos.x - parentPos.x,
            effectorPos.y - parentPos.y,
            effectorPos.z - parentPos.z
        };
        float32 len = std::sqrt(currentDir.x * currentDir.x +
                                currentDir.y * currentDir.y +
                                currentDir.z * currentDir.z);
        if (len < 1e-8f) return posDist;
        currentDir = {currentDir.x / len, currentDir.y / len, currentDir.z / len};

        // 计算旋转轴和角度
        float32 cx = currentDir.y * targetDir.z - currentDir.z * targetDir.y;
        float32 cy = currentDir.z * targetDir.x - currentDir.x * targetDir.z;
        float32 cz = currentDir.x * targetDir.y - currentDir.y * targetDir.x;
        float32 crossLen = std::sqrt(cx * cx + cy * cy + cz * cz);

        float32 dot = std::max(-1.0f, std::min(1.0f,
            currentDir.x * targetDir.x + currentDir.y * targetDir.y + currentDir.z * targetDir.z));
        float32 angle = std::acos(dot);

        if (std::abs(angle) > 0.001f && crossLen > 1e-8f) {
            Vec3 axis = {cx / crossLen, cy / crossLen, cz / crossLen};
            // 对父骨骼施加朝向旋转（权重为 dirWeight）
            RotateBoneWorld(skeleton, parentIdx, axis, angle, dirWeight);
            skeleton.UpdateWorldPoses();
        }

        return posDist;
    }

    // ════════════════════════════════════════════
    // 求解 + 混合
    // ════════════════════════════════════════════

    float32 IKSolver::SolveAndBlend(Skeleton& skeleton,
                                     const IKChain& chain,
                                     const Vec3& target,
                                     const std::vector<Mat4>& originalPoseSnapshot) {
        // 先求解 IK
        float32 dist = SolveCCD(skeleton, chain, target);

        // 与原始姿势混合
        const auto& boneIndices = chain.GetBoneIndices();
        for (size_t i = 0; i < boneIndices.size(); ++i) {
            int32 idx = boneIndices[i];
            Bone& bone = skeleton.GetBone(idx);

            // 分解 IK 解算后的姿势
            Vec3 ikT = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
            Quat ikR = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
            Vec3 ikS = AnimationBlend::DecomposeScale(bone.localPoseMatrix);

            // 分解原始姿势
            Vec3 origT = AnimationBlend::DecomposeTranslation(originalPoseSnapshot[i]);
            Quat origR = AnimationBlend::DecomposeRotation(originalPoseSnapshot[i]);
            Vec3 origS = AnimationBlend::DecomposeScale(originalPoseSnapshot[i]);

            // 混合
            Vec3 finalT = AnimationBlend::LERP(origT, ikT, m_BlendWeight);
            Quat finalR = AnimationBlend::SLERP(origR, ikR, m_BlendWeight);
            Vec3 finalS = AnimationBlend::LERP(origS, ikS, m_BlendWeight);

            AnimationPose::ComposeMatrix(finalT, AnimationBlend::ToEuler(finalR), finalS,
                                         bone.localPoseMatrix);
        }

        skeleton.UpdateWorldPoses();
        return dist;
    }

} // namespace Engine
