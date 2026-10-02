/**
 * @file AnimationCompression.cpp
 * @brief 动画数据压缩实现 — 量化、通道精简、曲线压缩、重采样
 */

#include "Engine/Animation/AnimationCompression.h"
#include "Engine/Animation/AnimationTrack.h"
#include "Engine/Animation/AnimationLocalTimeline.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Engine {

    // ════════════════════════════════════════════
    // 量化工具
    // ════════════════════════════════════════════

    uint16 AnimationCompressor::QuantizeFloat32(float32 value, float32 minVal,
                                                 float32 maxVal, int32 bits) {
        if (bits <= 0) return 0;

        uint32 maxQ = (bits >= 16) ? 0xFFFF : (static_cast<uint32>(1 << bits) - 1);
        float32 range = maxVal - minVal;
        if (range < 1e-10f) return maxQ / 2;  // 中值

        // 钳制 + 归一化
        float32 t = (value - minVal) / range;
        t = std::max(0.0f, std::min(1.0f, t));
        return static_cast<uint16>(std::round(t * static_cast<float32>(maxQ)));
    }

    float32 AnimationCompressor::DequantizeUint16(uint16 q, float32 minVal,
                                                   float32 maxVal, int32 bits) {
        if (bits <= 0) return minVal;

        uint32 maxQ = (bits >= 16) ? 0xFFFF : (static_cast<uint32>(1 << bits) - 1);
        float32 t = static_cast<float32>(q) / static_cast<float32>(maxQ);
        return minVal + t * (maxVal - minVal);
    }

    QuantizedRange AnimationCompressor::ComputeRange(
        const std::vector<KeyFrameFloat>& keys, int32 componentIndex) {
        (void)componentIndex;
        if (keys.empty()) return QuantizedRange(0.0f, 1.0f);

        float32 minVal = std::numeric_limits<float32>::max();
        float32 maxVal = std::numeric_limits<float32>::lowest();
        for (const auto& kf : keys) {
            if (kf.value < minVal) minVal = kf.value;
            if (kf.value > maxVal) maxVal = kf.value;
        }

        // 防止 min == max
        if (maxVal - minVal < 1e-10f) {
            minVal -= 0.5f;
            maxVal += 0.5f;
        }

        return QuantizedRange(minVal, maxVal);
    }

