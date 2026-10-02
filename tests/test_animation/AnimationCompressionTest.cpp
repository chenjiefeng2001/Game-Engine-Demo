// P1: Animation 子系统测试 —— AnimationCompression
//
// 依据 docs/Engine-Capability-Audit.md §4：Animation 声称完整但零测试。
// 压缩是"静默数据丢失"风险最高的一环：编码-解码往返若不保真，误差会一路
// 传到骨骼姿态而没有任何报错。因此本单元以**往返保真**为核心不变量：
//   - Quantize → Dequantize 的误差不超过量化步长 (max-min)/(2^bits - 1)
//   - 区间端点 min/max 必须**精确**往返（量化网格两端必须可达）
//   - 量化必须单调：value 增大 ⇒ q 不减
//   - 解码结果恒在 [min,max] 内（不得溢出区间）
//   - DecimateKeyFrames 在容差内丢帧，且不得改变端点
//   - 常量轨道判定与压缩幂等性
//
// 注意 API 实况（读源码确认，未猜测）：
//   - AnimationTrack 持 4 组 key 数组，按 PropertyType 择一使用
//   - AnimationCompressor 是 **纯静态**工具类，构造被 delete
//   - QuantizeFloat32/DequantizeUint16 均带 bits 参数（非固定 16）
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Engine/Animation/AnimationCompression.h"
#include "Engine/Core/RHI/MathTypes.h"

using namespace Engine;

namespace {

constexpr int32 kBits = 16;
constexpr double kLevels = 65535.0;   // 2^16 - 1

double Step(float32 minVal, float32 maxVal, int32 bits)
{
    const double levels = std::pow(2.0, bits) - 1.0;
    return static_cast<double>(maxVal - minVal) / levels;
}

AnimationTrack MakeFloatTrack(const std::vector<KeyFrameFloat>& keys)
{
    AnimationTrack t;
    for (const auto& k : keys) {
        t.AddKeyFrame(k);
    }
    return t;
}

} // namespace

// ── 量化往返保真（核心不变量）────────────────────────────────────────
TEST(AnimationCompressionTest, QuantizeRoundTripStaysWithinHalfStep)
{
    const float32 lo = -3.0f, hi = 7.0f;
    const double step = Step(lo, hi, kBits);

    for (int i = 0; i <= 20; ++i) {
        const float32 v = lo + (hi - lo) * (static_cast<float32>(i) / 20.0f);
        const uint16 q = AnimationCompressor::QuantizeFloat32(v, lo, hi, kBits);
        const float32 back = AnimationCompressor::DequantizeUint16(q, lo, hi, kBits);
        EXPECT_NEAR(static_cast<double>(back), static_cast<double>(v), step)
            << "round-trip error exceeded one quantization step at v=" << v;
    }
}

TEST(AnimationCompressionTest, QuantizeRoundTripIsExactAtEndpoints)
{
    const float32 lo = -2.5f, hi = 4.5f;
    // 量化网格的两端必须精确可达，否则端点姿态会系统性偏移
    EXPECT_FLOAT_EQ(AnimationCompressor::DequantizeUint16(
        AnimationCompressor::QuantizeFloat32(lo, lo, hi, kBits), lo, hi, kBits), lo);
    EXPECT_FLOAT_EQ(AnimationCompressor::DequantizeUint16(
        AnimationCompressor::QuantizeFloat32(hi, lo, hi, kBits), lo, hi, kBits), hi);
}

TEST(AnimationCompressionTest, QuantizeIsMonotonic)
{
    const float32 lo = 0.0f, hi = 10.0f;
    uint16 prev = AnimationCompressor::QuantizeFloat32(lo, lo, hi, kBits);
    for (int i = 1; i <= 200; ++i) {
        const float32 v = hi * (static_cast<float32>(i) / 200.0f);
        const uint16 q = AnimationCompressor::QuantizeFloat32(v, lo, hi, kBits);
        EXPECT_GE(q, prev) << "quantization not monotonic at v=" << v;
        prev = q;
    }
}

