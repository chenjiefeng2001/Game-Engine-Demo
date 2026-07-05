/**
 * @file AnimationBlend.cpp
 * @brief 动画混合实现 — LERP / SLERP / 姿势混合
 */

#define GLM_ENABLE_EXPERIMENTAL

#include "Engine/Animation/AnimationBlend.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <cstring>
#include <cmath>

namespace Engine {

    // ════════════════════════════════════════════
    // LERP — 标量
    // ════════════════════════════════════════════

    float32 AnimationBlend::LERP(float32 a, float32 b, float32 t) {
        return a + t * (b - a);
    }

    // ════════════════════════════════════════════
    // LERP — Vec2
    // ════════════════════════════════════════════

    Vec2 AnimationBlend::LERP(const Vec2& a, const Vec2& b, float32 t) {
        return Vec2(
            a.x + t * (b.x - a.x),
            a.y + t * (b.y - a.y)
        );
    }

    // ════════════════════════════════════════════
    // LERP — Vec3
    // ════════════════════════════════════════════

    Vec3 AnimationBlend::LERP(const Vec3& a, const Vec3& b, float32 t) {
        return Vec3(
            a.x + t * (b.x - a.x),
            a.y + t * (b.y - a.y),
            a.z + t * (b.z - a.z)
        );
    }

    // ════════════════════════════════════════════
    // LERP — Vec4
    // ════════════════════════════════════════════

    Vec4 AnimationBlend::LERP(const Vec4& a, const Vec4& b, float32 t) {
        return Vec4(
            a.x + t * (b.x - a.x),
            a.y + t * (b.y - a.y),
            a.z + t * (b.z - a.z),
            a.w + t * (b.w - a.w)
        );
    }

    // ════════════════════════════════════════════
    // LERP — Mat4（逐元素）
    // ════════════════════════════════════════════

    Mat4 AnimationBlend::LERP(const Mat4& a, const Mat4& b, float32 t) {
        Mat4 result;
        for (int32 i = 0; i < 16; ++i) {
            result.data[i] = a.data[i] + t * (b.data[i] - a.data[i]);
        }
        return result;
    }

    // ════════════════════════════════════════════
    // SLERP — 四元数球面线性插值
    // ════════════════════════════════════════════

    Quat AnimationBlend::SLERP(const Quat& a, const Quat& b, float32 t) {
        // 转换为 glm 四元数
        glm::quat qa(a.w, a.x, a.y, a.z);
        glm::quat qb(b.w, b.x, b.y, b.z);

        // 使用 glm 的 SLERP
        glm::quat result = glm::slerp(qa, qb, t);

        return Quat(result.x, result.y, result.z, result.w);
    }

    // ════════════════════════════════════════════
    // LERPQuat — 四元数快速线性插值（归一化 LERP）
    // ════════════════════════════════════════════

    Quat AnimationBlend::LERPQuat(const Quat& a, const Quat& b, float32 t) {
        // 确保最短路径
        float32 dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        Quat bn = b;
        if (dot < 0.0f) {
            // 取反目标，确保走最短弧
            bn = Quat(-b.x, -b.y, -b.z, -b.w);
        }

        // LERP
        Quat result(
            a.x + t * (bn.x - a.x),
            a.y + t * (bn.y - a.y),
            a.z + t * (bn.z - a.z),
            a.w + t * (bn.w - a.w)
        );

        // 归一化
        float32 len = std::sqrt(result.x * result.x + result.y * result.y
                              + result.z * result.z + result.w * result.w);
        if (len > 0.0f) {
            result.x /= len;
            result.y /= len;
            result.z /= len;
            result.w /= len;
        }

        return result;
    }

    // ════════════════════════════════════════════
    // 四元数 ↔ 欧拉角
    // ════════════════════════════════════════════

    Quat AnimationBlend::FromEuler(const Vec3& eulerDeg) {
        glm::vec3 rad(
            glm::radians(eulerDeg.x),
            glm::radians(eulerDeg.y),
            glm::radians(eulerDeg.z)
        );
        glm::quat q = glm::quat(rad);  // XYZ 顺序
        return Quat(q.x, q.y, q.z, q.w);
    }

