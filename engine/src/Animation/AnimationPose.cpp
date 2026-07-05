/**
 * @file AnimationPose.cpp
 * @brief 动画姿势求值器实现
 */

#include "Engine/Animation/AnimationPose.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>

namespace Engine {

    // ============================================================
    // 从动画时间线求值
    // ============================================================

    void AnimationPose::EvaluateFromTimeline(const AnimationLocalTimeline& timeLine) {
        if (!m_Skeleton) return;

        float32 t = timeLine.GetLocalTime();

        // 扫描所有浮点轨道，解析 "BoneName.property" 命名
        // 我们通过已知的骨骼名称来查找对应的轨道
        for (int32 i = 0; i < static_cast<int32>(m_Skeleton->GetBoneCount()); ++i) {
            const Bone& bone = m_Skeleton->GetBone(i);
            const std::string& boneName = bone.name;

            // 查找 position / rotation / scale 轨道
            bool hasPos = false, hasRot = false, hasScale = false;
            Vec3 pos, rot, scale(1.0f, 1.0f, 1.0f);

            const AnimationTrack* posTrack = timeLine.GetFloatTrack(boneName + ".position");
            if (posTrack && !posTrack->IsEmpty()) {
                pos = posTrack->EvaluateVec3(t);
                hasPos = true;
            }

            const AnimationTrack* rotTrack = timeLine.GetFloatTrack(boneName + ".rotation");
            if (rotTrack && !rotTrack->IsEmpty()) {
                rot = rotTrack->EvaluateVec3(t);
                hasRot = true;
            }

            const AnimationTrack* scaleTrack = timeLine.GetFloatTrack(boneName + ".scale");
            if (scaleTrack && !scaleTrack->IsEmpty()) {
                scale = scaleTrack->EvaluateVec3(t);
                hasScale = true;
            }

            if (hasPos || hasRot || hasScale) {
                // 从轨道值构建局部变换矩阵
                SetBoneLocalPose(i, pos, rot, scale);
            }
        }
    }

    // ============================================================
    // 设置骨骼局部姿势
    // ============================================================

    void AnimationPose::SetBoneLocalPose(int32 boneIndex,
                                          const Vec3& position,
                                          const Vec3& rotation,
                                          const Vec3& scale) {
        if (!m_Skeleton) return;
        if (boneIndex < 0 || boneIndex >= static_cast<int32>(m_Skeleton->GetBoneCount()))
            return;

        Mat4 localMatrix;
        ComposeMatrix(position, rotation, scale, localMatrix);
        SetBoneLocalMatrix(boneIndex, localMatrix);
    }

    void AnimationPose::SetBoneLocalMatrix(int32 boneIndex, const Mat4& localMatrix) {
        if (!m_Skeleton) return;
        if (boneIndex < 0 || boneIndex >= static_cast<int32>(m_Skeleton->GetBoneCount()))
            return;

        m_Skeleton->GetBone(boneIndex).localPoseMatrix = localMatrix;
    }

    // ============================================================
    // 便捷方法：求值 + 更新世界矩阵
    // ============================================================

    void AnimationPose::EvaluateAndUpdate(const AnimationLocalTimeline& timeLine) {
        EvaluateFromTimeline(timeLine);
        if (m_Skeleton) {
            m_Skeleton->UpdateWorldPoses();
        }
    }

    // ============================================================
    // 组合矩阵：T * R * S
    // ============================================================

    void AnimationPose::ComposeMatrix(const Vec3& pos, const Vec3& rotEuler,
                                      const Vec3& scale, Mat4& out) {
        // 使用 glm 构造
        glm::mat4 trans = glm::translate(glm::mat4(1.0f),
            glm::vec3(pos.x, pos.y, pos.z));

        // 欧拉角（度）转弧度
        // 欧拉角（度）转弧度，使用 XYZ 顺序（先绕 X，再绕 Y，最后绕 Z）
        float rx = glm::radians(rotEuler.x);
        float ry = glm::radians(rotEuler.y);
        float rz = glm::radians(rotEuler.z);

        // 手动构造旋转矩阵: R = Rz * Ry * Rx
        glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), rx, glm::vec3(1, 0, 0));
        glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), ry, glm::vec3(0, 1, 0));
        glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), rz, glm::vec3(0, 0, 1));
        glm::mat4 rot = rotZ * rotY * rotX;

        glm::mat4 scl = glm::scale(glm::mat4(1.0f),
            glm::vec3(scale.x, scale.y, scale.z));

        glm::mat4 result = trans * rot * scl;

        std::memcpy(out.Data(), glm::value_ptr(result), sizeof(float32) * 16);
    }

} // namespace Engine