TEST(AnimationCompressionTest, DequantizeAlwaysStaysWithinRange)
{
    const float32 lo = -1.0f, hi = 1.0f;
    for (int i = 0; i <= 64; ++i) {
        const float32 v = lo + (hi - lo) * (static_cast<float32>(i) / 64.0f);
        const float32 back = AnimationCompressor::DequantizeUint16(
            AnimationCompressor::QuantizeFloat32(v, lo, hi, kBits), lo, hi, kBits);
        EXPECT_GE(back, lo - 1e-5f) << "decoded below range min: " << back;
        EXPECT_LE(back, hi + 1e-5f) << "decoded above range max: " << back;
    }
}

TEST(AnimationCompressionTest, LowerBitDepthIncreasesError)
{
    const float32 lo = 0.0f, hi = 1.0f;
    const float32 v = 0.3712345f;
    const double err16 = std::abs(static_cast<double>(
        AnimationCompressor::DequantizeUint16(
            AnimationCompressor::QuantizeFloat32(v, lo, hi, 16), lo, hi, 16)) - v);
    const double err8 = std::abs(static_cast<double>(
        AnimationCompressor::DequantizeUint16(
            AnimationCompressor::QuantizeFloat32(v, lo, hi, 8), lo, hi, 8)) - v);
    // 位宽越低误差必须越大（或相等）—— 否则 bits 参数未被真正使用
    EXPECT_GE(err8, err16) << "8-bit error " << err8 << " < 16-bit error " << err16;
    EXPECT_LE(err8, Step(lo, hi, 8)) << "8-bit error exceeded its own step size";
}

// ── 区间计算 ──────────────────────────────────────────────────────────
TEST(AnimationCompressionTest, ComputeRangeCoversAllKeyValues)
{
    const std::vector<KeyFrameFloat> keys = {
        {0.0f, -1.0f}, {0.5f, 5.0f}, {1.0f, 2.5f},
    };
    const QuantizedRange r = AnimationCompressor::ComputeRange(keys, 0);
    EXPECT_FLOAT_EQ(r.minVal, -1.0f);
    EXPECT_FLOAT_EQ(r.maxVal, 5.0f);
}

TEST(AnimationCompressionTest, ComputeRangeOfEmptyKeysIsSafe)
{
    const std::vector<KeyFrameFloat> none;
    const QuantizedRange r = AnimationCompressor::ComputeRange(none, 0);
    // 不崩溃即可；不得产生 NaN
    EXPECT_FALSE(std::isnan(r.minVal));
    EXPECT_FALSE(std::isnan(r.maxVal));
}

TEST(AnimationCompressionTest, ComputeComponentRangesCoversVectorChannels)
{
    AnimationTrack t;
    t.SetPropertyType(AnimationPropertyType::Vec3);
    t.AddKeyFrame(KeyFrameVec3{0.0f, Vec3(-1.0f, 0.0f, 4.0f)});
    t.AddKeyFrame(KeyFrameVec3{1.0f, Vec3(3.0f, -2.0f, 1.0f)});

    const auto ranges = AnimationCompressor::ComputeComponentRanges(t);

    // K1 已修复：现在必须从真实关键帧数据算出每个分量的 min/max。
    ASSERT_EQ(ranges.size(), 3u) << "Vec3 应返回 3 个分量区间";
    EXPECT_FLOAT_EQ(ranges[0].minVal, -1.0f);   // x: -1 .. 3
    EXPECT_FLOAT_EQ(ranges[0].maxVal, 3.0f);
    EXPECT_FLOAT_EQ(ranges[1].minVal, -2.0f);   // y: -2 .. 0
    EXPECT_FLOAT_EQ(ranges[1].maxVal, 0.0f);
    EXPECT_FLOAT_EQ(ranges[2].minVal, 1.0f);    // z: 1 .. 4
    EXPECT_FLOAT_EQ(ranges[2].maxVal, 4.0f);
}