    Vec3 AnimationBlend::ToEuler(const Quat& q) {
        glm::quat gq(q.w, q.x, q.y, q.z);
        glm::vec3 euler = glm::eulerAngles(gq);  // 弧度
        return Vec3(
            glm::degrees(euler.x),
            glm::degrees(euler.y),
            glm::degrees(euler.z)
        );
    }

    // ════════════════════════════════════════════
    // 四元数 ↔ 矩阵
    // ════════════════════════════════════════════

    Mat4 AnimationBlend::ToMatrix(const Quat& q) {
        glm::quat gq(q.w, q.x, q.y, q.z);
        glm::mat4 m = glm::mat4_cast(gq);
        Mat4 result;
        std::memcpy(result.Data(), glm::value_ptr(m), sizeof(float32) * 16);
        return result;
    }

    Quat AnimationBlend::FromMatrix(const Mat4& m) {
        glm::mat4 gm = glm::make_mat4(m.Data());
        glm::quat q = glm::quat_cast(gm);
        return Quat(q.x, q.y, q.z, q.w);
    }

    // ════════════════════════════════════════════
    // 矩阵分解工具
    // ════════════════════════════════════════════

    Vec3 AnimationBlend::DecomposeTranslation(const Mat4& m) {
        // 平移在矩阵的最后一列：data[12], data[13], data[14]
        return Vec3(m.data[12], m.data[13], m.data[14]);
    }

    Quat AnimationBlend::DecomposeRotation(const Mat4& m) {
        glm::mat4 gm = glm::make_mat4(m.Data());
        glm::quat q = glm::quat_cast(gm);
        return Quat(q.x, q.y, q.z, q.w);
    }

    Vec3 AnimationBlend::DecomposeScale(const Mat4& m) {
        // 从矩阵的 3×3 部分提取缩放
        glm::mat4 gm = glm::make_mat4(m.Data());
        glm::vec3 scale;
        scale.x = glm::length(glm::vec3(gm[0]));
        scale.y = glm::length(glm::vec3(gm[1]));
        scale.z = glm::length(glm::vec3(gm[2]));
        return Vec3(scale.x, scale.y, scale.z);
    }

    // ════════════════════════════════════════════
    // 骨骼姿势混合
    // ════════════════════════════════════════════

    void AnimationBlend::BlendSkeletonPose(Skeleton& outSkeleton,
                                           const Skeleton& poseA,
                                           const Skeleton& poseB,
                                           float32 t) {
        size_t boneCount = outSkeleton.GetBoneCount();
        if (boneCount == 0) return;

        // 确保三个骨骼数量一致
        size_t countA = poseA.GetBoneCount();
        size_t countB = poseB.GetBoneCount();
        size_t count = std::min({boneCount, countA, countB});

        for (size_t i = 0; i < count; ++i) {
            const Bone& boneA = poseA.GetBone(static_cast<int32>(i));
            const Bone& boneB = poseB.GetBone(static_cast<int32>(i));
            Bone& outBone = outSkeleton.GetBone(static_cast<int32>(i));

            // 从局部矩阵分解
            Vec3 transA = DecomposeTranslation(boneA.localPoseMatrix);
            Vec3 transB = DecomposeTranslation(boneB.localPoseMatrix);
            Quat rotA   = DecomposeRotation(boneA.localPoseMatrix);
            Quat rotB   = DecomposeRotation(boneB.localPoseMatrix);
            Vec3 scaleA = DecomposeScale(boneA.localPoseMatrix);
            Vec3 scaleB = DecomposeScale(boneB.localPoseMatrix);

            // 混合
            Vec3 finalTrans = LERP(transA, transB, t);
            Quat finalRot   = SLERP(rotA, rotB, t);
            Vec3 finalScale = LERP(scaleA, scaleB, t);

            // 重组矩阵: T * R * S
            AnimationPose::ComposeMatrix(finalTrans, ToEuler(finalRot), finalScale,
                                         outBone.localPoseMatrix);
        }

        // 更新世界矩阵和蒙皮矩阵
        outSkeleton.UpdateWorldPoses();
    }

    // ════════════════════════════════════════════
    // 时间线求值混合
    // ════════════════════════════════════════════

