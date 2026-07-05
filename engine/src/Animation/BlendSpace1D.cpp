/**
 * @file BlendSpace1D.cpp
 * @brief 1D 混合空间实现
 */

#include "Engine/Animation/BlendSpace1D.h"
#include <algorithm>
#include <cmath>

namespace Engine {

    // ============================================================
    // 采样点管理
    // ============================================================

    void BlendSpace1D::AddSample(float32 paramValue,
                                  std::shared_ptr<AnimationLocalTimeline> clip) {
        if (!clip) return;

        // 如果已存在相同参数值，替换
        for (auto& sample : m_Samples) {
            if (std::abs(sample.parameterValue - paramValue) < 1e-6f) {
                sample.clip = std::move(clip);
                return;
            }
        }

        // 添加新采样点
        m_Samples.emplace_back(paramValue, std::move(clip));

        // 按参数值排序
        std::sort(m_Samples.begin(), m_Samples.end(),
                  [](const BlendSpace1DSample& a, const BlendSpace1DSample& b) {
                      return a.parameterValue < b.parameterValue;
                  });
    }

    void BlendSpace1D::RemoveSample(int32 index) {
        if (index >= 0 && index < static_cast<int32>(m_Samples.size())) {
            m_Samples.erase(m_Samples.begin() + index);
        }
    }

    void BlendSpace1D::Clear() {
        m_Samples.clear();
    }

    const BlendSpace1DSample& BlendSpace1D::GetSample(int32 index) const {
        return m_Samples[index];
    }

    BlendSpace1DSample& BlendSpace1D::GetSample(int32 index) {
        return m_Samples[index];
    }

    // ============================================================
    // 参数范围
    // ============================================================

    float32 BlendSpace1D::GetMinParameter() const noexcept {
        if (m_Samples.empty()) return 0.0f;
        return m_Samples.front().parameterValue;
    }

    float32 BlendSpace1D::GetMaxParameter() const noexcept {
        if (m_Samples.empty()) return 0.0f;
        return m_Samples.back().parameterValue;
    }

    float32 BlendSpace1D::NormalizeParameter(float32 paramValue) const {
        float32 minVal = GetMinParameter();
        float32 maxVal = GetMaxParameter();
        float32 range = maxVal - minVal;
        if (range < 1e-6f) return 0.0f;
        return (paramValue - minVal) / range;
    }

    // ============================================================
    // 内部：查找相邻采样点
    // ============================================================

    int32 BlendSpace1D::FindNeighbors(float32 paramValue,
                                       int32& outLo, int32& outHi) const {
        if (m_Samples.empty()) {
            outLo = outHi = -1;
            return 0;
        }

        // 只有一个采样点
        if (m_Samples.size() == 1) {
            outLo = outHi = 0;
            return 1;
        }

        // 边界：小于最小值 → 用第一个
        if (paramValue <= m_Samples.front().parameterValue) {
            outLo = outHi = 0;
            return 1;
        }

        // 边界：大于最大值 → 用最后一个
        if (paramValue >= m_Samples.back().parameterValue) {
            outLo = outHi = static_cast<int32>(m_Samples.size()) - 1;
            return 1;
        }

        // 二分查找：找到第一个 parameterValue ≥ paramValue 的采样点
        auto it = std::lower_bound(
            m_Samples.begin(), m_Samples.end(), paramValue,
            [](const BlendSpace1DSample& s, float32 val) {
                return s.parameterValue < val;
            });

        outHi = static_cast<int32>(it - m_Samples.begin());
        outLo = outHi - 1;

        // 如果恰好落在某个采样点上
        if (std::abs(m_Samples[outHi].parameterValue - paramValue) < 1e-6f) {
            outLo = outHi;
            return 1;
        }

        return 2;
    }

    // ============================================================
    // 核心采样
    // ============================================================

    void BlendSpace1D::Sample(Skeleton& skeleton,
                               AnimationPose& poseEval,
                               float32 paramValue,
                               float32 time) {
        // ── 查找相邻采样点 ──
        int32 loIdx = -1, hiIdx = -1;
        int32 sampleCount = FindNeighbors(paramValue, loIdx, hiIdx);

        if (sampleCount == 0) {
            return;  // 无采样点
        }

        if (sampleCount == 1) {
            // ── 纯态：只有一个采样点，或参数在边界外 ──
            const auto& sample = m_Samples[loIdx];
            if (sample.clip) {
                // 在指定时间点求值
                // 注意：这会修改 clip 的内部时间，我们用 seek 方式处理
                float32 savedTime = sample.clip->GetLocalTime();
                sample.clip->Seek(time);
                poseEval.EvaluateAndUpdate(*sample.clip);
                sample.clip->Seek(savedTime);  // 恢复
            }
            return;
        }

        // ── 有两个采样点：做 1D 线性混合 ──
        const auto& sampleLo = m_Samples[loIdx];
        const auto& sampleHi = m_Samples[hiIdx];

        // 计算混合权重
        float32 paramRange = sampleHi.parameterValue - sampleLo.parameterValue;
        float32 t = (paramRange > 1e-6f)
                        ? (paramValue - sampleLo.parameterValue) / paramRange
                        : 0.0f;

        // 钳制到 [0, 1]
        t = std::max(0.0f, std::min(1.0f, t));

        // 使用 AnimationBlend::BlendTimelinePose 做混合
        AnimationBlend::BlendTimelinePose(
            skeleton, poseEval,
            *sampleLo.clip,    // t=0 时完全为此姿势
            *sampleHi.clip,    // t=1 时完全为此姿势
            t,                 // 混合权重
            time               // 两个时间线在同一时间点求值
        );

        // BlendTimelinePose 内部已调用 skeleton.UpdateWorldPoses()
    }

} // namespace Engine