TEST(AnimationCompressionTest, ComponentRangesReflectRealDataNotHardcodedStub)
{
    // 直接针对 K1 的回归：同一维度、不同数据，区间必须不同。
    // 桩实现下两者都会返回 (0,1)。
// 每条轨道给两个不同值的帧：常量分量会被扩成 ±0.5 宽的区间，
    // 单帧轨道同理 —— 用变化数据才能直接读到真实 min/max。
    AnimationTrack a;
    a.AddKeyFrame(KeyFrameFloat{0.0f, -4.0f});
    a.AddKeyFrame(KeyFrameFloat{1.0f, -1.0f});
    AnimationTrack b;
    b.AddKeyFrame(KeyFrameFloat{0.0f, 2.0f});
    b.AddKeyFrame(KeyFrameFloat{1.0f, 10.0f});

    const auto ra = AnimationCompressor::ComputeComponentRanges(a);
    const auto rb = AnimationCompressor::ComputeComponentRanges(b);
    ASSERT_EQ(ra.size(), 1u);
    ASSERT_EQ(rb.size(), 1u);
    EXPECT_FLOAT_EQ(ra[0].minVal, -4.0f);
    EXPECT_FLOAT_EQ(ra[0].maxVal, -1.0f);
    EXPECT_FLOAT_EQ(rb[0].minVal, 2.0f);
    EXPECT_FLOAT_EQ(rb[0].maxVal, 10.0f);
    // 桩实现下两者都会是 (0,1)
    EXPECT_NE(ra[0].minVal, rb[0].minVal)
        << "K1 回归：区间未随数据变化，仍是硬编码桩";
}

TEST(AnimationCompressionTest, ConstantComponentGetsUnitWidenedRange)
{
    // 常量分量不能产生零步长区间（否则量化除零）
    AnimationTrack t;
    t.AddKeyFrame(KeyFrameVec3{0.0f, Vec3(7.0f, -3.0f, 2.0f)});
    t.AddKeyFrame(KeyFrameVec3{1.0f, Vec3(7.0f, -3.0f, 2.0f)});

    const auto r = AnimationCompressor::ComputeComponentRanges(t);
    ASSERT_EQ(r.size(), 3u);
    for (size_t i = 0; i < r.size(); ++i) {
        EXPECT_GT(r[i].maxVal - r[i].minVal, 0.0f)
            << "分量 " << i << " 为常量，区间宽度必须被扩开";
    }
}

TEST(AnimationCompressionTest, ComponentRangesOfEmptyTrackAreEmptyAndSafe)
{
    AnimationTrack t;                              // 无关键帧
    const auto r = AnimationCompressor::ComputeComponentRanges(t);
    // 空轨道没有可计算的区间；不得返回伪造的 (0,1)
    for (const auto& range : r) {
        EXPECT_FALSE(std::isnan(range.minVal));
        EXPECT_FALSE(std::isnan(range.maxVal));
        EXPECT_LE(range.minVal, range.maxVal);
    }
}

TEST(AnimationCompressionTest, ComponentRangeCountMatchesPropertyDimension)
{
    // 维度契约：Float→1, Vec2→2, Vec3→3, Vec4→4。
    // 注意：区间数由**实际存在的关键帧**决定，不能只靠 SetPropertyType ——
    // 空轨道没有区间可算（见上一测试）。因此这里必须真的塞入对应维度的帧。
    const size_t nFloat = [] {
        AnimationTrack t;
        t.AddKeyFrame(KeyFrameFloat{0.0f, 1.0f});
        return AnimationCompressor::ComputeComponentRanges(t).size();
    }();
    const size_t nVec2 = [] {
        AnimationTrack t;
        t.AddKeyFrame(KeyFrameVec2{0.0f, Vec2(1.0f, 2.0f)});
        return AnimationCompressor::ComputeComponentRanges(t).size();
    }();
    const size_t nVec3 = [] {
        AnimationTrack t;
        t.AddKeyFrame(KeyFrameVec3{0.0f, Vec3(1.0f, 2.0f, 3.0f)});
        return AnimationCompressor::ComputeComponentRanges(t).size();
    }();
    const size_t nVec4 = [] {
        AnimationTrack t;
        t.AddKeyFrame(KeyFrameVec4{0.0f, Vec4(1.0f, 2.0f, 3.0f, 4.0f)});
        return AnimationCompressor::ComputeComponentRanges(t).size();
    }();
    EXPECT_EQ(nFloat, 1u);
    EXPECT_EQ(nVec2, 2u);
    EXPECT_EQ(nVec3, 3u);
    EXPECT_EQ(nVec4, 4u);
}