    void AnimationBlend::BlendTimelinePose(Skeleton& skeleton,
                                           AnimationPose& poseEval,
                                           const AnimationLocalTimeline& timelineA,
                                           const AnimationLocalTimeline& timelineB,
                                           float32 t,
                                           float32 localTime) {
        // 为了避免修改原时间线的内部时间，我们将 localTime 直接 seek
        // 但更好的方式是创建快照求值。
        // 这里我们克隆骨骼状态来保存两次求值结果

        size_t boneCount = skeleton.GetBoneCount();
        if (boneCount == 0) return;

        // 创建两个临时 Skeleton 用于保存 A/B 的求值结果
        // 注意：我们需要一个"骨架空壳"来保存局部姿势
        // 最简单的方式：对 skeleton 分别求值并备份矩阵

        // 步骤 1: 用 timelineA 求值，备份所有骨骼的 localPoseMatrix
        // 我们使用 const_cast 来模拟"只读求值"——不推进时间线内部状态
        // 实际上我们需要一个不修改时间线状态的 Evaluate 方法

        // 保存 A 的快照
        std::vector<Mat4> poseASnapshots(boneCount);
        {
            // 用 timelineA 对骨架求值
            // 注意：这会修改 timelineA 的内部时间！我们在这里使用 const_cast
            // 并在之后恢复。更干净的方案是让 AnimationLocalTimeline 支持
            // 只读求值（EvaluateAt(localTime)），这可以作为后续改进。
            AnimationLocalTimeline& nonConstA =
                const_cast<AnimationLocalTimeline&>(timelineA);
            float32 savedTimeA = nonConstA.GetLocalTime();
            nonConstA.Seek(localTime);

            poseEval.EvaluateFromTimeline(nonConstA);

            // 备份
            for (size_t i = 0; i < boneCount; ++i) {
                poseASnapshots[i] = skeleton.GetBone(static_cast<int32>(i)).localPoseMatrix;
            }

            nonConstA.Seek(savedTimeA);  // 恢复
        }

        // 保存 B 的快照
        std::vector<Mat4> poseBSnapshots(boneCount);
        {
            AnimationLocalTimeline& nonConstB =
                const_cast<AnimationLocalTimeline&>(timelineB);
            float32 savedTimeB = nonConstB.GetLocalTime();
            nonConstB.Seek(localTime);

            poseEval.EvaluateFromTimeline(nonConstB);

            for (size_t i = 0; i < boneCount; ++i) {
                poseBSnapshots[i] = skeleton.GetBone(static_cast<int32>(i)).localPoseMatrix;
            }

            nonConstB.Seek(savedTimeB);  // 恢复
        }

        // 步骤 2: 混合骨骼姿势
        for (size_t i = 0; i < boneCount; ++i) {
            Bone& outBone = skeleton.GetBone(static_cast<int32>(i));

            // 分解
            Vec3 transA = DecomposeTranslation(poseASnapshots[i]);
            Vec3 transB = DecomposeTranslation(poseBSnapshots[i]);
            Quat rotA   = DecomposeRotation(poseASnapshots[i]);
            Quat rotB   = DecomposeRotation(poseBSnapshots[i]);
            Vec3 scaleA = DecomposeScale(poseASnapshots[i]);
            Vec3 scaleB = DecomposeScale(poseBSnapshots[i]);

            // 混合
            Vec3 finalTrans = LERP(transA, transB, t);
            Quat finalRot   = SLERP(rotA, rotB, t);
            Vec3 finalScale = LERP(scaleA, scaleB, t);

            // 重组
            AnimationPose::ComposeMatrix(finalTrans, ToEuler(finalRot), finalScale,
                                         outBone.localPoseMatrix);
        }

        // 更新世界矩阵和蒙皮矩阵
        skeleton.UpdateWorldPoses();
    }

    // ════════════════════════════════════════════
    // 分部混合（Masked Blending）
    // ════════════════════════════════════════════

