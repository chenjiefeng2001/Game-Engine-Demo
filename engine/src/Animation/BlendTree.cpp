/**
 * @file BlendTree.cpp
 * @brief 混合操作树实现
 */

#include "Engine/Animation/BlendTree.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace Engine {

    // ════════════════════════════════════════════
    // BlendNode 静态辅助
    // ════════════════════════════════════════════

    void BlendNode::ExtractClipPose(const AnimationResource& resource,
                                     const std::string& clipName,
                                     float32 time,
                                     Skeleton& skeleton,
                                     PoseLocalData& out) {
        auto clip = resource.GetClip(clipName);
        if (!clip) return;

        float32 savedTime = clip->GetLocalTime();
        const_cast<AnimationLocalTimeline*>(clip.get())->Seek(time);

        // 使用 AnimationPose 求值
        AnimationPose poseEval(resource.skeleton);
        poseEval.EvaluateFromTimeline(*clip);

        // 读回
        ReadPoseFromSkeleton(skeleton, out);

        const_cast<AnimationLocalTimeline*>(clip.get())->Seek(savedTime);
    }

    // ════════════════════════════════════════════
    // ClipBlendNode
    // ════════════════════════════════════════════

    void ClipBlendNode::Evaluate(Skeleton& skeleton,
                                  const AnimationResource& resource,
                                  float32 time,
                                  PoseLocalData& out) const {
        ExtractClipPose(resource, m_ClipName, time, skeleton, out);
    }

    // ════════════════════════════════════════════
    // LERPBlendNode
    // ════════════════════════════════════════════

    void LERPBlendNode::Evaluate(Skeleton& skeleton,
                                  const AnimationResource& resource,
                                  float32 time,
                                  PoseLocalData& out) const {
        size_t boneCount = std::min(out.GetBoneCount(), skeleton.GetBoneCount());
        if (boneCount == 0) return;

        // 求值子节点 A
        PoseLocalData poseA;
        poseA.Resize(boneCount);
        m_ChildA->Evaluate(skeleton, resource, time, poseA);

        // 求值子节点 B
        PoseLocalData poseB;
        poseB.Resize(boneCount);
        m_ChildB->Evaluate(skeleton, resource, time, poseB);

        // LERP/SLERP 混合
        float32 t = std::max(0.0f, std::min(1.0f, m_BlendWeight));
        for (size_t i = 0; i < boneCount; ++i) {
            out.translations[i] = AnimationBlend::LERP(poseA.translations[i], poseB.translations[i], t);
            out.rotations[i]    = AnimationBlend::SLERP(poseA.rotations[i], poseB.rotations[i], t);
            out.scales[i]       = AnimationBlend::LERP(poseA.scales[i], poseB.scales[i], t);
        }
    }

    // ════════════════════════════════════════════
    // AdditiveBlendNode
    // ════════════════════════════════════════════

    void AdditiveBlendNode::Evaluate(Skeleton& skeleton,
                                      const AnimationResource& resource,
                                      float32 time,
                                      PoseLocalData& out) const {
        size_t boneCount = std::min(out.GetBoneCount(), skeleton.GetBoneCount());
        if (boneCount == 0) return;

        // 求值基础姿势
        PoseLocalData basePose;
        basePose.Resize(boneCount);
        m_Base->Evaluate(skeleton, resource, time, basePose);

        // 求值加法姿势
        PoseLocalData addPose;
        addPose.Resize(boneCount);
        m_Additive->Evaluate(skeleton, resource, time, addPose);

        // 获取参考姿势（绑定姿势）
        PoseLocalData refPose;
        refPose.Resize(boneCount);
        for (size_t i = 0; i < boneCount; ++i) {
            const Bone& bone = skeleton.GetBone(static_cast<int32>(i));
            refPose.translations[i] = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
            refPose.rotations[i]    = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
            refPose.scales[i]       = AnimationBlend::DecomposeScale(bone.localPoseMatrix);
        }
        // 使用绑定姿势
        if (resource.bindPose) {
            for (size_t i = 0; i < boneCount; ++i) {
                const Bone& bone = resource.bindPose->GetBone(static_cast<int32>(i));
                refPose.translations[i] = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
                refPose.rotations[i]    = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
                refPose.scales[i]       = AnimationBlend::DecomposeScale(bone.localPoseMatrix);
            }
        }

        float32 w = std::max(0.0f, std::min(1.0f, m_Weight));

        for (size_t i = 0; i < boneCount; ++i) {
            // T: base + (add - ref) * w
            out.translations[i].x = basePose.translations[i].x + (addPose.translations[i].x - refPose.translations[i].x) * w;
            out.translations[i].y = basePose.translations[i].y + (addPose.translations[i].y - refPose.translations[i].y) * w;
            out.translations[i].z = basePose.translations[i].z + (addPose.translations[i].z - refPose.translations[i].z) * w;

            // R: slerp(base, add, w)
            out.rotations[i] = AnimationBlend::SLERP(basePose.rotations[i], addPose.rotations[i], w);

            // S: base + (add - ref) * w
            out.scales[i].x = basePose.scales[i].x + (addPose.scales[i].x - refPose.scales[i].x) * w;
            out.scales[i].y = basePose.scales[i].y + (addPose.scales[i].y - refPose.scales[i].y) * w;
            out.scales[i].z = basePose.scales[i].z + (addPose.scales[i].z - refPose.scales[i].z) * w;
        }
    }

    // ════════════════════════════════════════════
    // MaskedLERPBlendNode
    // ════════════════════════════════════════════

    void MaskedLERPBlendNode::Evaluate(Skeleton& skeleton,
                                        const AnimationResource& resource,
                                        float32 time,
                                        PoseLocalData& out) const {
        size_t boneCount = std::min(out.GetBoneCount(), skeleton.GetBoneCount());
        if (boneCount == 0) return;

        PoseLocalData poseA, poseB;
        poseA.Resize(boneCount);
        poseB.Resize(boneCount);

        m_ChildA->Evaluate(skeleton, resource, time, poseA);
        m_ChildB->Evaluate(skeleton, resource, time, poseB);

        for (size_t i = 0; i < boneCount; ++i) {
            float32 w = m_BlendWeight;
            if (m_Mask && i < static_cast<size_t>(m_Mask->GetSize())) {
                w *= m_Mask->GetBoneWeight(static_cast<int32>(i));
            }
            w = std::max(0.0f, std::min(1.0f, w));

            out.translations[i] = AnimationBlend::LERP(poseA.translations[i], poseB.translations[i], w);
            out.rotations[i]    = AnimationBlend::SLERP(poseA.rotations[i], poseB.rotations[i], w);
            out.scales[i]       = AnimationBlend::LERP(poseA.scales[i], poseB.scales[i], w);
        }
    }

    // ════════════════════════════════════════════
    // WeightedAvgBlendNode — 扁平加权平均
    // ════════════════════════════════════════════

    void WeightedAvgBlendNode::Evaluate(Skeleton& skeleton,
                                         const AnimationResource& resource,
                                         float32 time,
                                         PoseLocalData& out) const {
        size_t boneCount = std::min(out.GetBoneCount(), skeleton.GetBoneCount());
        if (boneCount == 0 || m_Inputs.empty()) return;

        // 计算总权重
        float32 totalWeight = 0.0f;
        for (const auto& input : m_Inputs) {
            totalWeight += std::abs(input.weight);
        }
        if (totalWeight < 1e-8f) return;

        // 清零输出
        for (size_t i = 0; i < boneCount; ++i) {
            out.translations[i] = Vec3(0, 0, 0);
            out.rotations[i]    = Quat::Identity();
            out.scales[i]       = Vec3(0, 0, 0);  // 加权求和，不用单位缩放
        }

        bool first = true;

        // 对每个输入：提取姿势 + 加权累加
        for (const auto& input : m_Inputs) {
            float32 normWeight = input.weight / totalWeight;
            if (normWeight < 1e-8f) continue;

            PoseLocalData inputPose;
            inputPose.Resize(boneCount);
            ExtractClipPose(resource, input.clipName, time, skeleton, inputPose);

            if (input.additive) {
                // 加法模式：累加差异
                for (size_t i = 0; i < boneCount; ++i) {
                    float32 w = normWeight;
                    if (m_GlobalMask && i < static_cast<size_t>(m_GlobalMask->GetSize())) {
                        w *= m_GlobalMask->GetBoneWeight(static_cast<int32>(i));
                    }

                    out.translations[i].x += inputPose.translations[i].x * w;
                    out.translations[i].y += inputPose.translations[i].y * w;
                    out.translations[i].z += inputPose.translations[i].z * w;

                    // 旋转：累加 SLERP
                    out.rotations[i] = AnimationBlend::SLERP(
                        out.rotations[i], inputPose.rotations[i], w);

                    out.scales[i].x += inputPose.scales[i].x * w;
                    out.scales[i].y += inputPose.scales[i].y * w;
                    out.scales[i].z += inputPose.scales[i].z * w;
                }
            } else {
                // 常规模式：加权平均
                for (size_t i = 0; i < boneCount; ++i) {
                    float32 w = normWeight;
                    if (m_GlobalMask && i < static_cast<size_t>(m_GlobalMask->GetSize())) {
                        w *= m_GlobalMask->GetBoneWeight(static_cast<int32>(i));
                    }

                    if (first) {
                        // 第一个输入直接赋值
                        out.translations[i] = {
                            inputPose.translations[i].x * w,
                            inputPose.translations[i].y * w,
                            inputPose.translations[i].z * w
                        };
                        out.rotations[i] = inputPose.rotations[i];
                        out.scales[i] = {
                            inputPose.scales[i].x * w,
                            inputPose.scales[i].y * w,
                            inputPose.scales[i].z * w
                        };
                    } else {
                        out.translations[i].x += inputPose.translations[i].x * w;
                        out.translations[i].y += inputPose.translations[i].y * w;
                        out.translations[i].z += inputPose.translations[i].z * w;

                        out.rotations[i] = AnimationBlend::SLERP(
                            out.rotations[i], inputPose.rotations[i], w);

                        out.scales[i].x += inputPose.scales[i].x * w;
                        out.scales[i].y += inputPose.scales[i].y * w;
                        out.scales[i].z += inputPose.scales[i].z * w;
                    }
                }
                first = false;
            }
        }
    }

    // ════════════════════════════════════════════
    // BlendTree
    // ════════════════════════════════════════════

    ClipBlendNode* BlendTree::AddClip(const std::string& clipName) {
        auto node = std::make_unique<ClipBlendNode>(clipName);
        auto* ptr = node.get();
        m_OwnedNodes.push_back(std::move(node));
        return ptr;
    }

    LERPBlendNode* BlendTree::AddLERP(std::unique_ptr<BlendNode> childA,
                                       std::unique_ptr<BlendNode> childB,
                                       float32 blendWeight) {
        auto node = std::make_unique<LERPBlendNode>(
            std::move(childA), std::move(childB), blendWeight);
        auto* ptr = node.get();
        m_OwnedNodes.push_back(std::move(node));
        return ptr;
    }

    LERPBlendNode* BlendTree::AddLERP(const std::string& clipA,
                                       const std::string& clipB,
                                       float32 blendWeight) {
        auto childA = std::make_unique<ClipBlendNode>(clipA);
        auto childB = std::make_unique<ClipBlendNode>(clipB);
        auto node = std::make_unique<LERPBlendNode>(
            std::move(childA), std::move(childB), blendWeight);
        auto* ptr = node.get();
        m_OwnedNodes.push_back(std::move(node));
        return ptr;
    }

    AdditiveBlendNode* BlendTree::AddAdditive(std::unique_ptr<BlendNode> base,
                                               std::unique_ptr<BlendNode> additive,
                                               float32 weight) {
        auto node = std::make_unique<AdditiveBlendNode>(
            std::move(base), std::move(additive), weight);
        auto* ptr = node.get();
        m_OwnedNodes.push_back(std::move(node));
        return ptr;
    }

    MaskedLERPBlendNode* BlendTree::AddMaskedLERP(const std::string& clipA,
                                                    const std::string& clipB,
                                                    const BlendMask& mask,
                                                    float32 blendWeight) {
        auto childA = std::make_unique<ClipBlendNode>(clipA);
        auto childB = std::make_unique<ClipBlendNode>(clipB);
        auto node = std::make_unique<MaskedLERPBlendNode>(
            std::move(childA), std::move(childB), mask, blendWeight);
        auto* ptr = node.get();
        m_OwnedNodes.push_back(std::move(node));
        return ptr;
    }

    WeightedAvgBlendNode* BlendTree::AddWeightedAvg() {
        auto node = std::make_unique<WeightedAvgBlendNode>();
        auto* ptr = node.get();
        m_OwnedNodes.push_back(std::move(node));
        return ptr;
    }

    void BlendTree::Evaluate(Skeleton& skeleton,
                              const AnimationResource& resource,
                              float32 time) {
        if (!m_Root) return;

        size_t boneCount = skeleton.GetBoneCount();
        PoseLocalData result;
        result.Resize(boneCount);

        m_Root->Evaluate(skeleton, resource, time, result);

        // 将结果写入骨骼
        for (size_t i = 0; i < boneCount; ++i) {
            Vec3 euler = AnimationBlend::ToEuler(result.rotations[i]);
            AnimationPose::ComposeMatrix(
                result.translations[i], euler, result.scales[i],
                skeleton.GetBone(static_cast<int32>(i)).localPoseMatrix);
        }

        // 更新世界矩阵和蒙皮矩阵
        skeleton.UpdateWorldPoses();
    }

    void BlendTree::EvaluateToPose(PoseLocalData& out, Skeleton& skeleton,
                                    const AnimationResource& resource,
                                    float32 time) {
        if (!m_Root) return;
        m_Root->Evaluate(skeleton, resource, time, out);
    }


    // ════════════════════════════════════════════
    // BilinearBlendNode — 2D 双线性混合
    // ════════════════════════════════════════════

    void BilinearBlendNode::Evaluate(Skeleton& skeleton,
                                      const AnimationResource& resource,
                                      float32 time,
                                      PoseLocalData& out) const {
        size_t boneCount = std::min(out.GetBoneCount(), skeleton.GetBoneCount());
        if (boneCount == 0) return;

        // 提取四个角落的姿势
        PoseLocalData poses[4];
        for (int i = 0; i < 4; ++i) poses[i].Resize(boneCount);
        ExtractClipPose(resource, m_Clips[0], time, skeleton, poses[0]);  // C00
        ExtractClipPose(resource, m_Clips[1], time, skeleton, poses[1]);  // C10
        ExtractClipPose(resource, m_Clips[2], time, skeleton, poses[2]);  // C01
        ExtractClipPose(resource, m_Clips[3], time, skeleton, poses[3]);  // C11

        float32 tx = std::max(0.0f, std::min(1.0f, m_TX));
        float32 ty = std::max(0.0f, std::min(1.0f, m_TY));

        // 对每个骨骼：双线性 LERP/SLERP
        // top    = LERP(C00, C10, tx)
        // bottom = LERP(C01, C11, tx)
        // result = LERP(top, bottom, ty)
        for (size_t i = 0; i < boneCount; ++i) {
            // top row: C00 ↔ C10
            Vec3 topT = AnimationBlend::LERP(poses[0].translations[i], poses[1].translations[i], tx);
            Quat topR = AnimationBlend::SLERP(poses[0].rotations[i], poses[1].rotations[i], tx);
            Vec3 topS = AnimationBlend::LERP(poses[0].scales[i], poses[1].scales[i], tx);

            // bottom row: C01 ↔ C11
            Vec3 botT = AnimationBlend::LERP(poses[2].translations[i], poses[3].translations[i], tx);
            Quat botR = AnimationBlend::SLERP(poses[2].rotations[i], poses[3].rotations[i], tx);
            Vec3 botS = AnimationBlend::LERP(poses[2].scales[i], poses[3].scales[i], tx);

            // final: top ↔ bottom along Y
            out.translations[i] = AnimationBlend::LERP(topT, botT, ty);
            out.rotations[i]    = AnimationBlend::SLERP(topR, botR, ty);
            out.scales[i]       = AnimationBlend::LERP(topS, botS, ty);
        }
    }

    // ════════════════════════════════════════════
    // TriLERPBlendNode — 三角 LERP 混合
    // ════════════════════════════════════════════

    void TriLERPBlendNode::Evaluate(Skeleton& skeleton,
                                     const AnimationResource& resource,
                                     float32 time,
                                     PoseLocalData& out) const {
        size_t boneCount = std::min(out.GetBoneCount(), skeleton.GetBoneCount());
        if (boneCount == 0) return;

        // 提取三个顶点的姿势
        PoseLocalData poses[3];
        for (int i = 0; i < 3; ++i) poses[i].Resize(boneCount);
        ExtractClipPose(resource, m_Clips[0], time, skeleton, poses[0]);
        ExtractClipPose(resource, m_Clips[1], time, skeleton, poses[1]);
        ExtractClipPose(resource, m_Clips[2], time, skeleton, poses[2]);

        // 归一化权重
        float32 sum = m_Weights[0] + m_Weights[1] + m_Weights[2];
        float32 w[3] = {m_Weights[0], m_Weights[1], m_Weights[2]};
        if (sum > 1e-10f) {
            w[0] /= sum; w[1] /= sum; w[2] /= sum;
        }

        // 对每个骨骼做三路加权混合
        for (size_t i = 0; i < boneCount; ++i) {
            // 平移：三路加权和
            out.translations[i].x = poses[0].translations[i].x * w[0]
                                  + poses[1].translations[i].x * w[1]
                                  + poses[2].translations[i].x * w[2];
            out.translations[i].y = poses[0].translations[i].y * w[0]
                                  + poses[1].translations[i].y * w[1]
                                  + poses[2].translations[i].y * w[2];
            out.translations[i].z = poses[0].translations[i].z * w[0]
                                  + poses[1].translations[i].z * w[1]
                                  + poses[2].translations[i].z * w[2];

            // 旋转：三路逐步 SLERP
            float32 sum01 = w[0] + w[1];
            float32 t01 = (sum01 > 1e-10f) ? w[1] / sum01 : 0.0f;
            Quat blended01 = AnimationBlend::SLERP(poses[0].rotations[i],
                                                    poses[1].rotations[i], t01);
            out.rotations[i] = AnimationBlend::SLERP(blended01,
                                                      poses[2].rotations[i], w[2]);

            // 缩放：三路加权和
            out.scales[i].x = poses[0].scales[i].x * w[0]
                            + poses[1].scales[i].x * w[1]
                            + poses[2].scales[i].x * w[2];
            out.scales[i].y = poses[0].scales[i].y * w[0]
                            + poses[1].scales[i].y * w[1]
                            + poses[2].scales[i].y * w[2];
            out.scales[i].z = poses[0].scales[i].z * w[0]
                            + poses[1].scales[i].z * w[1]
                            + poses[2].scales[i].z * w[2];
        }
    }

    // ════════════════════════════════════════════
    // FadeBlendNode — 淡入/淡出
    // ════════════════════════════════════════════

    void FadeBlendNode::FadeIn(float32 duration) {
        m_FadeState   = FadeState::FadingIn;
        m_FadeDuration = duration;
        m_FadeTimer    = 0.0f;
        m_FadeProgress = 0.0f;
    }

    void FadeBlendNode::FadeOut(float32 duration) {
        m_FadeState   = FadeState::FadingOut;
        m_FadeDuration = duration;
        m_FadeTimer    = 0.0f;
        m_FadeProgress = 1.0f;
    }

    void FadeBlendNode::CrossFade(std::unique_ptr<BlendNode> newChild, float32 duration) {
        m_TargetChild = std::move(newChild);
        m_FadeState   = FadeState::FadingOut;
        m_FadeDuration = duration;
        m_FadeTimer    = 0.0f;
        // 如果 m_FadeProgress 尚未初始化，从 1 开始
        if (m_FadeProgress > 1.0f || m_FadeProgress < 0.0f)
            m_FadeProgress = 1.0f;
    }

    void FadeBlendNode::UpdateFade(float32 dt) {
        if (m_FadeState == FadeState::None) return;

        m_FadeTimer += dt;
        float32 t = (m_FadeDuration > 1e-6f)
            ? std::min(m_FadeTimer / m_FadeDuration, 1.0f)
            : 1.0f;

        // 应用 smoothstep 使过渡更自然
        float32 smoothT = m_UseSmoothStep
            ? t * t * (3.0f - 2.0f * t)
            : t;

        switch (m_FadeState) {
            case FadeState::FadingIn:
                m_FadeProgress = smoothT;
                if (t >= 1.0f) {
                    m_FadeProgress = 1.0f;
                    m_FadeState = FadeState::None;
                }
                break;

            case FadeState::FadingOut:
                m_FadeProgress = 1.0f - smoothT;
                if (t >= 1.0f) {
                    // 淡出完成：切换到目标（如果有的话）
                    if (m_TargetChild) {
                        m_Child = std::move(m_TargetChild);
                        m_FadeProgress = 1.0f;
                        m_FadeState = FadeState::FadingIn;
                        m_FadeTimer = 0.0f;
                    } else {
                        m_FadeProgress = 0.0f;
                        m_FadeState = FadeState::None;
                    }
                }
                break;

            default:
                break;
        }
    }

    void FadeBlendNode::Evaluate(Skeleton& skeleton,
                                  const AnimationResource& resource,
                                  float32 time,
                                  PoseLocalData& out) const {
        size_t boneCount = std::min(out.GetBoneCount(), skeleton.GetBoneCount());
        if (boneCount == 0) return;

        float32 fadeWeight = std::max(0.0f, std::min(1.0f, m_FadeProgress));

        if (fadeWeight >= 1.0f) {
            // 完全不透明：直接求值子节点
            if (m_Child)
                m_Child->Evaluate(skeleton, resource, time, out);
            return;
        }

        if (fadeWeight <= 0.0f) {
            // 完全透明：输出零姿势（使用绑定姿势）
            for (size_t i = 0; i < boneCount; ++i) {
                const Bone& bone = skeleton.GetBone(static_cast<int32>(i));
                out.translations[i] = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
                out.rotations[i]    = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
                out.scales[i]       = AnimationBlend::DecomposeScale(bone.localPoseMatrix);
            }
            return;
        }

        // 部分透明：在子节点姿势和绑定姿势之间混合
        PoseLocalData childPose;
        childPose.Resize(boneCount);
        if (m_Child)
            m_Child->Evaluate(skeleton, resource, time, childPose);

        for (size_t i = 0; i < boneCount; ++i) {
            const Bone& bone = skeleton.GetBone(static_cast<int32>(i));
            Vec3 bindT = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
            Quat bindR = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
            Vec3 bindS = AnimationBlend::DecomposeScale(bone.localPoseMatrix);

            out.translations[i] = AnimationBlend::LERP(bindT, childPose.translations[i], fadeWeight);
            out.rotations[i]    = AnimationBlend::SLERP(bindR, childPose.rotations[i], fadeWeight);
            out.scales[i]       = AnimationBlend::LERP(bindS, childPose.scales[i], fadeWeight);
        }
    }

    // ════════════════════════════════════════════
    // BlendTree 新工厂方法
    // ════════════════════════════════════════════

    BilinearBlendNode* BlendTree::AddBilinear() {
        auto node = std::make_unique<BilinearBlendNode>();
        auto* ptr = node.get();
        m_OwnedNodes.push_back(std::move(node));
        return ptr;
    }

    TriLERPBlendNode* BlendTree::AddTriLERP() {
        auto node = std::make_unique<TriLERPBlendNode>();
        auto* ptr = node.get();
        m_OwnedNodes.push_back(std::move(node));
        return ptr;
    }

    FadeBlendNode* BlendTree::AddFade(std::unique_ptr<BlendNode> child) {
        auto node = std::make_unique<FadeBlendNode>(std::move(child));
        auto* ptr = node.get();
        m_OwnedNodes.push_back(std::move(node));
        return ptr;
    }

    // ════════════════════════════════════════════
    // 自定义节点注册表
    // ════════════════════════════════════════════

    using CustomRegistry = std::unordered_map<std::string, CustomNodeDesc>;
    static CustomRegistry& GetGlobalCustomRegistry() {
        static CustomRegistry reg;
        return reg;
    }

    void BlendTree::RegisterCustomNode(const std::string& typeName,
                                        CustomNodeFactory factory,
                                        const std::string& desc) {
        GetGlobalCustomRegistry()[typeName] = {typeName, std::move(factory), desc};
    }

    std::unique_ptr<BlendNode> BlendTree::CreateCustomNode(const std::string& typeName) {
        auto& reg = GetGlobalCustomRegistry();
        auto it = reg.find(typeName);
        return (it != reg.end() && it->second.factory) ? it->second.factory() : nullptr;
    }

    std::vector<CustomNodeDesc> BlendTree::GetRegisteredCustomNodes() {
        std::vector<CustomNodeDesc> result;
        for (const auto& [key, desc] : GetGlobalCustomRegistry()) {
            result.push_back(desc);
        }
        return result;
    }

    // ════════════════════════════════════════════
    // DSL 语法解析
    // ════════════════════════════════════════════

    static std::string Trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        size_t end   = s.find_last_not_of(" \t\r\n");
        return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
    }

    static std::vector<std::string> SplitArgs(const std::string& args) {
        std::vector<std::string> result;
        int32 depth = 0;
        size_t start = 0;
        for (size_t i = 0; i <= args.size(); ++i) {
            if (i < args.size()) {
                if (args[i] == '(') depth++;
                if (args[i] == ')') depth--;
            }
            if ((i == args.size() || (args[i] == ',' && depth == 0)) && i > start) {
                std::string arg = Trim(args.substr(start, i - start));
                if (!arg.empty()) result.push_back(arg);
                start = i + 1;
            }
        }
        return result;
    }

    std::unique_ptr<BlendNode> BlendTree::BuildFromSyntax(const std::string& syntax) {
        std::string s = Trim(syntax);
        size_t openParen = s.find('(');
        if (openParen == std::string::npos) return nullptr;

        std::string funcName = Trim(s.substr(0, openParen));
        size_t closeParen = s.rfind(')');
        if (closeParen == std::string::npos) return nullptr;

        std::string argsStr = s.substr(openParen + 1, closeParen - openParen - 1);
        auto args = SplitArgs(argsStr);

        if (funcName == "clip" && !args.empty()) {
            return std::make_unique<ClipBlendNode>(args[0]);
        }
        if (funcName == "lerp" && args.size() >= 3) {
            return std::make_unique<LERPBlendNode>(
                BuildFromSyntax(args[0]), BuildFromSyntax(args[1]), std::stof(args[2]));
        }
        if (funcName == "additive" && args.size() >= 3) {
            return std::make_unique<AdditiveBlendNode>(
                BuildFromSyntax(args[0]), BuildFromSyntax(args[1]), std::stof(args[2]));
        }
        if (funcName == "fade" && args.size() >= 2) {
            auto node = std::make_unique<FadeBlendNode>(BuildFromSyntax(args[0]));
            node->FadeIn(std::stof(args[1]));
            return node;
        }

        auto custom = CreateCustomNode(funcName);
        if (custom) return custom;

        return nullptr;
    }

    // ════════════════════════════════════════════
    // 序列化 / 现场更新
    // ════════════════════════════════════════════

    std::string BlendTree::Serialize() const {
        std::ostringstream oss;
        oss << "{ \"nodeCount\": " << m_OwnedNodes.size()
            << ", \"hasRoot\": " << (m_Root ? "true" : "false") << " }";
        return oss.str();
    }

    bool BlendTree::Deserialize(const std::string& json) {
        (void)json;
        return true;
    }

    void BlendTree::ReloadFromSource(const std::string& source) {
        m_SourceSyntax = source;
        Clear();
        auto root = BuildFromSyntax(source);
        if (root) {
            SetRoot(std::move(root));
        }
        if (m_LiveEditCallback) {
            m_LiveEditCallback(*this);
        }
    }

    void BlendTree::Clear() {
        m_Root.reset();
        m_OwnedNodes.clear();
    }

    BlendTree::Stats BlendTree::GetStats() const {
        Stats stats;
        stats.totalNodes = static_cast<int32>(m_OwnedNodes.size());
        for (const auto& n : m_OwnedNodes) {
            if (n->GetType() == BlendNodeType::Clip) stats.clipNodes++;
            else stats.blendNodes++;
        }
        return stats;
    }

} // namespace Engine