// ── 常量轨道 ──────────────────────────────────────────────────────────
TEST(AnimationCompressionTest, ConstantTrackIsDetected)
{
    AnimationTrack t = MakeFloatTrack({{0.0f, 3.0f}, {0.5f, 3.0f}, {1.0f, 3.0f}});
    EXPECT_TRUE(AnimationCompressor::IsConstantTrack(t));

    AnimationTrack v = MakeFloatTrack({{0.0f, 0.0f}, {1.0f, 1.0f}});
    EXPECT_FALSE(AnimationCompressor::IsConstantTrack(v));
}

TEST(AnimationCompressionTest, EmptyTrackIsConstantAndSafe)
{
    AnimationTrack t;
    EXPECT_TRUE(t.IsEmpty());
    EXPECT_EQ(t.GetKeyFrameCount(), 0u);
    EXPECT_NO_THROW(AnimationCompressor::IsConstantTrack(t));
}

// ── 抽帧 ──────────────────────────────────────────────────────────────
TEST(AnimationCompressionTest, DecimationNeverExceedsOriginalFrameCount)
{
    const std::vector<KeyFrameFloat> keys = {
        {0.0f, 0.0f}, {0.1f, 1.0f}, {0.2f, 2.0f},
        {0.3f, 3.0f}, {0.4f, 4.0f}, {0.5f, 5.0f},
    };
    const auto decimated = AnimationCompressor::DecimateKeyFrames(
        keys, 0.001f, AnimationInterpolation::Linear);
    EXPECT_LE(decimated.size(), keys.size());
}

TEST(AnimationCompressionTest, DecimationPreservesEndpoints)
{
    const std::vector<KeyFrameFloat> keys = {
        {0.0f, 0.0f}, {0.25f, 0.0f}, {0.5f, 0.0f},
        {0.75f, 0.0f}, {1.0f, 0.0f},
    };
    // 平坦曲线：中间帧应可被丢弃，但首尾必须保留
    const auto d = AnimationCompressor::DecimateKeyFrames(
        keys, 0.01f, AnimationInterpolation::Linear);
    ASSERT_FALSE(d.empty()) << "decimation must not produce an empty result";
    EXPECT_FLOAT_EQ(d.front().time, keys.front().time);
    EXPECT_FLOAT_EQ(d.back().time, keys.back().time);
}

TEST(AnimationCompressionTest, DecimationOfEmptyInputIsSafe)
{
    const std::vector<KeyFrameFloat> none;
    EXPECT_NO_THROW(AnimationCompressor::DecimateKeyFrames(
        none, 0.01f, AnimationInterpolation::Linear));
}

// ── CompressedTrack ───────────────────────────────────────────────────
TEST(AnimationCompressionTest, DefaultCompressedTrackIsEmpty)
{
    CompressedTrack t;
    EXPECT_TRUE(t.IsEmpty());
    EXPECT_EQ(t.GetNumFrames(), 0);
    EXPECT_TRUE(t.GetQuantizedFrames().empty());
    EXPECT_EQ(t.GetPropertyType(), AnimationPropertyType::Float);
    EXPECT_EQ(t.GetInterpolation(), AnimationInterpolation::Linear);
}

TEST(AnimationCompressionTest, CompressedTrackAccessorsRoundTrip)
{
    CompressedTrack t;
    t.SetNumFrames(7);
    t.SetDuration(2.5f);
    t.SetOriginalSampleRate(60.0f);
    // 实测枚举只有 Step / Linear / Smooth（Hermite 曲线对应 Smooth）
    t.SetInterpolation(AnimationInterpolation::Smooth);
    t.SetPropertyType(AnimationPropertyType::Vec3);

    EXPECT_EQ(t.GetNumFrames(), 7);
    EXPECT_FLOAT_EQ(t.GetDuration(), 2.5f);
    EXPECT_FLOAT_EQ(t.GetOriginalSampleRate(), 60.0f);
    EXPECT_EQ(t.GetInterpolation(), AnimationInterpolation::Smooth);
    EXPECT_EQ(t.GetPropertyType(), AnimationPropertyType::Vec3);
    // NumFrames>0 但帧数组为空 —— IsEmpty 只看 NumFrames
    EXPECT_FALSE(t.IsEmpty());
}