    void AnimationBlend::BlendSkeletonPoseMasked(Skeleton& outSkeleton,
                                                  const Skeleton& poseA,
                                                  const Skeleton& poseB,
                                                  const BlendMask& mask,
                                                  float32 t) {
        size_t boneCount = outSkeleton.GetBoneCount();
        if (boneCount == 0) return;

        size_t countA = poseA.GetBoneCount();
        size_t countB = poseB.GetBoneCount();
        size_t count = std::min({boneCount, countA, countB, mask.GetSize()});
        if (count == 0) return;

        for (size_t i = 0; i < count; ++i) {
            float32 effectiveWeight = mask.GetBoneWeight(static_cast<int32>(i)) * t;
            // 钳制
            if (effectiveWeight <= 0.0f) continue;  // 完全使用 poseA（不修改）
            if (effectiveWeight >= 1.0f) {
                effectiveWeight = 1.0f;
            }

            const Bone& boneA = poseA.GetBone(static_cast<int32>(i));
            const Bone& boneB = poseB.GetBone(static_cast<int32>(i));
            Bone& outBone = outSkeleton.GetBone(static_cast<int32>(i));

            if (effectiveWeight >= 1.0f) {
                // 完全使用 poseB
                outBone.localPoseMatrix = boneB.localPoseMatrix;
                continue;
            }

            // 分解
            Vec3 transA = DecomposeTranslation(boneA.localPoseMatrix);
            Vec3 transB = DecomposeTranslation(boneB.localPoseMatrix);
            Quat rotA   = DecomposeRotation(boneA.localPoseMatrix);
            Quat rotB   = DecomposeRotation(boneB.localPoseMatrix);
            Vec3 scaleA = DecomposeScale(boneA.localPoseMatrix);
            Vec3 scaleB = DecomposeScale(boneB.localPoseMatrix);

            // 混合
            Vec3 finalTrans = LERP(transA, transB, effectiveWeight);
            Quat finalRot   = SLERP(rotA, rotB, effectiveWeight);
            Vec3 finalScale = LERP(scaleA, scaleB, effectiveWeight);

            // 重组
            AnimationPose::ComposeMatrix(finalTrans, ToEuler(finalRot), finalScale,
                                         outBone.localPoseMatrix);
        }

        outSkeleton.UpdateWorldPoses();
    }

    // ════════════════════════════════════════════
    // 加法混合（Additive Blending）
    // ════════════════════════════════════════════

    void AnimationBlend::ApplyAdditivePose(Skeleton& basePose,
                                            const Skeleton& additivePose,
                                            const Skeleton& referencePose,
                                            float32 weight) {
        size_t boneCount = basePose.GetBoneCount();
        if (boneCount == 0) return;

        size_t count = std::min({boneCount,
                                 additivePose.GetBoneCount(),
                                 referencePose.GetBoneCount()});

        for (size_t i = 0; i < count; ++i) {
            Bone& base = basePose.GetBone(static_cast<int32>(i));
            const Bone& add = additivePose.GetBone(static_cast<int32>(i));
            const Bone& ref = referencePose.GetBone(static_cast<int32>(i));

            // ── 平移: base + (add - ref) * weight ──
            Vec3 baseT = DecomposeTranslation(base.localPoseMatrix);
            Vec3 addT  = DecomposeTranslation(add.localPoseMatrix);
            Vec3 refT  = DecomposeTranslation(ref.localPoseMatrix);
            Vec3 finalT(
                baseT.x + (addT.x - refT.x) * weight,
                baseT.y + (addT.y - refT.y) * weight,
                baseT.z + (addT.z - refT.z) * weight
            );

            // ── 旋转: result = base * slerp(identity, ref^{-1} * add, weight) ──
            Quat baseR = DecomposeRotation(base.localPoseMatrix);
            Quat addR  = DecomposeRotation(add.localPoseMatrix);
            Quat refR  = DecomposeRotation(ref.localPoseMatrix);
            Quat finalR;

            // 计算 delta = ref^{-1} * add（从参考到加法的增量旋转）
            {
                glm::quat gBase(baseR.w, baseR.x, baseR.y, baseR.z);
                glm::quat gAdd(addR.w, addR.x, addR.y, addR.z);
                glm::quat gRef(refR.w, refR.x, refR.y, refR.z);

                // delta = ref^{-1} * add
                glm::quat gDelta = glm::inverse(gRef) * gAdd;

                // 对 delta 做 SLERP: identity → delta
                glm::quat gBlendedDelta = glm::slerp(
                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f),  // identity
                    gDelta, weight);

                // 最终旋转 = base * blendedDelta
                glm::quat gFinal = gBase * gBlendedDelta;
                finalR = Quat(gFinal.x, gFinal.y, gFinal.z, gFinal.w);
            }

            // ── 缩放: base + (add - ref) * weight ──
            Vec3 baseS = DecomposeScale(base.localPoseMatrix);
            Vec3 addS  = DecomposeScale(add.localPoseMatrix);
            Vec3 refS  = DecomposeScale(ref.localPoseMatrix);
            Vec3 finalS(
                baseS.x + (addS.x - refS.x) * weight,
                baseS.y + (addS.y - refS.y) * weight,
                baseS.z + (addS.z - refS.z) * weight
            );

            // 重组矩阵
            AnimationPose::ComposeMatrix(finalT, ToEuler(finalR), finalS,
                                         base.localPoseMatrix);
        }

