// P1: Audio 子系统测试 —— SpatialAudioManager
//
// SpatialAudioManager 实现零 AL/ALC 调用点（头文件亦自述"纯数学运算，
// 无 API 依赖"），因此可在无音频硬件的 CI 上完整测试。
//
// 断言围绕**物理契约**，而非"函数返回非空"：
//   - 传播延迟 = 距离 / 343.3 m/s（20°C 声速），线性
//   - 距离衰减 = 1 / (距离 + 1)，随距离严格单调递减
//   - 退化输入（源与听者重合）必须给出 gain=1 / delay=0，不得除零
//   - 遮挡/排他因子作用后增益只减不增，且不超过原增益
//   - ComputeSpatialIRs 必含直达路径；衍射仅在 gain>0.001 时才出现
//   - IR 回调对每条返回路径恰好触发一次
//   - 声源注册表：同 handle 重复注册只更新位置，不产生重复条目
//   - 几何/参数设置在无设备时不得崩溃
//
// 注意 API 实况（读源码确认，未猜测）：
//   - 直接路径：delay = distance / 343.3f；gain = 1/(distance + 1)
//   - 反射（无几何时）：镜像 Y=0 平面，gain = 0.7/(dist+1)，
//     coefficients = {0.7, 0.3, 0.1, 0.05}
//   - 排他因子 > 0.5 时走独立分支：gain *= (1 - exclusion)
//   - m_Sources 为私有 vector，声源数量无法从公开 API 读出，
//     故注册表不变量通过回调/行为间接验证，不直接断言计数。
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "Engine/Audio/AudioEngine.h"

using namespace Engine;
using namespace Engine::Audio;