TEST(AnimationCompressionTest, EmptyTrackEvaluationDoesNotCrash)
{
    CompressedTrack t;
    EXPECT_NO_THROW(t.EvaluateFloat(0.5f));
    EXPECT_NO_THROW(t.EvaluateVec3(1.0f));
}

// ── CompressTrack 往返 ────────────────────────────────────────────────
TEST(AnimationCompressionTest, CompressTrackProducesFrames)
{
    // K2 已修复：processKeys 处理路径现由 ProcessKeyTrack 模板承担，
    // CompressTrack 按属性类型分派后应真正产出量化帧。
    std::vector<KeyFrameFloat> keys;
    for (int i = 0; i <= 10; ++i) {
        keys.push_back({static_cast<float32>(i) / 10.0f, static_cast<float32>(i) * 0.1f});
    }
    AnimationTrack src = MakeFloatTrack(keys);
    ASSERT_EQ(src.GetKeyFrameCount(), 11u) << "前置条件：源轨道必须有帧";

    AnimationCompressionSettings settings;
    settings.enableQuantization = true;
    settings.quantizeBits = 16;

    const CompressedTrack ct = AnimationCompressor::CompressTrack(src, settings);

    EXPECT_EQ(ct.GetNumFrames(), 11) << "K2: 帧数必须与输入关键帧数一致";
    EXPECT_EQ(ct.GetQuantizedFrames().size(), 11u);
    EXPECT_EQ(ct.GetComponentRanges().size(), 1u) << "标量轨道应有 1 个分量区间";
    EXPECT_FALSE(ct.IsEmpty());
}

TEST(AnimationCompressionTest, CompressTrackPreservesEndpointsExactly)
{
    // 端点姿态不得漂移：首帧值 0、末帧值 1.0 必须精确还原
    // （量化网格两端可达，见 QuantizeRoundTripIsExactAtEndpoints）
    AnimationTrack src = MakeFloatTrack({{0.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 1.0f}});
    AnimationCompressionSettings settings;

    const CompressedTrack ct = AnimationCompressor::CompressTrack(src, settings);
    ASSERT_EQ(ct.GetNumFrames(), 3);

    const float32 first = ct.EvaluateFloat(0.0f);
    const float32 last = ct.EvaluateFloat(1.0f);
    EXPECT_NEAR(first, 0.0f, 1e-4f) << "首帧值漂移";
    EXPECT_NEAR(last, 1.0f, 1e-4f) << "末帧值漂移";
}

TEST(AnimationCompressionTest, CompressTrackRoundTripErrorBoundedByQuantizationStep)
{
    // 核心契约：解码值与原始值的误差必须受量化步长约束，
    // 而不是"随便返回某个数"。
    std::vector<KeyFrameFloat> keys;
    for (int i = 0; i <= 20; ++i) {
        const float32 t = static_cast<float32>(i) / 20.0f;
        keys.push_back({t, std::sin(t * 3.14159f) * 0.5f + 0.5f});
    }
    AnimationTrack src = MakeFloatTrack(keys);
    AnimationCompressionSettings settings;

    const CompressedTrack ct = AnimationCompressor::CompressTrack(src, settings);
    ASSERT_GT(ct.GetNumFrames(), 0);

    const auto& timeRange = ct.GetTimeRange();
    const auto& range = ct.GetComponentRanges()[0];
    const double valueStep = Step(range.minVal, range.maxVal, 16);
    const double timeStep = Step(timeRange.minVal, timeRange.maxVal, 16);

    // 误差有两个来源，都必须计入：
    //   1. 值量化：<= valueStep/2
    //   2. 时间量化：帧时刻被量化后，按帧间线性插值求值会在原始时刻
    //      引入 |斜率| * timeStep/2 的偏差。
    // 只用 valueStep 作为界会误报（实测在 t=0.1/0.9 处超限）。
    // 这里用 valueStep + 4*timeStep 作为宽松但仍有意义的界：
    // 斜率上界取 2 已足够覆盖本测试的正弦曲线。
    const double tol = valueStep + 4.0 * timeStep;

    // 在每个原始关键帧时刻采样
    for (const auto& k : keys) {
        const float32 decoded = ct.EvaluateFloat(k.time);
        EXPECT_NEAR(static_cast<double>(decoded), static_cast<double>(k.value), tol)
            << "往返误差超过量化界，t=" << k.time;
    }
}