namespace {
    // The SINGLE implementation of per-component range computation.
    // Both ComputeComponentRanges (public API) and ProcessKeyTrack route
    // through here, so the two cannot drift apart over time.
    template <typename KeyVec>
    std::vector<QuantizedRange> ComputeRangesFromKeys(const KeyVec& keys) {
        std::vector<QuantizedRange> ranges;
        if (keys.empty()) return ranges;

        using ValueType = std::decay_t<decltype(keys[0].value)>;
        constexpr bool kIsScalar = std::is_same_v<ValueType, float32>;
        constexpr int32 kComps = kIsScalar
            ? 1
            : static_cast<int32>(sizeof(ValueType) / sizeof(float32));

        for (int32 c = 0; c < kComps; ++c) {
            auto componentOf = [&](const ValueType& v) -> float32 {
                if constexpr (kIsScalar) {
                    (void)c;
                    return v;
                } else {
                    return (c < kComps) ? v[static_cast<size_t>(c)] : 0.0f;
                }
            };

            float32 mn = componentOf(keys[0].value);
            float32 mx = mn;
            for (const auto& k : keys) {
                const float32 v = componentOf(k.value);
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
            // Constant component: widen to a unit range to avoid a zero step.
            if (mx - mn < 1e-10f) {
                mn -= 0.5f;
                mx += 0.5f;
            }
            ranges.emplace_back(mn, mx);
        }
        return ranges;
    }
} // namespace

namespace {
    // Compress one track: decimate -> component ranges -> quantize per frame.
    // This is the ONLY processing path for CompressTrack. The previous
    // implementation declared a generic lambda holding this logic but never
    // invoked it, so every input compressed to zero frames.
    template <typename KeyVec>
    void ProcessKeyTrack(CompressedTrack& out,
                         const KeyVec& keys,
                         const AnimationCompressionSettings& settings,
                         int32 bits) {
        if (keys.empty()) return;

        using KeyType = std::decay_t<decltype(keys[0])>;
        using ValueType = std::decay_t<decltype(keys[0].value)>;
        constexpr bool kIsScalar = std::is_same_v<ValueType, float32>;
        constexpr int32 kComps = kIsScalar
            ? 1
            : static_cast<int32>(sizeof(ValueType) / sizeof(float32));

        auto componentOf = [](const ValueType& v, int32 c) -> float32 {
            if constexpr (kIsScalar) {
                (void)c;
                return v;
            } else {
                return (c < kComps) ? v[static_cast<size_t>(c)] : 0.0f;
            }
        };

        std::vector<KeyType> processed = keys;

        // Decimation: scalar tracks only. There is no defined fidelity
        // criterion for multi-component tracks; decimating on component 0
        // would silently discard motion in the other components.
        if constexpr (kIsScalar) {
            if (settings.enableDecimation && processed.size() > 2) {
                std::vector<KeyFrameFloat> scalar;
                scalar.reserve(processed.size());
                for (const auto& k : processed) {
                    scalar.push_back(KeyFrameFloat{k.time, k.value});
                }
                const auto decimated = AnimationCompressor::DecimateKeyFrames(
                    scalar, settings.decimationTolerance,
                    AnimationInterpolation::Linear);
                if (decimated.size() >= 2 && decimated.size() < processed.size()) {
                    std::vector<KeyType> kept;
                    kept.reserve(decimated.size());
                    for (const auto& dk : decimated) {
                        for (const auto& ok : processed) {
                            if (std::abs(ok.time - dk.time) < 1e-6f) {
                                kept.push_back(ok);
                                break;
                            }
                        }
                    }
                    if (kept.size() >= 2) processed = kept;
                }
            }
        }

        // Time range: keys are already sorted by time (AddKeyFrame sorts).
        const float32 tMin = processed.front().time;
        const float32 tMax = processed.back().time;
        out.SetTimeRange(QuantizedRange(tMin, tMax));

        // Component ranges: shares ComputeRangesFromKeys with
        // ComputeComponentRanges so the public API and the compression path
        // cannot drift apart.
        out.GetComponentRanges() = ComputeRangesFromKeys(processed);

        // Quantize each frame
        for (const auto& k : processed) {
            QuantizedKeyFrame16 qk;
            for (int32 c = 0; c < 4; ++c) { qk.components[c] = 0; }
            qk.time = AnimationCompressor::QuantizeFloat32(k.time, tMin, tMax, bits);
            const auto& compRanges = out.GetComponentRanges();
            for (int32 c = 0; c < kComps; ++c) {
                qk.components[c] = AnimationCompressor::QuantizeFloat32(
                    componentOf(k.value, c),
                    compRanges[c].minVal, compRanges[c].maxVal, bits);
            }
            out.GetQuantizedFrames().push_back(qk);
        }
    }
} // namespace
    std::vector<QuantizedRange> AnimationCompressor::ComputeComponentRanges(
        const AnimationTrack& track) {
        // Routes through ComputeRangesFromKeys (defined above), which is the
        // single implementation shared with CompressTrack.
        switch (track.GetPropertyType()) {
            case AnimationPropertyType::Float:
                return ComputeRangesFromKeys(track.GetFloatKeys());
            case AnimationPropertyType::Vec2:
                return ComputeRangesFromKeys(track.GetVec2Keys());
            case AnimationPropertyType::Vec3:
                return ComputeRangesFromKeys(track.GetVec3Keys());
            case AnimationPropertyType::Vec4:
                return ComputeRangesFromKeys(track.GetVec4Keys());
        }
        return {};
    }
    // ════════════════════════════════════════════
    // 常数轨道检测
    // ════════════════════════════════════════════