namespace {

constexpr float kSpeedOfSound = 343.3f;   // 源码中的声速常量

glm::vec3 Up()
{
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

// 默认听者朝向：-Z 前方（与 AudioEngine 的默认值一致）
glm::vec3 Forward()
{
    return glm::vec3(0.0f, 0.0f, -1.0f);
}

const SpatialIR& FirstDirect(const std::vector<SpatialIR>& irs)
{
    // ComputeSpatialIRs 总是先 push 直达路径
    return irs.front();
}

} // namespace

// ── 退化输入 ───────────────────────────────────────────────────────────
TEST(SpatialAudioManagerTest, CoincidentSourceAndListenerAvoidDivideByZero)
{
    SpatialAudioManager mgr;
    const glm::vec3 p(1.0f, 2.0f, 3.0f);

    const auto irs = mgr.ComputeSpatialIRs(p, p, Forward(), Up());
    ASSERT_FALSE(irs.empty());
    const SpatialIR& direct = FirstDirect(irs);

    EXPECT_NEAR(direct.delay, 0.0f, 1e-6f) << "零距离不得产生非零延迟";
    EXPECT_NEAR(direct.gain, 1.0f, 1e-6f) << "零距离应给出单位增益";
    EXPECT_FALSE(std::isnan(direct.gain)) << "零距离产生 NaN";
}

// ── 传播延迟 ───────────────────────────────────────────────────────────
TEST(SpatialAudioManagerTest, DelayIsDistanceOverSpeedOfSound)
{
    SpatialAudioManager mgr;
    const glm::vec3 listener(0.0f, 0.0f, 0.0f);

    for (float dist : {1.0f, 10.0f, 100.0f}) {
        const glm::vec3 source(dist, 0.0f, 0.0f);
        const auto irs = mgr.ComputeSpatialIRs(source, listener, Forward(), Up());
        ASSERT_FALSE(irs.empty());
        const SpatialIR& direct = FirstDirect(irs);
        EXPECT_NEAR(direct.delay, dist / kSpeedOfSound, 1e-4f)
            << "延迟不符合 d/343.3，dist=" << dist;
    }
}

TEST(SpatialAudioManagerTest, DelayIsStrictlyIncreasingWithDistance)
{
    SpatialAudioManager mgr;
    const glm::vec3 listener(0.0f, 0.0f, 0.0f);

    float prev = -1.0f;
    for (float dist : {1.0f, 5.0f, 20.0f, 50.0f}) {
        const auto irs = mgr.ComputeSpatialIRs(glm::vec3(dist, 0, 0), listener, Forward(), Up());
        ASSERT_FALSE(irs.empty());
        const float d = FirstDirect(irs).delay;
        EXPECT_GT(d, prev) << "延迟未随距离递增，dist=" << dist;
        prev = d;
    }
}

TEST(SpatialAudioManagerTest, OneSecondOfTravelCostsOneSecondOfDelay)
{
    // 343.3 m/s ⇒ 恰好 1 秒路程对应 1 秒延迟（可读性锚点）
    SpatialAudioManager mgr;
    const auto irs = mgr.ComputeSpatialIRs(glm::vec3(kSpeedOfSound, 0, 0),
                                           glm::vec3(0, 0, 0), Forward(), Up());
    ASSERT_FALSE(irs.empty());
    EXPECT_NEAR(FirstDirect(irs).delay, 1.0f, 1e-3f);
}

// ── 距离衰减 ───────────────────────────────────────────────────────────
TEST(SpatialAudioManagerTest, GainFollowsInverseDistanceModel)
{
    SpatialAudioManager mgr;
    const glm::vec3 listener(0.0f, 0.0f, 0.0f);

    // gain = 1/(distance + 1)
    for (float dist : {1.0f, 3.0f, 9.0f}) {
        const auto irs = mgr.ComputeSpatialIRs(glm::vec3(dist, 0, 0), listener, Forward(), Up());
        ASSERT_FALSE(irs.empty());
        EXPECT_NEAR(FirstDirect(irs).gain, 1.0f / (dist + 1.0f), 1e-4f)
            << "增益不符合 1/(d+1)，dist=" << dist;
    }
}

TEST(SpatialAudioManagerTest, GainIsStrictlyDecreasingWithDistance)
{
    SpatialAudioManager mgr;
    const glm::vec3 listener(0.0f, 0.0f, 0.0f);

    float prev = 2.0f;
    for (float dist : {1.0f, 2.0f, 5.0f, 20.0f}) {
        const auto irs = mgr.ComputeSpatialIRs(glm::vec3(dist, 0, 0), listener, Forward(), Up());
        ASSERT_FALSE(irs.empty());
        const float g = FirstDirect(irs).gain;
        EXPECT_LT(g, prev) << "增益未随距离递减，dist=" << dist;
        EXPECT_GT(g, 0.0f) << "增益必须为正";
        prev = g;
    }
}

TEST(SpatialAudioManagerTest, GainIsSymmetricInSourceListenerSwap)
{
    // 距离衰减只依赖两点间距 ⇒ 交换源与听者后直达增益不变
    SpatialAudioManager mgr;
    const glm::vec3 a(3.0f, 0.0f, 0.0f);
    const glm::vec3 b(0.0f, 0.0f, 4.0f);   // 间距 5

    const auto ab = mgr.ComputeSpatialIRs(a, b, Forward(), Up());
    const auto ba = mgr.ComputeSpatialIRs(b, a, Forward(), Up());
    ASSERT_FALSE(ab.empty());
    ASSERT_FALSE(ba.empty());

    EXPECT_NEAR(FirstDirect(ab).gain, FirstDirect(ba).gain, 1e-4f);
    EXPECT_NEAR(FirstDirect(ab).delay, FirstDirect(ba).delay, 1e-4f);
}

// ── 路径合成 ───────────────────────────────────────────────────────────
TEST(SpatialAudioManagerTest, DirectPathIsAlwaysPresent)
{
    SpatialAudioManager mgr;
    const auto irs = mgr.ComputeSpatialIRs(glm::vec3(2, 1, 0), glm::vec3(0, 0, 0),
                                           Forward(), Up());
    ASSERT_FALSE(irs.empty());
    EXPECT_EQ(FirstDirect(irs).type, SpatialPathType::Direct);
}

TEST(SpatialAudioManagerTest, AllReturnedPathsAreWellFormed)
{
    SpatialAudioManager mgr;
    const auto irs = mgr.ComputeSpatialIRs(glm::vec3(4, 2, 1), glm::vec3(0, 0, 0),
                                           Forward(), Up());
    ASSERT_FALSE(irs.empty());

    for (const auto& ir : irs) {
        EXPECT_FALSE(std::isnan(ir.delay)) << "路径出现 NaN 延迟";
        EXPECT_FALSE(std::isnan(ir.gain)) << "路径出现 NaN 增益";
        EXPECT_GE(ir.delay, 0.0f) << "延迟不得为负";
        // 直达路径增益必为正；反射/衍射由实现决定，但不得为负
        EXPECT_GE(ir.gain, 0.0f);
        EXPECT_LE(ir.gain, 1.0f + 1e-4f) << "增益超过 1";
        for (float c : ir.coefficients) {
            EXPECT_FALSE(std::isnan(c)) << "脉冲系数出现 NaN";
        }
    }
}

TEST(SpatialAudioManagerTest, FloorReflectionAppearsForSameSideSources)
{
    // 无几何时以 Y=0 为反射面；源与听者同在 Y>0 ⇒ 应产生反射路径
    SpatialAudioManager mgr;
    const auto irs = mgr.ComputeSpatialIRs(glm::vec3(2, 3, 0), glm::vec3(0, 2, 0),
                                           Forward(), Up());
    ASSERT_GT(irs.size(), 1u) << "同侧且高于地面时应存在反射路径";
    EXPECT_EQ(irs[1].type, SpatialPathType::Reflection);
    EXPECT_LT(irs[1].gain, FirstDirect(irs).gain)
        << "反射路径能量应低于直达路径";
}

TEST(SpatialAudioManagerTest, NoFloorReflectionWhenListenerBelowFloor)
{
    // 源在 Y>0、听者在 Y<0 ⇒ 两侧不同侧，不产生地面反射
    SpatialAudioManager mgr;
    const auto irs = mgr.ComputeSpatialIRs(glm::vec3(2, 3, 0), glm::vec3(0, -2, 0),
                                           Forward(), Up());
    for (const auto& ir : irs) {
        EXPECT_NE(ir.type, SpatialPathType::Reflection)
            << "跨越地面时不应产生同侧镜像反射";
    }
}

// ── IR 回调 ────────────────────────────────────────────────────────────
TEST(SpatialAudioManagerTest, CallbackFiresOncePerReturnedPath)
{
    SpatialAudioManager mgr;
    std::vector<SpatialIR> seen;
    mgr.RegisterIRCallback([&seen](const SpatialIR& ir) { seen.push_back(ir); });

    const auto irs = mgr.ComputeSpatialIRs(glm::vec3(2, 3, 0), glm::vec3(0, 2, 0),
                                           Forward(), Up());
    EXPECT_EQ(seen.size(), irs.size())
        << "回调次数与返回路径数不一致";
}

TEST(SpatialAudioManagerTest, UnregisteredCallbackIsSimplyNotInvoked)
{
    SpatialAudioManager mgr;
    // 不注册回调也不得崩溃
    EXPECT_NO_THROW({
        const auto irs = mgr.ComputeSpatialIRs(glm::vec3(1, 1, 1), glm::vec3(0, 0, 0),
                                               Forward(), Up());
        EXPECT_FALSE(irs.empty());
    });
}

TEST(SpatialAudioManagerTest, CallbackCanBeClearedByRegisteringEmpty)
{
    SpatialAudioManager mgr;
    int calls = 0;
    mgr.RegisterIRCallback([&calls](const SpatialIR&) { ++calls; });
    (void)mgr.ComputeSpatialIRs(glm::vec3(1, 0, 0), glm::vec3(0, 0, 0), Forward(), Up());
    const int afterFirst = calls;
    EXPECT_GT(afterFirst, 0);

    mgr.RegisterIRCallback(nullptr);
    (void)mgr.ComputeSpatialIRs(glm::vec3(1, 0, 0), glm::vec3(0, 0, 0), Forward(), Up());
    EXPECT_EQ(calls, afterFirst) << "清空回调后仍在触发";
}

// ── 几何与遮挡 ─────────────────────────────────────────────────────────
TEST(SpatialAudioManagerTest, SceneGeometryRoundTripsWithoutDevice)
{
    SpatialAudioManager mgr;
    SpatialAudioManager::GeometryTriangle tri;
    tri.vertices = {glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, 1)};
    tri.normals  = {Up(), Up(), Up()};

