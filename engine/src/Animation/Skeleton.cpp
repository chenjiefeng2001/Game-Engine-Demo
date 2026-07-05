/**
 * @file Skeleton.cpp
 * @brief 骨骼层级实现
 */

#include "Engine/Animation/Skeleton.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

namespace Engine {

    // ============================================================
    // 骨骼构建
    // ============================================================

    int32 Skeleton::AddRootBone(const std::string& name, const Mat4& bindMatrix) {
        // 检查是否已存在同名骨骼
        if (m_NameToIndex.find(name) != m_NameToIndex.end())
            return m_NameToIndex[name];

        int32 index = static_cast<int32>(m_Bones.size());
        m_Bones.emplace_back(name, -1, bindMatrix);
        m_NameToIndex[name] = index;
        m_RootBones.push_back(index);
        return index;
    }

    int32 Skeleton::AddBone(const std::string& name, const std::string& parentName,
                            const Mat4& bindMatrix) {
        // 检查是否已存在同名骨骼
        if (m_NameToIndex.find(name) != m_NameToIndex.end())
            return m_NameToIndex[name];

        auto it = m_NameToIndex.find(parentName);
        if (it == m_NameToIndex.end())
            return -1;  // 父骨骼不存在

        int32 index = static_cast<int32>(m_Bones.size());
        m_Bones.emplace_back(name, it->second, bindMatrix);
        m_NameToIndex[name] = index;
        return index;
    }

    int32 Skeleton::AddBone(const Bone& bone) {
        if (m_NameToIndex.find(bone.name) != m_NameToIndex.end())
            return m_NameToIndex[bone.name];

        int32 index = static_cast<int32>(m_Bones.size());
        m_Bones.push_back(bone);
        m_NameToIndex[bone.name] = index;
        if (bone.parentIndex == -1) {
            m_RootBones.push_back(index);
        }
        return index;
    }

    // ============================================================
    // 查找
    // ============================================================

    int32 Skeleton::FindBoneIndex(const std::string& name) const {
        auto it = m_NameToIndex.find(name);
        return it != m_NameToIndex.end() ? it->second : -1;
    }

    Bone& Skeleton::GetBone(int32 index) {
        return m_Bones[index];
    }

    const Bone& Skeleton::GetBone(int32 index) const {
        return m_Bones[index];
    }

    // ============================================================
    // 姿势更新
    // ============================================================

    void Skeleton::UpdateWorldPoses() {
        // 确保蒙皮矩阵缓存大小与骨骼数量一致
        if (m_SkinningMatrices.size() != m_Bones.size()) {
            m_SkinningMatrices.resize(m_Bones.size());
        }

        // 从每个根骨骼开始递归更新
        for (int32 rootIdx : m_RootBones) {
            Mat4 parentWorld;
            parentWorld.Identity();
            UpdateBoneWorldPose(rootIdx, parentWorld);
        }
    }

    void Skeleton::UpdateBoneWorldPose(int32 boneIndex, const Mat4& parentWorld) {
        if (boneIndex < 0 || boneIndex >= static_cast<int32>(m_Bones.size()))
            return;

        Bone& bone = m_Bones[boneIndex];

        // currentPoseMatrix = parentWorld * localPoseMatrix
        Mat4Multiply(parentWorld, bone.localPoseMatrix, bone.currentPoseMatrix);

        // 计算蒙皮矩阵 = currentPoseMatrix * inverseBindMatrix
        bone.ComputeSkinningMatrix();

        // 更新缓存
        m_SkinningMatrices[boneIndex] = bone.skinningMatrix;

        // 递归更新子骨骼
        for (int32 i = 0; i < static_cast<int32>(m_Bones.size()); ++i) {
            if (m_Bones[i].parentIndex == boneIndex) {
                UpdateBoneWorldPose(i, bone.currentPoseMatrix);
            }
        }
    }

    void Skeleton::ResetToBindPose() {
        for (auto& bone : m_Bones) {
            // 在绑定姿势下，localPoseMatrix 为单位矩阵，
            // 因为 currentPoseMatrix = inverseBindMatrix（即骨骼的绑定世界矩阵）
            // 而 localPoseMatrix = parentWorld^{-1} * currentPoseMatrix
            // 简单地设置 localPoseMatrix = identity，用 bindMatrix 构建
            bone.localPoseMatrix.Identity();
        }
        UpdateWorldPoses();
    }

} // namespace Engine