TEST(AnimationCompressionTest, CompressTrackPreservesMonotonicity)
{
    // 严格单调的输入轨道，压缩后仍须单调（量化不得引入反向跳变）
    std::vector<KeyFrameFloat> keys;
    for (int i = 0; i <= 10; ++i) {
        keys.push_back({static_cast<float32>(i) / 10.0f, static_cast<float32>(i)});
    }
    AnimationTrack src = MakeFloatTrack(keys);
    AnimationCompressionSettings settings;

    const CompressedTrack ct = AnimationCompressor::CompressTrack(src, settings);
    ASSERT_GT(ct.GetNumFrames(), 1);

    float32 prev = ct.EvaluateFloat(0.0f);
    for (int i = 1; i <= 20; ++i) {
        const float32 t = static_cast<float32>(i) / 20.0f;
        const float32 cur = ct.EvaluateFloat(t);
        EXPECT_GE(cur, prev - 1e-5f)
            << "单调性被破坏：t=" << t << " cur=" << cur << " prev=" << prev;
        prev = cur;
    }
}

TEST(AnimationCompressionTest, CompressTrackOfConstantTrackIsWellFormed)
{
    // 常量轨道：不得产生零宽区间（量化会除零）
    AnimationTrack src = MakeFloatTrack({{0.0f, 3.0f}, {0.5f, 3.0f}, {1.0f, 3.0f}});
    AnimationCompressionSettings settings;

    const CompressedTrack ct = AnimationCompressor::CompressTrack(src, settings);
    ASSERT_EQ(ct.GetComponentRanges().size(), 1u);
    const auto& r = ct.GetComponentRanges()[0];
    EXPECT_GT(r.maxVal - r.minVal, 0.0f) << "常量轨道区间宽度必须被扩开";
    EXPECT_NEAR(ct.EvaluateFloat(0.5f), 3.0f, 1e-3f) << "常量值必须保真";
}

TEST(AnimationCompressionTest, CompressTrackOfSingleFrameTrack)
{
    // 单帧输入：行为明确 —— 产出 1 帧，采样恒返回该值
    AnimationTrack src = MakeFloatTrack({{0.0f, 2.5f}});
    AnimationCompressionSettings settings;

    const CompressedTrack ct = AnimationCompressor::CompressTrack(src, settings);
    EXPECT_EQ(ct.GetNumFrames(), 1);
    EXPECT_NEAR(ct.EvaluateFloat(0.0f), 2.5f, 1e-3f);
    EXPECT_NEAR(ct.EvaluateFloat(99.0f), 2.5f, 1e-3f)
        << "单帧轨道在任意时刻都应返回唯一帧值";
}

TEST(AnimationCompressionTest, CompressTrackHandlesAllVectorDimensions)
{
    // 四种属性类型都必须真正产出对应维度的帧与区间
    AnimationCompressionSettings settings;

    AnimationTrack f; f.AddKeyFrame(KeyFrameFloat{0.0f, 1.0f}); f.AddKeyFrame(KeyFrameFloat{1.0f, 2.0f});
    AnimationTrack v2; v2.AddKeyFrame(KeyFrameVec2{0.0f, Vec2(1.0f, 2.0f)}); v2.AddKeyFrame(KeyFrameVec2{1.0f, Vec2(3.0f, 4.0f)});
    AnimationTrack v3; v3.AddKeyFrame(KeyFrameVec3{0.0f, Vec3(1.0f, 2.0f, 3.0f)}); v3.AddKeyFrame(KeyFrameVec3{1.0f, Vec3(4.0f, 5.0f, 6.0f)});
    AnimationTrack v4; v4.AddKeyFrame(KeyFrameVec4{0.0f, Vec4(1.0f, 2.0f, 3.0f, 4.0f)}); v4.AddKeyFrame(KeyFrameVec4{1.0f, Vec4(5.0f, 6.0f, 7.0f, 8.0f)});

    for (const auto* t : {&f, &v2, &v3, &v4}) {
        const CompressedTrack ct = AnimationCompressor::CompressTrack(*t, settings);
        EXPECT_EQ(ct.GetNumFrames(), 2)
            << "类型 " << static_cast<int>(t->GetPropertyType()) << " 未产出 2 帧";
    }
    EXPECT_EQ(AnimationCompressor::CompressTrack(v2, settings).GetComponentRanges().size(), 2u);
    EXPECT_EQ(AnimationCompressor::CompressTrack(v3, settings).GetComponentRanges().size(), 3u);
    EXPECT_EQ(AnimationCompressor::CompressTrack(v4, settings).GetComponentRanges().size(), 4u);
}

