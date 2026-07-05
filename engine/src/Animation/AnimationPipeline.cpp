/**
 * @file AnimationPipeline.cpp
 * @brief 动画管线实现 — 6 阶段标准化处理流程
 */

#include "Engine/Animation/AnimationPipeline.h"
#include "Engine/Animation/ConstraintSolver.h"
#include "Engine/Core/Log.h"
#include <chrono>
#include <algorithm>

namespace Engine {

    // ============================================================
    // 构造
    // ============================================================

    AnimationPipeline::AnimationPipeline(std::shared_ptr<Skeleton> skeleton,
                                         std::shared_ptr<AnimationPose> poseEval)
        : m_Skeleton(std::move(skeleton))
        , m_PoseEval(poseEval) {

        // 如果没有提供 poseEval，创建一个默认的
        if (!m_PoseEval && m_Skeleton) {
            m_PoseEval = std::make_shared<AnimationPose>(m_Skeleton);
        }

        // 预分配姿势数据
        if (m_Skeleton) {
            size_t boneCount = m_Skeleton->GetBoneCount();
            m_ExtractedPose.Resize(boneCount);
            m_BlendedPose.Resize(boneCount);
        }
    }

    // ============================================================
    // 骨骼
    // ============================================================

    void AnimationPipeline::SetSkeleton(std::shared_ptr<Skeleton> skeleton) {
        m_Skeleton = std::move(skeleton);
        if (!m_PoseEval) {
            m_PoseEval = std::make_shared<AnimationPose>(m_Skeleton);
        } else {
            m_PoseEval->SetSkeleton(m_Skeleton);
        }
        if (m_Skeleton) {
            size_t n = m_Skeleton->GetBoneCount();
            m_ExtractedPose.Resize(n);
            m_BlendedPose.Resize(n);
        }
    }

    // ════════════════════════════════════════════
    // Stage 1: EXTRACT
    // ════════════════════════════════════════════

    void AnimationPipeline::SetPrimaryClip(const AnimationLocalTimeline* clip, float32 time) {
        m_PrimaryClip = clip;
        m_ExtractTime = time;
    }

    void AnimationPipeline::StageExtract(float32 dt) {
        if (!m_Skeleton || !m_PrimaryClip) return;

        float32 sampleTime = m_ExtractTime;
        if (sampleTime < 0.0f) {
            sampleTime = m_PrimaryClip->GetLocalTime();
        }

        size_t boneCount = m_Skeleton->GetBoneCount();

        // 使用 poseEval 从 clip 提取姿势
        // 先将时间线 seek 到指定时间，提取姿势，再恢复
        // 更优雅的方式：让 AnimationPose 支持指定时间求值

        if (m_PoseEval) {
            // 保存原时间
            float32 savedTime = m_PrimaryClip->GetLocalTime();

            // 注意：这里使用 const_cast 因为 Seek 不是 const
            // 后续应添加 EvaluateAt(localTime) 只读方法
            const_cast<AnimationLocalTimeline*>(m_PrimaryClip)->Seek(sampleTime);
            m_PoseEval->EvaluateFromTimeline(*m_PrimaryClip);

            // 将骨骼的 localPoseMatrix 提取为 PoseLocalData
            for (size_t i = 0; i < boneCount; ++i) {
                const Bone& bone = m_Skeleton->GetBone(static_cast<int32>(i));
                m_ExtractedPose.translations[i] = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
                m_ExtractedPose.rotations[i]    = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
                m_ExtractedPose.scales[i]       = AnimationBlend::DecomposeScale(bone.localPoseMatrix);
            }

            // 恢复原时间
            const_cast<AnimationLocalTimeline*>(m_PrimaryClip)->Seek(savedTime);
        }
    }

    // ════════════════════════════════════════════
    // Stage 2: BLEND
    // ════════════════════════════════════════════

    void AnimationPipeline::ClearBlendInputs() {
        m_BlendInputs.clear();
    }

    void AnimationPipeline::AddBlendInput(const BlendInput& input) {
        m_BlendInputs.push_back(input);
    }

