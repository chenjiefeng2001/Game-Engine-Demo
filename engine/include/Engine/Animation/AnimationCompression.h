#pragma once

/**
 * @file AnimationCompression.h
 * @brief 动画数据压缩 — 量化、通道精简、曲线压缩、采样率重采样
 *
 * ════════════════════════════════════════════
 * 压缩策略总览
 * ════════════════════════════════════════════
 *
 * 1. 通道精简 (Channel Reduction)
 *    - 移除无变化的关键帧轨道（如没有缩放变化的骨骼不存储 Scale 轨道）
 *    - 对每个轨道，检测是否所有值相接近常数，若是则移除
 *
 * 2. 关键帧降采样 (Keyframe Decimation)
 *    - 使用用户指定的最大误差容限，移除可被插值逼近的冗余关键帧
 *    - 基于简化曲线算法（类似 Ramer-Douglas-Peucker）
 *
 * 3. 量化 (Quantization)
 *    - 时间量化：时间归一化到 [0, 65535] 范围，存储为 uint16
 *    - 值量化：每个分量独立归一化到 [-1, 1] 或 [0, 1]，存储为 uint16
 *    - 旋转量化：对四元数使用最小的 3 个分量 + 符号位恢复（可选）
 *
 * 4. 曲线压缩 (Curve-based Compression)
 *    - 将关键帧序列拟合为分段三次 Hermite 曲线
 *    - 只存储曲线断点（knots）而不是所有关键帧
 *    - 运行时用 Hermite 插值重建
 *
 * 5. 自定义采样率 (Variable Sample Rate)
 *    - 用户指定目标采样率（如 30fps）
 *    - 在压缩时重采样动画数据到目标帧率
 *    - 解码时按原采样率线性/平滑插值
 *
 * ════════════════════════════════════════════
 * 使用示例
 * ════════════════════════════════════════════
 *
 *   AnimationCompressionSettings settings;
 *   settings.enableQuantization   = true;
 *   settings.enableDecimation     = true;
 *   settings.decimationTolerance  = 0.001f;
 *   settings.targetSampleRate     = 30.0f;
 *   settings.quantizeBits         = 16;
 *
 *   auto compressed = AnimationCompressor::CompressTrack(
 *       originalTrack, settings);
 *
 *   // 解码（运行时）
 *   float value = compressed.EvaluateFloat(time);
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Animation/AnimationKeyFrame.h"
#include "Engine/Animation/AnimationTrack.h"
#include <vector>
#include <cstdint>
#include <limits>
#include <cmath>

namespace Engine {

    class AnimationLocalTimeline;  // 前向声明

    // ============================================================
    // 压缩配置
    // ============================================================
    struct AnimationCompressionSettings {
        // ── 通道精简 ──
        bool enableChannelReduction = true;     ///< 移除无变化的冗余轨道
        bool removeConstantTracks   = true;     ///< 移除常数值轨道

        // ── 关键帧降采样 ──
        bool enableDecimation       = false;     ///< 启用关键帧降采样
        float32 decimationTolerance = 0.001f;    ///< 降采样最大误差容限（归一化后）

        // ── 量化 ──
        bool enableQuantization     = true;      ///< 启用量化
        int32 quantizeBits          = 16;        ///< 量化位数（8/16）
        bool enableTimeQuantization = true;      ///< 时间轴量化

        // ── 曲线压缩 ──
        bool enableCurveCompression = false;     ///< 启用曲线拟合压缩
        float32 curveErrorTolerance = 0.005f;    ///< 曲线拟合误差容限

        // ── 采样率 ──
        bool enableResampling       = false;     ///< 启用重采样
        float32 targetSampleRate    = 30.0f;     ///< 目标采样率（帧/秒）
    };

    // ============================================================
    // 量化后的关键帧（每个分量 uint16）
    // ============================================================
    struct QuantizedKeyFrame16 {
        uint16 time;            ///< 量化后的时间 [0, 65535]
        uint16 components[4];   ///< 量化后的值分量（最多 4 个）
    };

    // ============================================================
    // 量化范围（用于反量化）
    // ============================================================
    struct QuantizedRange {
        float32 minVal = 0.0f;
        float32 maxVal = 1.0f;

        QuantizedRange() = default;
        QuantizedRange(float32 min, float32 max) : minVal(min), maxVal(max) {}
    };

    // ============================================================
    // Hermite 曲线控制点（曲线压缩用）
    // ============================================================
    struct HermiteControlPoint {
        float32 time;           ///< 时间位置
        float32 value;          ///< 值
        float32 inTangent;      ///< 入切线（前一控制点指向本点）
        float32 outTangent;     ///< 出切线（本点指向下一控制点）
    };

    // ============================================================
    // 压缩轨道
    // ============================================================
    class CompressedTrack {
    public:
        CompressedTrack() = default;
        ~CompressedTrack() = default;

        // ── 类型信息 ──
        AnimationPropertyType GetPropertyType() const noexcept { return m_Type; }
        void SetPropertyType(AnimationPropertyType t) noexcept { m_Type = t; }

        AnimationInterpolation GetInterpolation() const noexcept { return m_Interpolation; }
        void SetInterpolation(AnimationInterpolation i) noexcept { m_Interpolation = i; }

        bool IsEmpty() const noexcept { return m_NumFrames == 0; }
        void SetDuration(float32 d) noexcept { m_Duration = d; }
        float32 GetDuration() const noexcept { return m_Duration; }

        // ── 量化数据访问 ──
        const std::vector<QuantizedKeyFrame16>& GetQuantizedFrames() const noexcept {
            return m_QuantizedFrames;
        }
        std::vector<QuantizedKeyFrame16>& GetQuantizedFrames() noexcept {
            return m_QuantizedFrames;
        }

        const QuantizedRange& GetTimeRange() const noexcept { return m_TimeRange; }
        void SetTimeRange(const QuantizedRange& r) noexcept { m_TimeRange = r; }

        const std::vector<QuantizedRange>& GetComponentRanges() const noexcept {
            return m_CompRanges;
        }
        std::vector<QuantizedRange>& GetComponentRanges() noexcept {
            return m_CompRanges;
        }

        // ── Hermite 曲线数据访问 ──
        const std::vector<HermiteControlPoint>& GetHermitePoints() const noexcept {
            return m_HermitePoints;
        }
        std::vector<HermiteControlPoint>& GetHermitePoints() noexcept {
            return m_HermitePoints;
        }

        // ── 元数据 ──
        void SetNumFrames(int32 n) noexcept { m_NumFrames = n; }
        int32 GetNumFrames() const noexcept { return m_NumFrames; }

        void SetOriginalSampleRate(float32 rate) noexcept { m_OrigSampleRate = rate; }
        float32 GetOriginalSampleRate() const noexcept { return m_OrigSampleRate; }

        // ── 运行时解码求值 ──
        float32 EvaluateFloat(float32 time) const;
        Vec2    EvaluateVec2(float32 time) const;
        Vec3    EvaluateVec3(float32 time) const;
        Vec4    EvaluateVec4(float32 time) const;

        /** 获取未压缩时的近似内存估算（调试用） */
        size_t EstimateUncompressedSize() const;
        /** 获取压缩后的内存大小 */
        size_t GetCompressedSize() const;

    private:
        // ── 解码辅助 ──
        float32 DequantizeTime(uint16 qtime) const;
        float32 DequantizeComponent(uint16 qval, int32 compIdx) const;
        void DequantizeFrame(int32 idx, float32& outTime, float32 outComponents[4]) const;

        // Hermite 求值
        float32 EvaluateHermite(float32 time) const;

        // ── 元数据 ──
        AnimationPropertyType    m_Type          = AnimationPropertyType::Float;
        AnimationInterpolation   m_Interpolation = AnimationInterpolation::Linear;
        float32                  m_Duration      = 0.0f;
        int32                    m_NumFrames     = 0;
        float32                  m_OrigSampleRate = 30.0f;

        // ── 量化数据 ──
        std::vector<QuantizedKeyFrame16> m_QuantizedFrames;
        QuantizedRange               m_TimeRange;       ///< 时间范围 [min, max]
        std::vector<QuantizedRange>  m_CompRanges;      ///< 每个分量的范围

        // ── 曲线数据（曲线压缩模式使用） ──
        std::vector<HermiteControlPoint> m_HermitePoints;
    };

    // ============================================================
    // 压缩后的动画片段
    // ============================================================
    struct CompressedAnimationClip {
        std::string                     name;
        float32                         duration   = 0.0f;
        float32                         sampleRate = 30.0f;
        AnimationLoopMode               loopMode   = AnimationLoopMode::Once;

        // 压缩后的轨道
        CompressedTrack                 positionTrack;
        CompressedTrack                 rotationTrack;
        CompressedTrack                 scaleTrack;
        CompressedTrack                 colorTrack;

        // 自定义浮点轨道
        struct CompressedFloatTrack {
            std::string name;
            CompressedTrack track;
        };
        std::vector<CompressedFloatTrack> floatTracks;

        bool IsEmpty() const noexcept {
            return positionTrack.IsEmpty() && rotationTrack.IsEmpty()
                && scaleTrack.IsEmpty() && colorTrack.IsEmpty()
                && floatTracks.empty();
        }

        /** 估算内存使用 */
        size_t EstimateMemoryUsage() const;
    };

    // ============================================================
    // 压缩器
    // ============================================================
    class AnimationCompressor {
    public:
        AnimationCompressor() = delete;

        // ── 轨道压缩 ──

        /**
         * @brief 压缩单个轨道
         * @param track    原始轨道
         * @param settings 压缩配置
         * @return 压缩后的轨道
         */
        static CompressedTrack CompressTrack(const AnimationTrack& track,
                                              const AnimationCompressionSettings& settings);

        /**
         * @brief 压缩完整的时间线
         * @param timeline 原始动画时间线
         * @param settings 压缩配置
         * @return 压缩后的动画片段
         */
        static CompressedAnimationClip CompressTimeline(
            const AnimationLocalTimeline& timeline,
            const AnimationCompressionSettings& settings);

        // ── 关键帧降采样 ──

        /**
         * @brief 简化关键帧序列（类似 Ramer-Douglas-Peucker 算法）
         * @param keys        输入关键帧
         * @param tolerance   误差容限
         * @param interpMode  插值模式（Linear/Smooth）
         * @return 简化后的关键帧
         */
        static std::vector<KeyFrameFloat> DecimateKeyFrames(
            const std::vector<KeyFrameFloat>& keys,
            float32 tolerance,
            AnimationInterpolation interpMode);

        // ── 曲线拟合 ──

        /**
         * @brief 将关键帧序列拟合为 Hermite 曲线控制点
         * @param keys      输入关键帧
         * @param tolerance 误差容限
         * @return Hermite 控制点序列
         */
        static std::vector<HermiteControlPoint> FitHermiteCurve(
            const std::vector<KeyFrameFloat>& keys,
            float32 tolerance);

        // ── 重采样 ──

        /**
         * @brief 重采样轨道到目标帧率
         * @param track    原始轨道
         * @param sampleRate 目标采样率（帧/秒）
         * @return 包含新关键帧的轨道
         */
        static AnimationTrack ResampleTrack(const AnimationTrack& track,
                                              float32 sampleRate);

        // ── 量化工具 ──

        /** 将 float32 量化为 uint16 */
        static uint16 QuantizeFloat32(float32 value, float32 minVal, float32 maxVal, int32 bits);

        /** 将 uint16 反量化为 float32 */
        static float32 DequantizeUint16(uint16 q, float32 minVal, float32 maxVal, int32 bits);

        /** 计算量化范围 */
        static QuantizedRange ComputeRange(const std::vector<KeyFrameFloat>& keys,
                                            int32 componentIndex = 0);

        /** 计算轨道中每个分量的量化范围 */
        static std::vector<QuantizedRange> ComputeComponentRanges(
            const AnimationTrack& track);

        // ── 通道精简 ──

        /**
         * @brief 检测轨道是否可视为常数（所有值接近相同）
         * @param track     轨道
         * @param tolerance 容限
         * @return 如果是常数轨道则返回 true
         */
        static bool IsConstantTrack(const AnimationTrack& track, float32 tolerance = 1e-6f);
    };

} // namespace Engine