    bool AnimationCompressor::IsConstantTrack(const AnimationTrack& track,
                                               float32 tolerance) {
        if (track.IsEmpty()) return true;

        // 采样几个时间点检查值是否一致
        float32 dur = track.GetDuration();
        if (dur <= 0.0f) return true;

        float32 refVal = 0.0f;
        bool first = true;

        for (int32 i = 0; i <= 10; ++i) {
            float32 t = dur * static_cast<float32>(i) / 10.0f;
            float32 v = 0.0f;

            switch (track.GetPropertyType()) {
                case AnimationPropertyType::Float:
                    v = track.EvaluateFloat(t);
                    break;
                case AnimationPropertyType::Vec2: {
                    Vec2 v2 = track.EvaluateVec2(t);
                    v = v2.x + v2.y;
                    break;
                }
                case AnimationPropertyType::Vec3: {
                    Vec3 v3 = track.EvaluateVec3(t);
                    v = v3.x + v3.y + v3.z;
                    break;
                }
                case AnimationPropertyType::Vec4: {
                    Vec4 v4 = track.EvaluateVec4(t);
                    v = v4.x + v4.y + v4.z + v4.w;
                    break;
                }
            }

            if (first) {
                refVal = v;
                first = false;
            } else if (std::abs(v - refVal) > tolerance) {
                return false;
            }
        }
        return true;
    }

    // ════════════════════════════════════════════
    // 关键帧降采样 (Ramer-Douglas-Peucker style)
    // ════════════════════════════════════════════

    std::vector<KeyFrameFloat> AnimationCompressor::DecimateKeyFrames(
        const std::vector<KeyFrameFloat>& keys,
        float32 tolerance,
        AnimationInterpolation interpMode) {
        if (keys.size() <= 2) return keys;

        // 递归简化：找到离线段最远的点
        auto findFurthest = [&](int32 start, int32 end) -> int32 {
            float32 tStart = keys[start].time;
            float32 tEnd   = keys[end].time;
            float32 vStart = keys[start].value;
            float32 vEnd   = keys[end].value;

            float32 maxDist = 0.0f;
            int32 maxIdx = -1;

            for (int32 i = start + 1; i < end; ++i) {
                float32 t = (keys[i].time - tStart) / (tEnd - tStart + 1e-10f);
                float32 expected;
                if (interpMode == AnimationInterpolation::Smooth) {
                    float32 s = t * t * (3.0f - 2.0f * t);  // smoothstep
                    expected = vStart + (vEnd - vStart) * s;
                } else {
                    expected = vStart + (vEnd - vStart) * t;
                }
                float32 dist = std::abs(keys[i].value - expected);
                if (dist > maxDist) {
                    maxDist = dist;
                    maxIdx = i;
                }
            }
            return (maxDist > tolerance) ? maxIdx : -1;
        };

        // 栈式简化（非递归，避免栈溢出）
        struct Segment { int32 start, end; };
        std::vector<Segment> stack;
        std::vector<bool> keep(keys.size(), false);
        keep[0] = keep[keys.size() - 1] = true;
        stack.push_back({0, static_cast<int32>(keys.size()) - 1});

        while (!stack.empty()) {
            auto seg = stack.back();
            stack.pop_back();
            int32 idx = findFurthest(seg.start, seg.end);
            if (idx >= 0) {
                keep[idx] = true;
                stack.push_back({seg.start, idx});
                stack.push_back({idx, seg.end});
            }
        }

        std::vector<KeyFrameFloat> result;
        for (size_t i = 0; i < keys.size(); ++i) {
            if (keep[i]) result.push_back(keys[i]);
        }
        return result;
    }

    // ════════════════════════════════════════════
    // Hermite 曲线拟合
    // ════════════════════════════════════════════