    void AnimationPipeline::BlendPose(PoseLocalData& out,
                                       const PoseLocalData& a,
                                       const PoseLocalData& b,
                                       float32 t, const BlendMask* mask) {
        size_t count = std::min({a.GetBoneCount(), b.GetBoneCount(), out.GetBoneCount()});
        if (count == 0) return;

        for (size_t i = 0; i < count; ++i) {
            float32 weight = t;
            if (mask) {
                weight *= mask->GetBoneWeight(static_cast<int32>(i));
            }

            out.translations[i] = AnimationBlend::LERP(a.translations[i], b.translations[i], weight);
            out.rotations[i]    = AnimationBlend::SLERP(a.rotations[i], b.rotations[i], weight);
            out.scales[i]       = AnimationBlend::LERP(a.scales[i], b.scales[i], weight);
        }
    }

    void AnimationPipeline::StageBlend(float32 dt) {
        if (!m_Skeleton || m_BlendInputs.empty()) {
            // 无混合输入时，将 Extract 结果直接作为 Blend 结果
            m_BlendedPose = m_ExtractedPose;
            return;
        }

        size_t boneCount = m_Skeleton->GetBoneCount();
        if (boneCount == 0) return;

        // 确保 m_BlendedPose 大小正确
        if (m_BlendedPose.GetBoneCount() != boneCount) {
            m_BlendedPose.Resize(boneCount);
        }

        // 清零目标姿势
        for (size_t i = 0; i < boneCount; ++i) {
            m_BlendedPose.translations[i] = Vec3(0, 0, 0);
            m_BlendedPose.rotations[i]    = Quat::Identity();
            m_BlendedPose.scales[i]       = Vec3(1, 1, 1);
        }

        // 处理第一个输入（基础帧）
        bool first = true;
        for (const auto& input : m_BlendInputs) {
            if (!input.clip) continue;

            // 提取此输入在指定时间的姿势
            PoseLocalData inputPose;
            inputPose.Resize(boneCount);

            float32 savedTime = input.clip->GetLocalTime();
            const_cast<AnimationLocalTimeline*>(input.clip)->Seek(
                dt >= 0 ? input.clip->GetLocalTime() : 0.0f);
            m_PoseEval->EvaluateFromTimeline(*input.clip);

            for (size_t i = 0; i < boneCount; ++i) {
                const Bone& bone = m_Skeleton->GetBone(static_cast<int32>(i));
                inputPose.translations[i] = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
                inputPose.rotations[i]    = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
                inputPose.scales[i]       = AnimationBlend::DecomposeScale(bone.localPoseMatrix);
            }
            const_cast<AnimationLocalTimeline*>(input.clip)->Seek(savedTime);

            if (input.additive && !first) {
                // ── 加法混合 ──
                // 从参考姿势提取 delta
                PoseLocalData refPose;
                refPose.Resize(boneCount);
                if (m_ReferencePose) {
                    for (size_t i = 0; i < boneCount; ++i) {
                        refPose.translations[i] = AnimationBlend::DecomposeTranslation(
                            m_ReferencePose->GetBone(static_cast<int32>(i)).localPoseMatrix);
                        refPose.rotations[i] = AnimationBlend::DecomposeRotation(
                            m_ReferencePose->GetBone(static_cast<int32>(i)).localPoseMatrix);
                        refPose.scales[i] = AnimationBlend::DecomposeScale(
                            m_ReferencePose->GetBone(static_cast<int32>(i)).localPoseMatrix);
                    }
                }

                for (size_t i = 0; i < boneCount; ++i) {
                    float32 w = input.mask ? input.weight * input.mask->GetBoneWeight(static_cast<int32>(i))
                                           : input.weight;

                    // T: base + (add - ref) * w
                    m_BlendedPose.translations[i].x += (inputPose.translations[i].x - refPose.translations[i].x) * w;
                    m_BlendedPose.translations[i].y += (inputPose.translations[i].y - refPose.translations[i].y) * w;
                    m_BlendedPose.translations[i].z += (inputPose.translations[i].z - refPose.translations[i].z) * w;

                    // R: base * slerp(identity, ref⁻¹ * add, w)
                    // 简化：SLERP 混合
                    m_BlendedPose.rotations[i] = AnimationBlend::SLERP(
                        m_BlendedPose.rotations[i], inputPose.rotations[i], w);

                    // S
                    m_BlendedPose.scales[i].x += (inputPose.scales[i].x - refPose.scales[i].x) * w;
                    m_BlendedPose.scales[i].y += (inputPose.scales[i].y - refPose.scales[i].y) * w;
                    m_BlendedPose.scales[i].z += (inputPose.scales[i].z - refPose.scales[i].z) * w;
                }
            } else {
                // ── 常规混合（LERP/SLERP） ──
                if (first) {
                    // 第一个输入直接作为基础
                    float32 w = std::max(input.weight, 0.0f);
                    for (size_t i = 0; i < boneCount; ++i) {
                        float32 boneWeight = input.mask
                            ? w * input.mask->GetBoneWeight(static_cast<int32>(i))
                            : w;
                        m_BlendedPose.translations[i] = AnimationBlend::LERP(
                            m_BlendedPose.translations[i], inputPose.translations[i], boneWeight);
                        m_BlendedPose.rotations[i] = AnimationBlend::SLERP(
                            m_BlendedPose.rotations[i], inputPose.rotations[i], boneWeight);
                        m_BlendedPose.scales[i] = AnimationBlend::LERP(
                            m_BlendedPose.scales[i], inputPose.scales[i], boneWeight);
                    }
                    first = false;
                } else {
                    // 后续输入与当前结果混合
                    for (size_t i = 0; i < boneCount; ++i) {
                        float32 boneWeight = input.mask
                            ? input.weight * input.mask->GetBoneWeight(static_cast<int32>(i))
                            : input.weight;
                        m_BlendedPose.translations[i] = AnimationBlend::LERP(
                            m_BlendedPose.translations[i], inputPose.translations[i], boneWeight);
                        m_BlendedPose.rotations[i] = AnimationBlend::SLERP(
                            m_BlendedPose.rotations[i], inputPose.rotations[i], boneWeight);
                        m_BlendedPose.scales[i] = AnimationBlend::LERP(
                            m_BlendedPose.scales[i], inputPose.scales[i], boneWeight);
                    }
                }
            }
        }

        // 如果没有输入，使用提取的姿势
        if (first) {
            m_BlendedPose = m_ExtractedPose;
        }
    }