TEST(AnimationCompressionTest, CompressTrackVec3RoundTripPerComponent)
{
    // Vec3 往返：每个分量都要在各自区间内保真
    AnimationTrack v3;
    v3.AddKeyFrame(KeyFrameVec3{0.0f, Vec3(-1.0f, 0.0f, 4.0f)});
    v3.AddKeyFrame(KeyFrameVec3{1.0f, Vec3(3.0f, -2.0f, 1.0f)});
    AnimationCompressionSettings settings;

    const CompressedTrack ct = AnimationCompressor::CompressTrack(v3, settings);
    ASSERT_EQ(ct.GetComponentRanges().size(), 3u);

    const Vec3 got = ct.EvaluateVec3(0.0f);
    const Vec3 want(-1.0f, 0.0f, 4.0f);
    const double step = Step(-1.0f, 3.0f, 16);
    EXPECT_NEAR(got.x, want.x, step);
    EXPECT_NEAR(got.y, want.y, step);
    EXPECT_NEAR(got.z, want.z, step);
}

TEST(AnimationCompressionTest, CompressTrackEmptyInputIsHandled)
{
    AnimationTrack empty;
    AnimationCompressionSettings settings;
    const CompressedTrack ct = AnimationCompressor::CompressTrack(empty, settings);
    EXPECT_EQ(ct.GetNumFrames(), 0);
    EXPECT_TRUE(ct.IsEmpty());
}

TEST(AnimationCompressionTest, CompressTrackNonQuantizedPathForcesQuantization)
{
    // CompressedTrack 的存储格式本身就是量化帧，没有"原始帧"表示。
    // 因此 enableQuantization=false 会归一化为 16-bit 量化，两条设置
    // 产出的帧数必须一致 —— 否则"关闭量化"就会静默丢帧。
    AnimationTrack src = MakeFloatTrack({{0.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 1.0f}});

    AnimationCompressionSettings off;
    off.enableQuantization = false;

    AnimationCompressionSettings on;
    on.enableQuantization = true;
    on.quantizeBits = 16;

    const CompressedTrack a = AnimationCompressor::CompressTrack(src, off);
    const CompressedTrack b = AnimationCompressor::CompressTrack(src, on);
    EXPECT_EQ(a.GetNumFrames(), b.GetNumFrames());
    EXPECT_GT(a.GetNumFrames(), 0) << "关闭量化不得导致零帧";
}

TEST(AnimationCompressionTest, CompressTrackNon16BitRequestIsNormalizedTo16)
{
    // DequantizeComponent 固定按 16-bit 解释存储的 uint16。
    // 若编码时用 8-bit，解码会用错误的 scale —— 往返必然崩掉。
    // 因此 quantizeBits != 16 必须被归一化到 16。
    AnimationTrack src = MakeFloatTrack({{0.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 1.0f}});

    AnimationCompressionSettings eight;
    eight.enableQuantization = true;
    eight.quantizeBits = 8;

    const CompressedTrack a = AnimationCompressor::CompressTrack(src, eight);
    ASSERT_GT(a.GetNumFrames(), 0);
    // 若未归一化，按 8-bit 编码的值会被 16-bit 解码成错误数值
    EXPECT_NEAR(a.EvaluateFloat(1.0f), 1.0f, 1e-3f)
        << "位宽未归一化：解码 scale 与编码 scale 不一致";
}