    std::vector<HermiteControlPoint> AnimationCompressor::FitHermiteCurve(
        const std::vector<KeyFrameFloat>& keys,
        float32 tolerance) {
        if (keys.size() <= 1) {
            std::vector<HermiteControlPoint> pts;
            for (const auto& k : keys) {
                pts.push_back({k.time, k.value, 0.0f, 0.0f});
            }
            return pts;
        }

        // 先降采样
        auto decimated = DecimateKeyFrames(keys, tolerance, AnimationInterpolation::Smooth);
        if (decimated.size() < 2) {
            decimated = {keys.front(), keys.back()};
        }

        // 计算切线（Catmull-Rom 风格：切线 = (next - prev) / 2）
        std::vector<HermiteControlPoint> result;
        for (size_t i = 0; i < decimated.size(); ++i) {
            HermiteControlPoint cp;
            cp.time = decimated[i].time;
            cp.value = decimated[i].value;

            if (i == 0) {
                cp.inTangent = 0.0f;
                cp.outTangent = (decimated.size() > 1)
                    ? (decimated[1].value - decimated[0].value) / (decimated[1].time - decimated[0].time + 1e-10f)
                    : 0.0f;
            } else if (i == decimated.size() - 1) {
                cp.inTangent = (decimated[i].value - decimated[i-1].value)
                    / (decimated[i].time - decimated[i-1].time + 1e-10f);
                cp.outTangent = 0.0f;
            } else {
                // Catmull-Rom 切线
                float32 dtPrev = decimated[i].time - decimated[i-1].time;
                float32 dtNext = decimated[i+1].time - decimated[i].time;
                float32 dvPrev = decimated[i].value - decimated[i-1].value;
                float32 dvNext = decimated[i+1].value - decimated[i].value;
                cp.inTangent = (dvPrev / dtPrev + dvNext / dtNext) * 0.5f;
                cp.outTangent = cp.inTangent;
            }

            result.push_back(cp);
        }

        return result;
    }

    // ════════════════════════════════════════════
    // 重采样
    // ════════════════════════════════════════════

    AnimationTrack AnimationCompressor::ResampleTrack(const AnimationTrack& track,
                                                       float32 sampleRate) {
        AnimationTrack result;
        result.SetPropertyType(track.GetPropertyType());
        result.SetInterpolation(AnimationInterpolation::Linear);

        float32 dur = track.GetDuration();
        if (dur <= 0.0f || sampleRate <= 0.0f) return result;

        float32 dt = 1.0f / sampleRate;
        int32 numFrames = static_cast<int32>(std::ceil(dur / dt)) + 1;

        switch (track.GetPropertyType()) {
            case AnimationPropertyType::Float:
                for (int32 i = 0; i < numFrames; ++i) {
                    float32 t = std::min(static_cast<float32>(i) * dt, dur);
                    result.AddKeyFrame(KeyFrameFloat{t, track.EvaluateFloat(t)});
                }
                break;

            case AnimationPropertyType::Vec2:
                for (int32 i = 0; i < numFrames; ++i) {
                    float32 t = std::min(static_cast<float32>(i) * dt, dur);
                    result.AddKeyFrame(KeyFrameVec2{t, track.EvaluateVec2(t)});
                }
                break;

            case AnimationPropertyType::Vec3:
                for (int32 i = 0; i < numFrames; ++i) {
                    float32 t = std::min(static_cast<float32>(i) * dt, dur);
                    result.AddKeyFrame(KeyFrameVec3{t, track.EvaluateVec3(t)});
                }
                break;

            case AnimationPropertyType::Vec4:
                for (int32 i = 0; i < numFrames; ++i) {
                    float32 t = std::min(static_cast<float32>(i) * dt, dur);
                    result.AddKeyFrame(KeyFrameVec4{t, track.EvaluateVec4(t)});
                }
                break;
        }

        return result;
    }

    // ════════════════════════════════════════════
    // 轨道压缩（核心管线）
    // ════════════════════════════════════════════