        basePose.UpdateWorldPoses();
    }

    // ════════════════════════════════════════════
    // 带遮罩的加法混合
    // ════════════════════════════════════════════

    void AnimationBlend::ApplyAdditivePoseMasked(Skeleton& basePose,
                                                  const Skeleton& additivePose,
                                                  const Skeleton& referencePose,
                                                  const BlendMask& mask,
                                                  float32 weight) {
        size_t boneCount = basePose.GetBoneCount();
        if (boneCount == 0) return;

        size_t count = std::min({boneCount,
                                 additivePose.GetBoneCount(),
                                 referencePose.GetBoneCount(),
                                 mask.GetSize()});

        for (size_t i = 0; i < count; ++i) {
            float32 effectiveWeight = mask.GetBoneWeight(static_cast<int32>(i)) * weight;
            if (effectiveWeight <= 1e-6f) continue;
            if (effectiveWeight > 1.0f) effectiveWeight = 1.0f;

            Bone& base = basePose.GetBone(static_cast<int32>(i));
            const Bone& add = additivePose.GetBone(static_cast<int32>(i));
            const Bone& ref = referencePose.GetBone(static_cast<int32>(i));

            // ── 平移 ──
            Vec3 baseT = DecomposeTranslation(base.localPoseMatrix);
            Vec3 addT  = DecomposeTranslation(add.localPoseMatrix);
            Vec3 refT  = DecomposeTranslation(ref.localPoseMatrix);
            Vec3 finalT(
                baseT.x + (addT.x - refT.x) * effectiveWeight,
                baseT.y + (addT.y - refT.y) * effectiveWeight,
                baseT.z + (addT.z - refT.z) * effectiveWeight
            );

            // ── 旋转 ──
            Quat baseR = DecomposeRotation(base.localPoseMatrix);
            Quat addR  = DecomposeRotation(add.localPoseMatrix);
            Quat refR  = DecomposeRotation(ref.localPoseMatrix);

            glm::quat gBase(baseR.w, baseR.x, baseR.y, baseR.z);
            glm::quat gAdd(addR.w, addR.x, addR.y, addR.z);
            glm::quat gRef(refR.w, refR.x, refR.y, refR.z);

            glm::quat gDelta = glm::inverse(gRef) * gAdd;
            glm::quat gBlendedDelta = glm::slerp(
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                gDelta, effectiveWeight);
            glm::quat gFinal = gBase * gBlendedDelta;
            Quat finalR(gFinal.x, gFinal.y, gFinal.z, gFinal.w);

            // ── 缩放 ──
            Vec3 baseS = DecomposeScale(base.localPoseMatrix);
            Vec3 addS  = DecomposeScale(add.localPoseMatrix);
            Vec3 refS  = DecomposeScale(ref.localPoseMatrix);
            Vec3 finalS(
                baseS.x + (addS.x - refS.x) * effectiveWeight,
                baseS.y + (addS.y - refS.y) * effectiveWeight,
                baseS.z + (addS.z - refS.z) * effectiveWeight
            );

            AnimationPose::ComposeMatrix(finalT, ToEuler(finalR), finalS,
                                         base.localPoseMatrix);
        }

        basePose.UpdateWorldPoses();
    }

} // namespace Engine