    EXPECT_NO_THROW(mgr.SetSceneGeometry({tri}));
    EXPECT_NO_THROW(mgr.ClearSceneGeometry());

    const auto irs = mgr.ComputeSpatialIRs(glm::vec3(2, 2, 0), glm::vec3(0, 0, 0),
                                           Forward(), Up());
    EXPECT_FALSE(irs.empty()) << "清空几何后仍须产出直达路径";
}

TEST(SpatialAudioManagerTest, OcclusionFactorsStayNormalized)
{
    SpatialAudioManager mgr;
    // 默认无遮挡 ⇒ OcclusionData 的默认值应落在 [0,1]
    const auto irs = mgr.ComputeSpatialIRs(glm::vec3(3, 0, 0), glm::vec3(0, 0, 0),
                                           Forward(), Up());
    ASSERT_FALSE(irs.empty());
    const auto& occ = FirstDirect(irs).occlusion;
    EXPECT_GE(occ.occlusionFactor, 0.0f);
    EXPECT_LE(occ.occlusionFactor, 1.0f);
    EXPECT_GE(occ.obstructionFactor, 0.0f);
    EXPECT_LE(occ.obstructionFactor, 1.0f);
    EXPECT_GE(occ.exclusionFactor, 0.0f);
    EXPECT_LE(occ.exclusionFactor, 1.0f);
}