    CompressedTrack AnimationCompressor::CompressTrack(
        const AnimationTrack& track,
        const AnimationCompressionSettings& settings) {

        CompressedTrack compressed;
        compressed.SetPropertyType(track.GetPropertyType());
        compressed.SetInterpolation(track.GetInterpolation());

        if (track.IsEmpty()) return compressed;

        // The CompressedTrack storage format IS quantized frames; there is no
        // "raw frame" representation to fall back to. Non-quantized mode is
        // therefore normalized to 16-bit quantization.
        //
        // CompressedTrack::DequantizeComponent always reinterprets the stored
        // uint16 as 16-bit, so the encode width must match or the round-trip
        // values would be decoded with the wrong scale.
        AnimationCompressionSettings effective = settings;
        effective.enableQuantization = true;
        effective.quantizeBits = 16;
        const int32 bits = effective.quantizeBits;

        // Resampling (if enabled)
        AnimationTrack working = track;
        compressed.SetOriginalSampleRate(30.0f);
        if (effective.enableResampling && effective.targetSampleRate > 0.0f) {
            working = ResampleTrack(track, effective.targetSampleRate);
            compressed.SetOriginalSampleRate(effective.targetSampleRate);
        }

        const float32 duration = working.GetDuration();

        // Dispatch to the keyframe container that matches the property type
        switch (track.GetPropertyType()) {
            case AnimationPropertyType::Float:
                ProcessKeyTrack(compressed, working.GetFloatKeys(), effective, bits);
                break;
            case AnimationPropertyType::Vec2:
                ProcessKeyTrack(compressed, working.GetVec2Keys(), effective, bits);
                break;
            case AnimationPropertyType::Vec3:
                ProcessKeyTrack(compressed, working.GetVec3Keys(), effective, bits);
                break;
            case AnimationPropertyType::Vec4:
                ProcessKeyTrack(compressed, working.GetVec4Keys(), effective, bits);
                break;
        }

        compressed.SetNumFrames(static_cast<int32>(compressed.GetQuantizedFrames().size()));
        compressed.SetDuration(duration);

        return compressed;
    }
    // ════════════════════════════════════════════
    // 时间线压缩
    // ════════════════════════════════════════════

    CompressedAnimationClip AnimationCompressor::CompressTimeline(
        const AnimationLocalTimeline& timeline,
        const AnimationCompressionSettings& settings) {

        CompressedAnimationClip clip;
        clip.name = timeline.GetName();
        clip.duration = timeline.GetDuration();
        clip.loopMode = timeline.GetLoopMode();

        // 压缩各轨道
        // 由于 CompressTrack 需要访问原始关键帧数据，但我们的 AnimationTrack
        // 没有直接暴露 const 引用的接口，这里通过求值后重采样的方式压缩

        // 位置轨道
        if (!timeline.GetPositionTrack().IsEmpty()) {
            clip.positionTrack = CompressTrack(timeline.GetPositionTrack(), settings);
        }

        // 旋转轨道
        if (!timeline.GetRotationTrack().IsEmpty()) {
            clip.rotationTrack = CompressTrack(timeline.GetRotationTrack(), settings);
        }

        // 缩放轨道
        if (!timeline.GetScaleTrack().IsEmpty()) {
            clip.scaleTrack = CompressTrack(timeline.GetScaleTrack(), settings);
        }

        // 颜色轨道
        if (!timeline.GetColorTrack().IsEmpty()) {
            clip.colorTrack = CompressTrack(timeline.GetColorTrack(), settings);
        }

        // 自定义浮点轨道（通过名称获取）
        // 注意：没有公共 API 遍历所有 floatTrack，这里简化处理

        return clip;
    }

    // ════════════════════════════════════════════
    // CompressedTrack 解码求值
    // ════════════════════════════════════════════

    float32 CompressedTrack::DequantizeTime(uint16 qtime) const {
        return AnimationCompressor::DequantizeUint16(qtime, m_TimeRange.minVal, m_TimeRange.maxVal, 16);
    }

    float32 CompressedTrack::DequantizeComponent(uint16 qval, int32 compIdx) const {
        if (compIdx < static_cast<int32>(m_CompRanges.size())) {
            return AnimationCompressor::DequantizeUint16(qval, m_CompRanges[compIdx].minVal,
                                     m_CompRanges[compIdx].maxVal, 16);
        }
        return 0.0f;
    }

    void CompressedTrack::DequantizeFrame(int32 idx, float32& outTime,
                                           float32 outComponents[4]) const {
        if (idx < 0 || idx >= static_cast<int32>(m_QuantizedFrames.size())) return;
        const auto& qf = m_QuantizedFrames[idx];
        outTime = DequantizeTime(qf.time);
        for (int32 c = 0; c < 4; ++c) {
            outComponents[c] = DequantizeComponent(qf.components[c], c);
        }
    }