    // ════════════════════════════════════════════
    // 内部：PoseLocalData ↔ Skeleton
    // ════════════════════════════════════════════

    void AnimationPipeline::PoseToSkeleton(const PoseLocalData& pose,
                                            Skeleton& skeleton) const {
        size_t count = std::min(pose.GetBoneCount(), skeleton.GetBoneCount());
        for (size_t i = 0; i < count; ++i) {
            Vec3 euler = AnimationBlend::ToEuler(pose.rotations[i]);
            AnimationPose::ComposeMatrix(
                pose.translations[i], euler, pose.scales[i],
                skeleton.GetBone(static_cast<int32>(i)).localPoseMatrix);
        }
    }

    void AnimationPipeline::SkeletonToPose(const Skeleton& skeleton,
                                            PoseLocalData& pose) const {
        size_t count = std::min(pose.GetBoneCount(), skeleton.GetBoneCount());
        for (size_t i = 0; i < count; ++i) {
            const Bone& bone = skeleton.GetBone(static_cast<int32>(i));
            pose.translations[i] = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
            pose.rotations[i]    = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
            pose.scales[i]       = AnimationBlend::DecomposeScale(bone.localPoseMatrix);
        }
    }

    // ════════════════════════════════════════════
    // Stage 3: GLOBALIZE
    // ════════════════════════════════════════════

