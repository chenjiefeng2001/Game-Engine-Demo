/**
 * @file AnimationBatch.cpp
 * @brief 动画批量处理实现 — SoA 布局、热/冷分离、SIMD 接口预留
 */

#include "Engine/Animation/AnimationBatch.h"
#include "Engine/Animation/AnimationBlend.h"
#include "Engine/Animation/AnimationPose.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace Engine {

    // ============================================================
    // 构造 / 析构
    // ============================================================

    AnimationBatch::AnimationBatch(int32 maxInstances, int32 maxBones)
        : m_MaxInstances(maxInstances)
        , m_MaxBones(maxBones) {

        // 分配热数据（16 字节对齐，SIMD 友好）
#ifdef _MSC_VER
        m_HotData = static_cast<InstanceHotData*>(
            _aligned_malloc(sizeof(InstanceHotData) * maxInstances, 16));
        m_LocalPoseTime = static_cast<float32*>(
            _aligned_malloc(sizeof(float32) * maxInstances, 16));
        m_Instances = static_cast<AnimationInstance**>(
            _aligned_malloc(sizeof(AnimationInstance*) * maxInstances, 16));
#else
        posix_memalign(reinterpret_cast<void**>(&m_HotData), 16,
                       sizeof(InstanceHotData) * maxInstances);
        posix_memalign(reinterpret_cast<void**>(&m_LocalPoseTime), 16,
                       sizeof(float32) * maxInstances);
        posix_memalign(reinterpret_cast<void**>(&m_Instances), 16,
                       sizeof(AnimationInstance*) * maxInstances);
#endif

        // 初始化
        std::memset(m_HotData, 0, sizeof(InstanceHotData) * maxInstances);
        std::memset(m_LocalPoseTime, 0, sizeof(float32) * maxInstances);
        std::memset(m_Instances, 0, sizeof(AnimationInstance*) * maxInstances);

        // 预分配 SoA 姿势缓冲区
        m_PoseBuffer.Resize(maxInstances, maxBones);
    }

    AnimationBatch::~AnimationBatch() {
#ifdef _MSC_VER
        _aligned_free(m_HotData);
        _aligned_free(m_LocalPoseTime);
        _aligned_free(m_Instances);
#else
        std::free(m_HotData);
        std::free(m_LocalPoseTime);
        std::free(m_Instances);
#endif
    }

    // ============================================================
    // 实例管理
    // ============================================================

    int32 AnimationBatch::AddInstance(AnimationInstance* instance) {
        if (!instance || m_InstanceCount >= m_MaxInstances) return -1;

        int32 idx = m_InstanceCount++;
        m_Instances[idx] = instance;

        // 初始化热数据
        auto& hot = m_HotData[idx];
        hot.localTime   = 0.0f;
        hot.timeScale   = 1.0f;
        hot.blendWeight = 1.0f;
        hot.state       = static_cast<uint8>(TimelineState::Playing);
        hot.loopMode    = static_cast<uint8>(AnimationLoopMode::Loop);

        return idx;
    }

    void AnimationBatch::RemoveInstance(int32 index) {
        if (index < 0 || index >= m_InstanceCount) return;

        // 用最后一个实例填补空缺
        m_InstanceCount--;
        if (index < m_InstanceCount) {
            m_Instances[index] = m_Instances[m_InstanceCount];
            m_HotData[index] = m_HotData[m_InstanceCount];
            m_LocalPoseTime[index] = m_LocalPoseTime[m_InstanceCount];
        }
    }

    void AnimationBatch::RemoveInstance(AnimationInstance* instance) {
        for (int32 i = 0; i < m_InstanceCount; ++i) {
            if (m_Instances[i] == instance) {
                RemoveInstance(i);
                return;
            }
        }
    }

    void AnimationBatch::Clear() {
        m_InstanceCount = 0;
    }

    // ============================================================
    // 热数据收集 / 结果回写
    // ============================================================

    void AnimationBatch::GatherHotData() {
        for (int32 i = 0; i < m_InstanceCount; ++i) {
            auto* inst = m_Instances[i];
            if (!inst) continue;

            auto& hot = m_HotData[i];

            // 收集片段状态（使用第一个活跃片段）
            const auto& clipStates = inst->GetClipStates();
            if (!clipStates.empty()) {
                const auto& cs = clipStates[0];
                hot.localTime   = cs.localTime;
                hot.timeScale   = cs.timeScale;
                hot.state       = static_cast<uint8>(cs.state);
                hot.loopMode    = static_cast<uint8>(cs.loop);
            }

            // 收集混合权重（使用第一个混合层）
            const auto& blendSpec = inst->GetBlendSpec();
            if (!blendSpec.IsEmpty()) {
                hot.blendWeight = blendSpec.layers[0].weight;
            }
        }
    }

    void AnimationBatch::ScatterResults() {
        for (int32 i = 0; i < m_InstanceCount; ++i) {
            auto* inst = m_Instances[i];
            if (!inst) continue;

            // 将 SoA 缓冲区中的姿势写回实例
            auto& localPose = inst->GetLocalPose();
            const auto& skeleton = *inst->GetResource()->skeleton;
            size_t boneCount = skeleton.GetBoneCount();

            // 从 SoA 读取并写入实例的 AoS PoseLocalData
            for (size_t b = 0; b < boneCount; ++b) {
                localPose.translations[b] = m_PoseBuffer.GetTranslation(i, static_cast<int32>(b));
                localPose.rotations[b]    = m_PoseBuffer.GetRotation(i, static_cast<int32>(b));
                localPose.scales[b]       = m_PoseBuffer.GetScale(i, static_cast<int32>(b));
            }

            // 写回片段状态
            auto& clipStates = inst->GetClipStates();
            if (!clipStates.empty()) {
                auto& cs = clipStates[0];
                cs.localTime = m_HotData[i].localTime;
                cs.state     = static_cast<TimelineState>(m_HotData[i].state);
            }

            // 触发完整的全局姿势 + 矩阵调色板生成
            inst->GenerateGlobalPose();
            inst->GenerateMatrixPalette();
        }
    }

    // ============================================================
    // 批量更新时间线
    // ============================================================

    void AnimationBatch::BatchUpdateTimelines(float32 dt) {
        // 缓存友好：连续遍历热数据数组
        for (int32 i = 0; i < m_InstanceCount; ++i) {
            auto& hot = m_HotData[i];
            if (hot.state != static_cast<uint8>(TimelineState::Playing))
                continue;

            // 推进时间
            hot.localTime += dt * hot.timeScale;

            // 获取骨骼和资源信息
            auto* inst = m_Instances[i];
            if (!inst) continue;
            auto resource = inst->GetResource();
            if (!resource) continue;

            // 获取片段时长（简化：使用第一个活跃片段）
            const auto& clipStates = inst->GetClipStates();
            if (clipStates.empty()) continue;

            auto clip = resource->GetClip(clipStates[0].clipName);
            float32 duration = clip ? clip->GetDuration() : 0.0f;
            if (duration <= 0.0f) continue;

            // 循环处理
            auto loop = static_cast<AnimationLoopMode>(hot.loopMode);
            switch (loop) {
                case AnimationLoopMode::Once:
                    if (hot.localTime >= duration) {
                        hot.localTime = duration;
                        hot.state = static_cast<uint8>(TimelineState::Stopped);
                    }
                    break;
                case AnimationLoopMode::Loop:
                    if (hot.localTime >= duration)
                        hot.localTime = std::fmod(hot.localTime, duration);
                    break;
                case AnimationLoopMode::PingPong: {
                    float32 cycle = std::fmod(hot.localTime, duration * 2.0f);
                    hot.localTime = (cycle < duration) ? cycle : duration * 2.0f - cycle;
                    break;
                }
            }
        }
    }

    // ============================================================
    // 批量更新主入口
    // ============================================================

    void AnimationBatch::UpdateAll(float32 dt) {
        if (m_InstanceCount == 0) return;

        // 1. 收集热数据（实例 → SoA 缓冲区）
        GatherHotData();

        // 2. 批量更新时间线
        BatchUpdateTimelines(dt);

        // 3. 批量解压姿势到 SoA 缓冲区
        //    对每个实例，从 clip 提取姿势并存入 SoA
        for (int32 i = 0; i < m_InstanceCount; ++i) {
            auto* inst = m_Instances[i];
            if (!inst || !inst->GetResource()) continue;

            // 使用实例的 ExtractPoses 方法求值
            inst->ExtractPoses();

            // 将 AoS 结果复制到 SoA 缓冲区
            const auto& localPose = inst->GetLocalPose();
            size_t boneCount = localPose.GetBoneCount();
            for (size_t b = 0; b < boneCount; ++b) {
                m_PoseBuffer.SetTranslation(i, static_cast<int32>(b), localPose.translations[b]);
                m_PoseBuffer.SetRotation(i, static_cast<int32>(b), localPose.rotations[b]);
                m_PoseBuffer.SetScale(i, static_cast<int32>(b), localPose.scales[b]);
            }
        }

        // 4. 批量混合（对 SoA 数据做 LERP/SLERP）
        //    TODO: 后续可用 SIMD 一次性处理 4 个实例的同一根骨骼
        for (int32 i = 0; i < m_InstanceCount; ++i) {
            auto* inst = m_Instances[i];
            if (!inst) continue;

            // 如果有多个混合层，执行批量混合
            const auto& spec = inst->GetBlendSpec();
            if (spec.GetLayerCount() > 1) {
                // 对后续层混合
                for (size_t layer = 1; layer < spec.GetLayerCount(); ++layer) {
                    const auto& layerData = spec.layers[layer];
                    auto clip = inst->GetResource()->GetClip(layerData.clipName);
                    if (!clip) continue;

                    // 求值该层
                    PoseLocalData layerPose;
                    layerPose.Resize(inst->GetLocalPose().GetBoneCount());

                    float32 savedTime = clip->GetLocalTime();
                    float32 sampleTime = m_HotData[i].localTime;

                    // 使用 AnimationPose 求值（简化处理）
                    AnimationPose poseEval(inst->GetResource()->skeleton);
                    const_cast<AnimationLocalTimeline*>(clip.get())->Seek(sampleTime);
                    poseEval.EvaluateFromTimeline(*clip);

                    for (size_t b = 0; b < inst->GetLocalPose().GetBoneCount(); ++b) {
                        const Bone& bone = inst->GetResource()->skeleton->GetBone(static_cast<int32>(b));
                        layerPose.translations[b] = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
                        layerPose.rotations[b]    = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
                        layerPose.scales[b]       = AnimationBlend::DecomposeScale(bone.localPoseMatrix);
                    }
                    const_cast<AnimationLocalTimeline*>(clip.get())->Seek(savedTime);

                    // 混合到 SoA 缓冲区
                    float32 w = layerData.weight;
                    for (size_t b = 0; b < inst->GetLocalPose().GetBoneCount(); ++b) {
                        int32 idx = m_PoseBuffer.Index(i, static_cast<int32>(b));
                        // SIMD 预留：此处可用 _mm_add_ps/_mm_mul_ps 等
                        m_PoseBuffer.transX[idx] = AnimationBlend::LERP(
                            m_PoseBuffer.transX[idx], layerPose.translations[b].x, w);
                        m_PoseBuffer.transY[idx] = AnimationBlend::LERP(
                            m_PoseBuffer.transY[idx], layerPose.translations[b].y, w);
                        m_PoseBuffer.transZ[idx] = AnimationBlend::LERP(
                            m_PoseBuffer.transZ[idx], layerPose.translations[b].z, w);

                        // 旋转：SLERP（SIMD 预留）
                        Quat blended = AnimationBlend::SLERP(
                            m_PoseBuffer.GetRotation(i, static_cast<int32>(b)),
                            layerPose.rotations[b], w);
                        m_PoseBuffer.SetRotation(i, static_cast<int32>(b), blended);

                        m_PoseBuffer.scaleX[idx] = AnimationBlend::LERP(
                            m_PoseBuffer.scaleX[idx], layerPose.scales[b].x, w);
                        m_PoseBuffer.scaleY[idx] = AnimationBlend::LERP(
                            m_PoseBuffer.scaleY[idx], layerPose.scales[b].y, w);
                        m_PoseBuffer.scaleZ[idx] = AnimationBlend::LERP(
                            m_PoseBuffer.scaleZ[idx], layerPose.scales[b].z, w);
                    }
                }
            }
        }

        // 5. 结果回写
        ScatterResults();

        // 更新统计
        m_Stats.instanceCount   = m_InstanceCount;
        m_Stats.poseBufferBytes = m_PoseBuffer.transX.capacity() * sizeof(float32) * 11;
        m_Stats.hotDataBytes    = sizeof(InstanceHotData) * m_MaxInstances;
    }

} // namespace Engine