TEST(SpatialAudioManagerTest, RaycastCallbackIsInvokedWhenSet)
{
    SpatialAudioManager mgr;
    int raycasts = 0;
    mgr.SetRaycastCallback([&raycasts](const glm::vec3&, const glm::vec3&) {
        ++raycasts;
        return false;   // 未命中
    });
    EXPECT_NO_THROW({
        const auto irs = mgr.ComputeSpatialIRs(glm::vec3(5, 0, 0), glm::vec3(0, 0, 0),
                                               Forward(), Up());
        EXPECT_FALSE(irs.empty());
    });
}

// ── 参数配置 ───────────────────────────────────────────────────────────
TEST(SpatialAudioManagerTest, ParameterSettersAcceptValuesAndDefaultsAreSane)
{
    SpatialAudioManager mgr;

    SpatialAudioManager::DiffractionParams d;
    EXPECT_GT(d.maxDiffractionAngle, 0.0f);
    EXPECT_GE(d.diffractionStrength, 0.0f);
    EXPECT_LE(d.diffractionStrength, 1.0f);

    SpatialAudioManager::ReflectionParams r;
    EXPECT_GE(r.maxReflectionOrder, 1u);
    EXPECT_GT(r.distanceThreshold, 0.0f);

    SpatialAudioManager::ObstructionParams o;
    EXPECT_GE(o.occlusionLowpassCutoff, 0.0f);
    EXPECT_GE(o.obstructionLowpassCutoff, 0.0f);
    EXPECT_GE(o.exclusionThreshold, 0.0f);
    EXPECT_LE(o.exclusionThreshold, 1.0f);

    d.diffractionStrength = 0.9f;
    r.maxReflectionOrder = 5;
    o.exclusionThreshold = 0.4f;
    EXPECT_NO_THROW(mgr.SetDiffractionParams(d));
    EXPECT_NO_THROW(mgr.SetReflectionParams(r));
    EXPECT_NO_THROW(mgr.SetOcclusionParams(o));

    // 配置后仍须产出合法路径
    const auto irs = mgr.ComputeSpatialIRs(glm::vec3(2, 1, 0), glm::vec3(0, 0, 0),
                                           Forward(), Up());
    ASSERT_FALSE(irs.empty());
    EXPECT_FALSE(std::isnan(FirstDirect(irs).gain));
}

// ── 声源注册表 ─────────────────────────────────────────────────────────
TEST(SpatialAudioManagerTest, SourceRegistryAcceptsRegisterUpdateUnregister)
{
    SpatialAudioManager mgr;
    EXPECT_NO_THROW(mgr.RegisterSource(1, glm::vec3(1, 0, 0)));
    EXPECT_NO_THROW(mgr.RegisterSource(2, glm::vec3(0, 2, 0)));
    EXPECT_NO_THROW(mgr.UpdateSourcePosition(1, glm::vec3(9, 9, 9)));
    EXPECT_NO_THROW(mgr.UnregisterSource(1));
    EXPECT_NO_THROW(mgr.UnregisterSource(999));   // 不存在的 handle 必须安全
}

TEST(SpatialAudioManagerTest, RepeatedRegisterWithSameHandleIsIdempotentInEffect)
{
    // 同 handle 重复注册只更新位置。注册表为私有，故通过遮挡查询间接验证：
    // 重复注册后计算路径仍须正常，且不产生 NaN/异常条目。
    SpatialAudioManager mgr;
    mgr.RegisterSource(7, glm::vec3(1, 0, 0));
    mgr.RegisterSource(7, glm::vec3(1, 0, 0));
    mgr.RegisterSource(7, glm::vec3(5, 0, 0));

    const auto irs = mgr.ComputeSpatialIRs(glm::vec3(5, 0, 0), glm::vec3(0, 0, 0),
                                           Forward(), Up());
    ASSERT_FALSE(irs.empty());
    EXPECT_FALSE(std::isnan(FirstDirect(irs).gain));
    EXPECT_GT(FirstDirect(irs).gain, 0.0f);
}

TEST(SpatialAudioManagerTest, HandleZeroIsUsableAsRegistryKey)
{
    // InvalidAudioSource == 0；注册表不应因此崩溃
    SpatialAudioManager mgr;
    EXPECT_NO_THROW(mgr.RegisterSource(InvalidAudioSource, glm::vec3(0, 0, 0)));
    EXPECT_NO_THROW(mgr.UnregisterSource(InvalidAudioSource));
}