    float32 CompressedTrack::EvaluateHermite(float32 time) const {
        if (m_HermitePoints.empty()) return 0.0f;
        if (time <= m_HermitePoints.front().time) return m_HermitePoints.front().value;
        if (time >= m_HermitePoints.back().time) return m_HermitePoints.back().value;

        // 找到所在段
        for (size_t i = 0; i < m_HermitePoints.size() - 1; ++i) {
            const auto& p0 = m_HermitePoints[i];
            const auto& p1 = m_HermitePoints[i + 1];
            if (time >= p0.time && time <= p1.time) {
                float32 dt = p1.time - p0.time;
                if (dt < 1e-10f) return p0.value;
                float32 t = (time - p0.time) / dt;

                // 三次 Hermite 插值
                float32 t2 = t * t;
                float32 t3 = t2 * t;
                float32 h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
                float32 h10 = t3 - 2.0f * t2 + t;
                float32 h01 = -2.0f * t3 + 3.0f * t2;
                float32 h11 = t3 - t2;

                return h00 * p0.value + h10 * p0.outTangent * dt
                     + h01 * p1.value + h11 * p1.inTangent * dt;
            }
        }
        return m_HermitePoints.back().value;
    }

    float32 CompressedTrack::EvaluateFloat(float32 time) const {
        if (m_QuantizedFrames.empty()) return 0.0f;

        // 曲线模式
        if (!m_HermitePoints.empty()) {
            return EvaluateHermite(time);
        }

        // 量化帧模式
        if (m_QuantizedFrames.size() == 1) {
            float32 t; float32 comps[4];
            DequantizeFrame(0, t, comps);
            return comps[0];
        }

        // 二分查找
        auto& frames = m_QuantizedFrames;
        int32 lo = 0, hi = static_cast<int32>(frames.size()) - 1;

        // 边界检测
        float32 t0, t1, v0[4], v1[4];
        DequantizeFrame(0, t0, v0);
        DequantizeFrame(hi, t1, v1);
        if (time <= t0) return v0[0];
        if (time >= t1) return v1[0];

        while (lo < hi - 1) {
            int32 mid = (lo + hi) / 2;
            float32 tm; float32 vm[4];
            DequantizeFrame(mid, tm, vm);
            if (tm < time) lo = mid;
            else hi = mid;
        }

        float32 tLo, tHi, vLo[4], vHi[4];
        DequantizeFrame(lo, tLo, vLo);
        DequantizeFrame(hi, tHi, vHi);

        float32 t = (tHi - tLo > 1e-10f) ? (time - tLo) / (tHi - tLo) : 0.0f;
        return AnimationTrack::Interpolate(t, vLo[0], vHi[0], m_Interpolation);
    }

    Vec2 CompressedTrack::EvaluateVec2(float32 time) const {
        if (m_QuantizedFrames.empty()) return Vec2(0, 0);
        if (m_QuantizedFrames.size() == 1) {
            float32 t; float32 c[4];
            DequantizeFrame(0, t, c);
            return Vec2(c[0], c[1]);
        }

        int32 lo = 0, hi = static_cast<int32>(m_QuantizedFrames.size()) - 1;
        float32 t0, t1, v0[4], v1[4];
        DequantizeFrame(0, t0, v0);
        DequantizeFrame(hi, t1, v1);
        if (time <= t0) return Vec2(v0[0], v0[1]);
        if (time >= t1) return Vec2(v1[0], v1[1]);

        while (lo < hi - 1) {
            int32 mid = (lo + hi) / 2;
            float32 tm; float32 vm[4];
            DequantizeFrame(mid, tm, vm);
            if (tm < time) lo = mid;
            else hi = mid;
        }

        float32 tLo, tHi, vLo[4], vHi[4];
        DequantizeFrame(lo, tLo, vLo);
        DequantizeFrame(hi, tHi, vHi);

        float32 t = (tHi - tLo > 1e-10f) ? (time - tLo) / (tHi - tLo) : 0.0f;
        Vec2 a(vLo[0], vLo[1]), b(vHi[0], vHi[1]);
        return AnimationTrack::Interpolate(t, a, b, m_Interpolation);
    }