    void AnimationPipeline::StageGlobalize(float32 dt) {
        if (!m_Skeleton) return;

        // ── 应用约束（可选） ──
        if (m_ConstraintSolver) {
            // 将混合后的姿势先写入骨骼（约束可能需读取世界位置）
            PoseToSkeleton(m_BlendedPose, *m_Skeleton);
            m_Skeleton->UpdateWorldPoses();

            // 更新约束求解器的定位器
            m_ConstraintSolver->UpdateLocators(m_Skeleton.get());

            // 求值约束，修改 m_BlendedPose
            m_ConstraintSolver->EvaluateAll(*m_Skeleton, m_BlendedPose);
        }

        // 将混合后的 PoseLocalData 写入骨骼 localPoseMatrix
        PoseToSkeleton(m_BlendedPose, *m_Skeleton);

        // 递归计算世界矩阵
        m_Skeleton->UpdateWorldPoses();

        // 如果启用了自动调色板，同步矩阵
        if (m_AutoPalette) {
            m_MatrixPalette = m_Skeleton->GetSkinningMatrices();
        }
    }

    // ════════════════════════════════════════════
    // Stage 4 & 5: POSTPROCESS
    // ════════════════════════════════════════════

    int32 AnimationPipeline::AddPostProcess(PostProcessFn fn, int32 order) {
        int32 id = static_cast<int32>(m_PostProcessFns.size());
        m_PostProcessFns.push_back({std::move(fn), order});
        // 按 order 排序
        std::sort(m_PostProcessFns.begin(), m_PostProcessFns.end(),
                  [](const PostProcessEntry& a, const PostProcessEntry& b) {
                      return a.order < b.order;
                  });
        return id;
    }

    int32 AnimationPipeline::AddPostProcess2(PostProcessFn fn, int32 order) {
        int32 id = static_cast<int32>(m_PostProcessFns2.size());
        m_PostProcessFns2.push_back({std::move(fn), order});
        std::sort(m_PostProcessFns2.begin(), m_PostProcessFns2.end(),
                  [](const PostProcessEntry& a, const PostProcessEntry& b) {
                      return a.order < b.order;
                  });
        return id;
    }

    void AnimationPipeline::ClearPostProcess() {
        m_PostProcessFns.clear();
    }

    void AnimationPipeline::ClearPostProcess2() {
        m_PostProcessFns2.clear();
    }

    void AnimationPipeline::StagePostProcess(float32 dt) {
        if (!m_Skeleton) return;

        for (const auto& entry : m_PostProcessFns) {
            if (entry.fn) {
                entry.fn(*m_Skeleton, dt);
            }
        }
    }

    void AnimationPipeline::StagePostProcess2(float32 dt) {
        if (!m_Skeleton) return;

        for (const auto& entry : m_PostProcessFns2) {
            if (entry.fn) {
                entry.fn(*m_Skeleton, dt);
            }
        }
    }

    // ════════════════════════════════════════════
    // Stage 6: PALETTE
    // ════════════════════════════════════════════

    void AnimationPipeline::StagePalette(float32 dt) {
        if (!m_Skeleton) return;

        // 从 Skeleton 获取最新蒙皮矩阵
        m_MatrixPalette = m_Skeleton->GetSkinningMatrices();

        // 矩阵调色板已就绪，可通过 GetMatrixPalette() 获取
    }

    // ════════════════════════════════════════════
    // 执行管线
    // ════════════════════════════════════════════

    void AnimationPipeline::Execute(float32 dt) {
        for (int32 i = 0; i < 6; ++i) {
            if (m_StageEnabled[i]) {
                ExecuteStage(static_cast<PipelineStage>(i), dt);
            }
        }
    }

    void AnimationPipeline::ExecuteStage(PipelineStage stage, float32 dt) {
        if (!m_StageEnabled[static_cast<size_t>(stage)]) return;

        auto start = std::chrono::high_resolution_clock::now();

        switch (stage) {
            case PipelineStage::Extract:
                StageExtract(dt);
                break;
            case PipelineStage::Blend:
                StageBlend(dt);
                break;
            case PipelineStage::Globalize:
                StageGlobalize(dt);
                break;
            case PipelineStage::PostProcess:
                StagePostProcess(dt);
                break;
            case PipelineStage::PostProcess2:
                StagePostProcess2(dt);
                break;
            case PipelineStage::Palette:
                StagePalette(dt);
                break;
            default:
                break;
        }

        auto end = std::chrono::high_resolution_clock::now();
        float32 elapsed = std::chrono::duration<float32, std::milli>(end - start).count();
        m_StageTimings[static_cast<size_t>(stage)] = elapsed;
    }

} // namespace Engine
