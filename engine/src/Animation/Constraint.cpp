#include "Engine/Animation/Constraint.h"
#include "Engine/Animation/AnimationBlend.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace Engine {

    // ══════════════════════════════════════════════════════════════
    // 内部工具
    // ══════════════════════════════════════════════════════════════

    // 四元数乘法
    static Quat QuatMultiply(const Quat& a, const Quat& b) {
        return Quat(
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
        );
    }

    // 四元数共轭
    static Quat QuatConjugate(const Quat& q) {
        return Quat(-q.x, -q.y, -q.z, q.w);
    }

    // 四元数旋转向量
    static Vec3 QuatRotateVector(const Quat& q, const Vec3& v) {
        Quat p(v.x, v.y, v.z, 0.0f);
        Quat qConj = QuatConjugate(q);
        Quat result = QuatMultiply(QuatMultiply(q, p), qConj);
        return Vec3(result.x, result.y, result.z);
    }

    // 向量归一化
    static Vec3 Vec3Normalize(const Vec3& v) {
        float32 len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (len < 1e-8f) return Vec3(0, 0, 0);
        float32 inv = 1.0f / len;
        return Vec3(v.x * inv, v.y * inv, v.z * inv);
    }

    // 向量点积
    static float32 Vec3Dot(const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    // 向量叉积
    static Vec3 Vec3Cross(const Vec3& a, const Vec3& b) {
        return Vec3(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        );
    }

    // 向量差
    static Vec3 Vec3Sub(const Vec3& a, const Vec3& b) {
        return Vec3(a.x - b.x, a.y - b.y, a.z - b.z);
    }

    // 向量缩放
    static Vec3 Vec3Scale(const Vec3& v, float32 s) {
        return Vec3(v.x * s, v.y * s, v.z * s);
    }

    // 向量加法
    static Vec3 Vec3Add(const Vec3& a, const Vec3& b) {
        return Vec3(a.x + b.x, a.y + b.y, a.z + b.z);
    }

    // 从 Mat4 提取缩放
    static Vec3 ExtractScaleFromMat4(const Mat4& m) {
        Vec3 s;
        s.x = std::sqrt(m.data[0] * m.data[0] + m.data[1] * m.data[1] + m.data[2] * m.data[2]);
        s.y = std::sqrt(m.data[4] * m.data[4] + m.data[5] * m.data[5] + m.data[6] * m.data[6]);
        s.z = std::sqrt(m.data[8] * m.data[8] + m.data[9] * m.data[9] + m.data[10] * m.data[10]);
        return s;
    }

    // 从轴+角度构造四元数
    static Quat QuatFromAxisAngle(const Vec3& axis, float32 angle) {
        float32 half = angle * 0.5f;
        float32 s = std::sin(half);
        return Quat(axis.x * s, axis.y * s, axis.z * s, std::cos(half));
    }

    // ══════════════════════════════════════════════════════════════
    // IConstraint 实现
    // ══════════════════════════════════════════════════════════════

    void IConstraint::ResolveBoneNames(const Skeleton& skeleton) {
        for (auto& target : m_Targets) {
            if (target.type == ConstraintTarget::Type::Bone &&
                target.boneIndex < 0 && !target.locatorName.empty()) {
                target.boneIndex = skeleton.FindBoneIndex(target.locatorName);
                if (target.boneIndex < 0) {
                    // 骨骼名称未找到，回退到世界空间
                    target.type = ConstraintTarget::Type::WorldSpace;
                }
            }
        }
    }

    void IConstraint::ResolveTargetTransform(
        const ConstraintTarget& target,
        const Skeleton& skeleton,
        const std::vector<Locator>& locators,
        Vec3& outPos, Quat& outRot, Vec3& outScale)
    {
        outPos = Vec3(0, 0, 0);
        outRot = Quat::Identity();
        outScale = Vec3(1, 1, 1);

        switch (target.type) {
            case ConstraintTarget::Type::Locator: {
                // 在定位器列表中查找
                auto it = std::find_if(locators.begin(), locators.end(),
                    [&](const Locator& l) { return l.name == target.locatorName; });
                if (it != locators.end()) {
                    outPos = it->worldPosition;
                    outRot = it->worldRotation;
                    outScale = it->worldScale;
                }
                break;
            }

            case ConstraintTarget::Type::Bone: {
                if (target.boneIndex >= 0 &&
                    target.boneIndex < static_cast<int32>(skeleton.GetBoneCount())) {
                    const Bone& bone = skeleton.GetBone(target.boneIndex);
                    const Mat4& m = bone.currentPoseMatrix;
                    outPos = Vec3(m.data[12], m.data[13], m.data[14]);
                    outRot = AnimationBlend::FromMatrix(m);
                    outScale = ExtractScaleFromMat4(m);
                }
                break;
            }

            case ConstraintTarget::Type::WorldSpace:
            default:
                // 使用目标自带的偏移量（作为世界空间变换）
                outPos = target.positionOffset;
                outRot = target.rotationOffset;
                outScale = target.scaleOffset;
                break;
        }

        // 应用目标偏移 — 将偏移叠加到目标变换上
        if (target.type != ConstraintTarget::Type::WorldSpace) {
            // 位置偏移在目标局部空间中旋转后加上去
            Vec3 rotatedOff = QuatRotateVector(outRot, target.positionOffset);
            outPos = Vec3Add(outPos, rotatedOff);
            outRot = QuatMultiply(outRot, target.rotationOffset);
            outScale = Vec3(
                outScale.x * target.scaleOffset.x,
                outScale.y * target.scaleOffset.y,
                outScale.z * target.scaleOffset.z
            );
        }
    }

    Vec3 IConstraint::ComputeWeightedPosition(
        const std::vector<ConstraintTarget>& targets,
        const Skeleton& skeleton,
        const std::vector<Locator>& locators)
    {
        Vec3 result(0, 0, 0);
        float32 totalWeight = 0.0f;

        for (const auto& target : targets) {
            Vec3 pos, scale;
            Quat rot;
            ResolveTargetTransform(target, skeleton, locators, pos, rot, scale);
            result = Vec3Add(result, Vec3Scale(pos, target.weight));
            totalWeight += target.weight;
        }

        if (totalWeight > 1e-8f) {
            float32 invW = 1.0f / totalWeight;
            result = Vec3Scale(result, invW);
        }

        return result;
    }

    Quat IConstraint::ComputeWeightedRotation(
        const std::vector<ConstraintTarget>& targets,
        const Skeleton& skeleton,
        const std::vector<Locator>& locators)
    {
        // 收集所有有效旋转
        struct RotEntry {
            Quat rot;
            float32 weight;
        };
        std::vector<RotEntry> entries;

        for (const auto& target : targets) {
            if (target.weight < 1e-8f) continue;
            Vec3 pos, scale;
            Quat rot;
            ResolveTargetTransform(target, skeleton, locators, pos, rot, scale);
            entries.push_back({rot, target.weight});
        }

        if (entries.empty()) return Quat::Identity();
        if (entries.size() == 1) return entries[0].rot;

        // 使用 SLERP 链：从权重最大的开始依次混合
        // 找到权重最大的作为起点
        size_t pivot = 0;
        float32 maxW = entries[0].weight;
        for (size_t i = 1; i < entries.size(); ++i) {
            if (entries[i].weight > maxW) {
                maxW = entries[i].weight;
                pivot = i;
            }
        }

        Quat result = entries[pivot].rot;
        float32 accumWeight = entries[pivot].weight;

        for (size_t i = 0; i < entries.size(); ++i) {
            if (i == pivot) continue;
            float32 t = entries[i].weight / (accumWeight + entries[i].weight);
            result = AnimationBlend::SLERP(result, entries[i].rot, t);
            accumWeight += entries[i].weight;
        }

        return result;
    }

    void IConstraint::DecomposeBoneMatrix(const Bone& bone,
                                          Vec3& pos, Quat& rot, Vec3& scale)
    {
        const Mat4& m = bone.localPoseMatrix;
        pos = Vec3(m.data[12], m.data[13], m.data[14]);
        rot = AnimationBlend::FromMatrix(m);
        scale = ExtractScaleFromMat4(m);
    }

    void IConstraint::ComposeBoneMatrix(Bone& bone,
                                        const Vec3& pos, const Quat& rot, const Vec3& scale)
    {
        Mat4 m = AnimationBlend::ToMatrix(rot);

        // 应用缩放
        m.data[0]  *= scale.x;  m.data[1]  *= scale.x;  m.data[2]  *= scale.x;
        m.data[4]  *= scale.y;  m.data[5]  *= scale.y;  m.data[6]  *= scale.y;
        m.data[8]  *= scale.z;  m.data[9]  *= scale.z;  m.data[10] *= scale.z;

        // 应用平移
        m.data[12] = pos.x;
        m.data[13] = pos.y;
        m.data[14] = pos.z;

        bone.localPoseMatrix = m;
    }

    void IConstraint::DecomposeMatrix(const Mat4& m, Vec3& pos, Quat& rot, Vec3& scale) {
        pos = Vec3(m.data[12], m.data[13], m.data[14]);
        rot = AnimationBlend::FromMatrix(m);
        scale = ExtractScaleFromMat4(m);
    }

    Mat4 IConstraint::ComposeMatrix(const Vec3& pos, const Quat& rot, const Vec3& scale) {
        Mat4 m = AnimationBlend::ToMatrix(rot);

        m.data[0]  *= scale.x;  m.data[1]  *= scale.x;  m.data[2]  *= scale.x;
        m.data[4]  *= scale.y;  m.data[5]  *= scale.y;  m.data[6]  *= scale.y;
        m.data[8]  *= scale.z;  m.data[9]  *= scale.z;  m.data[10] *= scale.z;

        m.data[12] = pos.x;
        m.data[13] = pos.y;
        m.data[14] = pos.z;

        return m;
    }

    // ══════════════════════════════════════════════════════════════
    // PointConstraint
    // ══════════════════════════════════════════════════════════════

    void PointConstraint::Evaluate(Skeleton& skeleton,
                                   const std::vector<Locator>& locators,
                                   PoseLocalData& pose)
    {
        if (!m_Enabled || m_AffectedBone < 0) return;
        if (m_Targets.empty()) return;

        Vec3 targetPos = ComputeWeightedPosition(m_Targets, skeleton, locators);

        if (m_AffectedBone >= static_cast<int32>(pose.GetBoneCount())) return;

        // 应用约束到 PoseLocalData
        if (m_MaintainOffset) {
            if (!m_HasOffset) {
                m_OffsetPosition = Vec3Sub(pose.translations[m_AffectedBone], targetPos);
                m_HasOffset = true;
            }
            pose.translations[m_AffectedBone] = Vec3Add(targetPos, m_OffsetPosition);
        } else {
            pose.translations[m_AffectedBone] = AnimationBlend::LERP(
                pose.translations[m_AffectedBone], targetPos, m_Strength);
        }
    }

    // ══════════════════════════════════════════════════════════════
    // OrientConstraint
    // ══════════════════════════════════════════════════════════════

    void OrientConstraint::Evaluate(Skeleton& skeleton,
                                    const std::vector<Locator>& locators,
                                    PoseLocalData& pose)
    {
        if (!m_Enabled || m_AffectedBone < 0) return;
        if (m_Targets.empty()) return;
        if (m_AffectedBone >= static_cast<int32>(pose.GetBoneCount())) return;

        Quat targetRot = ComputeWeightedRotation(m_Targets, skeleton, locators);

        if (m_MaintainOffset) {
            if (!m_HasOffset) {
                // 计算初始偏移: offset = current⁻¹ * target
                Quat current = pose.rotations[m_AffectedBone];
                Quat currentConj = QuatConjugate(current);
                m_OffsetRotation = QuatMultiply(targetRot, currentConj);
                m_HasOffset = true;
            }
            // 应用偏移: result = target * offset
            pose.rotations[m_AffectedBone] = QuatMultiply(targetRot, m_OffsetRotation);
        } else {
            pose.rotations[m_AffectedBone] = AnimationBlend::SLERP(
                pose.rotations[m_AffectedBone], targetRot, m_Strength);
        }
    }

    // ══════════════════════════════════════════════════════════════
    // ScaleConstraint
    // ══════════════════════════════════════════════════════════════

    void ScaleConstraint::Evaluate(Skeleton& skeleton,
                                   const std::vector<Locator>& locators,
                                   PoseLocalData& pose)
    {
        if (!m_Enabled || m_AffectedBone < 0) return;
        if (m_Targets.empty()) return;
        if (m_AffectedBone >= static_cast<int32>(pose.GetBoneCount())) return;

        // 计算加权平均缩放
        Vec3 targetScale(0, 0, 0);
        float32 totalWeight = 0.0f;

        for (const auto& target : m_Targets) {
            Vec3 pos, scale;
            Quat rot;
            ResolveTargetTransform(target, skeleton, locators, pos, rot, scale);
            targetScale = Vec3Add(targetScale, Vec3Scale(scale, target.weight));
            totalWeight += target.weight;
        }

        if (totalWeight > 1e-8f) {
            float32 invW = 1.0f / totalWeight;
            targetScale = Vec3Scale(targetScale, invW);
        } else {
            targetScale = Vec3(1, 1, 1);
        }

        if (m_MaintainOffset) {
            if (!m_HasOffset) {
                m_OffsetScale = Vec3(
                    pose.scales[m_AffectedBone].x / (targetScale.x > 1e-8f ? targetScale.x : 1.0f),
                    pose.scales[m_AffectedBone].y / (targetScale.y > 1e-8f ? targetScale.y : 1.0f),
                    pose.scales[m_AffectedBone].z / (targetScale.z > 1e-8f ? targetScale.z : 1.0f)
                );
                m_HasOffset = true;
            }
            pose.scales[m_AffectedBone] = Vec3(
                targetScale.x * m_OffsetScale.x,
                targetScale.y * m_OffsetScale.y,
                targetScale.z * m_OffsetScale.z
            );
        } else {
            pose.scales[m_AffectedBone] = AnimationBlend::LERP(
                pose.scales[m_AffectedBone], targetScale, m_Strength);
        }
    }

    // ══════════════════════════════════════════════════════════════
    // ParentConstraint
    // ══════════════════════════════════════════════════════════════

    void ParentConstraint::Evaluate(Skeleton& skeleton,
                                    const std::vector<Locator>& locators,
                                    PoseLocalData& pose)
    {
        if (!m_Enabled || m_AffectedBone < 0) return;
        if (m_Targets.empty()) return;
        if (m_AffectedBone >= static_cast<int32>(pose.GetBoneCount())) return;

        // 使用第一个目标的变换
        Vec3 targetPos, targetScale;
        Quat targetRot;
        ResolveTargetTransform(m_Targets[0], skeleton, locators,
                               targetPos, targetRot, targetScale);

        // 获取当前骨骼变换
        Vec3 currentPos = pose.translations[m_AffectedBone];
        Quat currentRot = pose.rotations[m_AffectedBone];
        Vec3 currentScale = pose.scales[m_AffectedBone];

        if (m_MaintainOffset && !m_HasOffset) {
            // 计算初始偏移：骨骼相对于目标的变换
            // offset = target⁻¹ * current
            Quat targetConj = QuatConjugate(targetRot);
            Vec3 diff = Vec3Sub(currentPos, targetPos);
            Vec3 rotatedDiff = QuatRotateVector(targetConj, diff);

            m_OffsetPosition = rotatedDiff;
            m_OffsetRotation = QuatMultiply(targetConj, currentRot);
            m_OffsetScale = Vec3(
                currentScale.x / (targetScale.x > 1e-8f ? targetScale.x : 1.0f),
                currentScale.y / (targetScale.y > 1e-8f ? targetScale.y : 1.0f),
                currentScale.z / (targetScale.z > 1e-8f ? targetScale.z : 1.0f)
            );
            m_HasOffset = true;
        }

        // 计算约束结果
        Vec3 resultPos;
        Quat resultRot;
        Vec3 resultScale;

        if (m_MaintainOffset) {
            // result = target * offset
            Vec3 rotatedOff = QuatRotateVector(targetRot, m_OffsetPosition);
            resultPos = Vec3Add(targetPos, rotatedOff);
            resultRot = QuatMultiply(targetRot, m_OffsetRotation);
            resultScale = Vec3(
                targetScale.x * m_OffsetScale.x,
                targetScale.y * m_OffsetScale.y,
                targetScale.z * m_OffsetScale.z
            );
        } else {
            resultPos = targetPos;
            resultRot = targetRot;
            resultScale = targetScale;
        }

        // 逐轴控制
        const ConstraintTarget& target = m_Targets[0];
        if (!target.constrainX || !target.constrainY || !target.constrainZ) {
            if (!target.constrainX) resultPos.x = currentPos.x;
            if (!target.constrainY) resultPos.y = currentPos.y;
            if (!target.constrainZ) resultPos.z = currentPos.z;
        }

        // 应用强度混合
        pose.translations[m_AffectedBone] = AnimationBlend::LERP(currentPos, resultPos, m_Strength);
        pose.rotations[m_AffectedBone] = AnimationBlend::SLERP(currentRot, resultRot, m_Strength);
        pose.scales[m_AffectedBone] = AnimationBlend::LERP(currentScale, resultScale, m_Strength);
    }

    // ══════════════════════════════════════════════════════════════
    // LookAtConstraint
    // ══════════════════════════════════════════════════════════════

    void LookAtConstraint::Evaluate(Skeleton& skeleton,
                                    const std::vector<Locator>& locators,
                                    PoseLocalData& pose)
    {
        if (!m_Enabled || m_AffectedBone < 0) return;
        if (m_Targets.empty()) return;
        if (m_AffectedBone >= static_cast<int32>(pose.GetBoneCount())) return;

        // 计算目标位置（加权平均）
        Vec3 targetPos = ComputeWeightedPosition(m_Targets, skeleton, locators);

        // 获取骨骼的世界空间位置（从 currentPoseMatrix）
        if (m_AffectedBone >= static_cast<int32>(skeleton.GetBoneCount())) return;
        const Bone& bone = skeleton.GetBone(m_AffectedBone);
        Vec3 boneWorldPos(bone.currentPoseMatrix.data[12],
                          bone.currentPoseMatrix.data[13],
                          bone.currentPoseMatrix.data[14]);

        // 计算目标方向
        Vec3 direction = Vec3Sub(targetPos, boneWorldPos);
        float32 len = std::sqrt(direction.x * direction.x +
                                direction.y * direction.y +
                                direction.z * direction.z);
        if (len < 1e-8f) return; // 目标就在骨骼上，不做任何操作

        direction = Vec3Scale(direction, 1.0f / len);

        // 构建注视旋转
        // 使 m_ForwardAxis 指向 direction，m_UpAxis 尽量对齐
        Vec3 forward = Vec3Normalize(m_ForwardAxis);
        Vec3 up = Vec3Normalize(m_UpAxis);

        // 计算右向量
        Vec3 right = Vec3Cross(up, forward);
        float32 rightLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
        if (rightLen < 1e-8f) {
            // forward 和 up 平行，使用备用 up
            up = Vec3(0, 1, 0);
            if (std::abs(Vec3Dot(forward, up)) > 0.999f) {
                up = Vec3(0, 0, 1);
            }
            right = Vec3Cross(up, forward);
            right = Vec3Normalize(right);
        } else {
            right = Vec3Scale(right, 1.0f / rightLen);
        }

        // 重新计算真正的 up
        up = Vec3Cross(forward, right);
        up = Vec3Normalize(up);

        // 构建旋转矩阵的列 (列主序)
        // 目标: forward 轴对齐到 direction
        // 我们需要一个旋转，使 m_ForwardAxis 转到 direction
        // 方法：构建一个从 m_ForwardAxis 到 direction 的旋转

        // 使用轴角方法：旋转 axis = cross(forward, direction), angle = acos(dot(forward, direction))
        Vec3 crossFD = Vec3Cross(forward, direction);
        float32 crossLen = std::sqrt(crossFD.x * crossFD.x + crossFD.y * crossFD.y + crossFD.z * crossFD.z);
        float32 dotFD = Vec3Dot(forward, direction);

        Quat lookRot;
        if (crossLen < 1e-8f) {
            // 方向相同或相反
            if (dotFD > 0.0f) {
                lookRot = Quat::Identity(); // 相同方向，无需旋转
            } else {
                // 相反方向，绕 up 旋转 180 度
                lookRot = QuatFromAxisAngle(up, 3.14159265f);
            }
        } else {
            Vec3 axis = Vec3Scale(crossFD, 1.0f / crossLen);
            float32 angle = std::atan2(crossLen, dotFD);
            lookRot = QuatFromAxisAngle(axis, angle);
        }

        // 应用旋转到当前骨骼局部旋转
        // 注意：我们是在局部空间操作，LookAt 应该修改局部旋转
        // 使骨骼的世界空间朝向指向目标
        // 简单方法：直接设置 pose rotations
        Quat currentRot = pose.rotations[m_AffectedBone];

        if (m_MaintainOffset && !m_HasOffset) {
            // 计算初始偏移
            m_OffsetRotation = QuatMultiply(QuatConjugate(lookRot), currentRot);
            m_HasOffset = true;
        }

        Quat finalRot;
        if (m_MaintainOffset) {
            finalRot = QuatMultiply(lookRot, m_OffsetRotation);
        } else {
            finalRot = lookRot;
        }

        // 强度混合
        pose.rotations[m_AffectedBone] = AnimationBlend::SLERP(currentRot, finalRot, m_Strength);
    }

    // ══════════════════════════════════════════════════════════════
    // AimConstraint
    // ══════════════════════════════════════════════════════════════

    void AimConstraint::Evaluate(Skeleton& skeleton,
                                 const std::vector<Locator>& locators,
                                 PoseLocalData& pose)
    {
        if (!m_Enabled || m_AffectedBone < 0) return;
        if (m_Targets.empty()) return;
        if (m_AffectedBone >= static_cast<int32>(pose.GetBoneCount())) return;

        // 计算目标位置
        Vec3 targetPos = ComputeWeightedPosition(m_Targets, skeleton, locators);

        // 获取骨骼的世界空间位置
        if (m_AffectedBone >= static_cast<int32>(skeleton.GetBoneCount())) return;
        const Bone& bone = skeleton.GetBone(m_AffectedBone);
        Vec3 boneWorldPos(bone.currentPoseMatrix.data[12],
                          bone.currentPoseMatrix.data[13],
                          bone.currentPoseMatrix.data[14]);

        // 计算目标方向
        Vec3 aimDir = Vec3Sub(targetPos, boneWorldPos);
        float32 len = std::sqrt(aimDir.x * aimDir.x + aimDir.y * aimDir.y + aimDir.z * aimDir.z);
        if (len < 1e-8f) return;
        aimDir = Vec3Scale(aimDir, 1.0f / len);

        // Aim 约束：将骨骼的 m_AimAxis 旋转到 aimDir 方向
        Vec3 aimAxis = Vec3Normalize(m_AimAxis);
        Vec3 up = Vec3Normalize(m_UpAxis);

        // 计算从 aimAxis 到 aimDir 的旋转
        Vec3 cross = Vec3Cross(aimAxis, aimDir);
        float32 crossLen = std::sqrt(cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
        float32 dot = Vec3Dot(aimAxis, aimDir);

        Quat aimRot;
        if (crossLen < 1e-8f) {
            if (dot > 0.0f) {
                aimRot = Quat::Identity();
            } else {
                aimRot = QuatFromAxisAngle(up, 3.14159265f);
            }
        } else {
            Vec3 axis = Vec3Scale(cross, 1.0f / crossLen);
            float32 angle = std::atan2(crossLen, dot);
            aimRot = QuatFromAxisAngle(axis, angle);
        }

        Quat currentRot = pose.rotations[m_AffectedBone];

        if (m_MaintainOffset && !m_HasOffset) {
            m_OffsetRotation = QuatMultiply(QuatConjugate(aimRot), currentRot);
            m_HasOffset = true;
        }

        Quat finalRot;
        if (m_MaintainOffset) {
            finalRot = QuatMultiply(aimRot, m_OffsetRotation);
        } else {
            finalRot = aimRot;
        }

        pose.rotations[m_AffectedBone] = AnimationBlend::SLERP(currentRot, finalRot, m_Strength);
    }

} // namespace Engine