    Vec3 CompressedTrack::EvaluateVec3(float32 time) const {
        if (m_QuantizedFrames.empty()) return Vec3(0, 0, 0);
        if (m_QuantizedFrames.size() == 1) {
            float32 t; float32 c[4];
            DequantizeFrame(0, t, c);
            return Vec3(c[0], c[1], c[2]);
        }

        int32 lo = 0, hi = static_cast<int32>(m_QuantizedFrames.size()) - 1;
        float32 t0, t1, v0[4], v1[4];
        DequantizeFrame(0, t0, v0);
        DequantizeFrame(hi, t1, v1);
        if (time <= t0) return Vec3(v0[0], v0[1], v0[2]);
        if (time >= t1) return Vec3(v1[0], v1[1], v1[2]);

        while (lo < hi - 1) {
            int32 mid = (lo + hi) / 2;
            float32 tm; float32 vm[4];
            DequantizeFrame(mid, tm, vm);
            if (tm < time) lo = mid;
            else hi = mid;
        }

        float32 tLo, tHi, vLo[4], vHi[4];
        DequantizeFrame(lo, tLo, vLo);
        DequantizeFrame(hi, tHi, vHi);

        float32 t = (tHi - tLo > 1e-10f) ? (time - tLo) / (tHi - tLo) : 0.0f;
        Vec3 a(vLo[0], vLo[1], vLo[2]), b(vHi[0], vHi[1], vHi[2]);
        return AnimationTrack::Interpolate(t, a, b, m_Interpolation);
    }

    Vec4 CompressedTrack::EvaluateVec4(float32 time) const {
        if (m_QuantizedFrames.empty()) return Vec4(0, 0, 0, 1);
        if (m_QuantizedFrames.size() == 1) {
            float32 t; float32 c[4];
            DequantizeFrame(0, t, c);
            return Vec4(c[0], c[1], c[2], c[3]);
        }

        int32 lo = 0, hi = static_cast<int32>(m_QuantizedFrames.size()) - 1;
        float32 t0, t1, v0[4], v1[4];
        DequantizeFrame(0, t0, v0);
        DequantizeFrame(hi, t1, v1);
        if (time <= t0) return Vec4(v0[0], v0[1], v0[2], v0[3]);
        if (time >= t1) return Vec4(v1[0], v1[1], v1[2], v1[3]);

        while (lo < hi - 1) {
            int32 mid = (lo + hi) / 2;
            float32 tm; float32 vm[4];
            DequantizeFrame(mid, tm, vm);
            if (tm < time) lo = mid;
            else hi = mid;
        }

        float32 tLo, tHi, vLo[4], vHi[4];
        DequantizeFrame(lo, tLo, vLo);
        DequantizeFrame(hi, tHi, vHi);

        float32 t = (tHi - tLo > 1e-10f) ? (time - tLo) / (tHi - tLo) : 0.0f;
        Vec4 a(vLo[0], vLo[1], vLo[2], vLo[3]), b(vHi[0], vHi[1], vHi[2], vHi[3]);
        return AnimationTrack::Interpolate(t, a, b, m_Interpolation);
    }

    // ════════════════════════════════════════════
    // 内存估算
    // ════════════════════════════════════════════

    size_t CompressedTrack::EstimateUncompressedSize() const {
        // 假设 uncompressed 使用 float32 per component + float32 time
        size_t comps = 0;
        switch (m_Type) {
            case AnimationPropertyType::Float: comps = 1; break;
            case AnimationPropertyType::Vec2:  comps = 2; break;
            case AnimationPropertyType::Vec3:  comps = 3; break;
            case AnimationPropertyType::Vec4:  comps = 4; break;
        }
        return m_QuantizedFrames.size() * (sizeof(float32) + comps * sizeof(float32));
    }

    size_t CompressedTrack::GetCompressedSize() const {
        size_t size = sizeof(*this);
        size += m_QuantizedFrames.size() * sizeof(QuantizedKeyFrame16);
        size += m_CompRanges.size() * sizeof(QuantizedRange);
        size += m_HermitePoints.size() * sizeof(HermiteControlPoint);
        return size;
    }

    size_t CompressedAnimationClip::EstimateMemoryUsage() const {
        size_t total = sizeof(*this);
        total += positionTrack.GetCompressedSize();
        total += rotationTrack.GetCompressedSize();
        total += scaleTrack.GetCompressedSize();
        total += colorTrack.GetCompressedSize();
        for (const auto& ft : floatTracks) {
            total += ft.name.capacity() + ft.track.GetCompressedSize();
        }
        return total;
    }

} // namespace Engine