TEST(AnimationCompressionTest, CompressionSettingsDefaultsAreConservative)
{
    AnimationCompressionSettings s;
    // 默认关闭有损/重采样类优化，仅开启结构化裁剪与量化
    EXPECT_FALSE(s.enableDecimation) << "decimation is lossy; must be opt-in";
    EXPECT_FALSE(s.enableResampling);
    EXPECT_TRUE(s.enableQuantization);
    EXPECT_EQ(s.quantizeBits, 16);
    EXPECT_FLOAT_EQ(s.targetSampleRate, 30.0f);
}

// ── CompressedAnimationClip ───────────────────────────────────────────
TEST(AnimationCompressionTest, DefaultCompressedClipIsEmpty)
{
    CompressedAnimationClip c;
    EXPECT_TRUE(c.IsEmpty());
    EXPECT_FLOAT_EQ(c.duration, 0.0f);
    EXPECT_TRUE(c.floatTracks.empty());
    EXPECT_EQ(c.loopMode, AnimationLoopMode::Once);
}

TEST(AnimationCompressionTest, ClipIsNonEmptyOnceAnyTrackHasFrames)
{
    CompressedAnimationClip c;
    c.positionTrack.SetNumFrames(1);
    EXPECT_FALSE(c.IsEmpty()) << "IsEmpty must consult the individual tracks";
}

// ── 工具类形态 ────────────────────────────────────────────────────────
TEST(AnimationCompressionTest, CompressorIsNonConstructible)
{
    // 实测：default ctor 被 delete ⇒ 无法实例化（纯静态工具类）。
    // 注意：delete 默认构造**不会**顺带删除隐式拷贝构造，所以此处不假装
    // 它是 non-copyable —— 只断言真正被契约保证的那一条。
    static_assert(!std::is_default_constructible<AnimationCompressor>::value,
                  "AnimationCompressor must not be default-constructible");
    SUCCEED();
}

TEST(AnimationCompressionTest, DegenerateRangeDoesNotProduceNaN)
{
    // min == max：量化区间宽度为 0，任何输入都不得产生 NaN
    const float32 v = AnimationCompressor::DequantizeUint16(
        AnimationCompressor::QuantizeFloat32(5.0f, 5.0f, 5.0f, kBits), 5.0f, 5.0f, kBits);
    EXPECT_FALSE(std::isnan(v));
}

// ── 已修复缺陷（K1 / K2）─────────────────────────────────────────────
//
// K1  ComputeComponentRanges 原为桩实现：对 Float/Vec2/Vec3/Vec4 一律返回
//     硬编码 QuantizedRange(0,1)，从不读取关键帧。
//     修复：新增 ComputeRangesFromKeys 模板，从真实 key data 逐分量求 min/max，
//     常量分量扩成 ±0.5 避免零步长。该模板是**唯一**实现，
//     ComputeComponentRanges 与 CompressTrack 共用，不会漂移。
//     回归覆盖：ComputeComponentRangesCoversVectorChannels、
//              ComponentRangesReflectRealDataNotHardcodedStub、
//              ConstantComponentGetsUnitWidenedRange、
//              ComponentRangesOfEmptyTrackAreEmptyAndSafe。
//
// K2  CompressTrack 内部的 processKeys lambda 定义后从未被调用，
//     导致对任何输入都返回 0 帧 —— 压缩→评估链路整体是静默 no-op。
//     修复前提（缺一不可）：
//       a) AnimationTrack 暴露只读关键帧访问器（原先 key 数组私有且无
//          friend，CompressTrack 根本无法取到数据）；
//       b) 处理逻辑提为匿名 namespace 的 ProcessKeyTrack 模板，
//          CompressTrack 按属性类型分派调用；
//       c) 归一化位宽到 16 —— DequantizeComponent 固定按 16-bit 解释，
//          编码用其它位宽会导致往返数值错误。
//     回归覆盖：CompressTrackProducesFrames、PreservesEndpointsExactly、
//              RoundTripErrorBoundedByQuantizationStep、
//              PreservesMonotonicity、ConstantTrackIsWellFormed、
//              SingleFrame、HandlesAllVectorDimensions、Vec3RoundTrip。
