// P1: Core headless 覆盖 —— Engine::Time
//
// 对应 engine/include/Engine/Platform/PlatformUtils.h +
// engine/src/Core/Time.cpp（159 行实现），此前零测试覆盖。
//
// 准入：Category 1。依赖闭包 = <cstdint> + <chrono> + <cmath>，无设备、
// 无 singleton、无文件系统。所有状态为 static 全局量，因此每个用例前
// 置为冷状态（Shutdown），且用例必须串行。
//
// 本文件只断言**从实现读出的**语义，不从函数名推测方向：
//   - Time::Init() 把 s_LastFrameTimeD 置 0（不是 s_InitTimeD），
//     而 GetTimeD() 返回"自 Init 起的经过时间"，两者同处 elapsed 坐标系
//   - GetTimeD() 在未初始化时**惰性调用 Init()**
//   - Shutdown() 只是清零 + 置 s_Initialized=false，**不是终止态**：
//     之后任何时间 getter 都会再次惰性初始化
//   - UpdateDeltaTime() 把 dt 钳制到 [0, maxDt]
//   - SetTimeScale() 把负值钳到 0
//   - s_GameTime 累加的是 timeScale * dt（受 timeScale 影响）
//   - CalibrateAccumulator() **忽略传入的 accumulator**，返回
//     fmod(自Init起的总经过时间, fixedDt)
//
// 两处刻意不写成断言（记录为 observation，见 docs/Stage2-Headless-Surface-Audit.md）：
//   - UpdateDeltaTime() 的 dt<0 分支：steady_clock 单调，s_LastFrameTimeD
//     恒 <= now，该分支在当前实现下不可达
//   - CalibrateAccumulator(fixedDt == 0)：std::fmod(x, 0) 为 UB，唯一生产
//     调用点传入 1.0f/60.0f 常量，属 latent boundary
#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "Engine/Platform/PlatformUtils.h"

using namespace Engine;

namespace {

// 确保经过时间真实推进，避免依赖时钟分辨率假设
void SleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

class TimeTest : public ::testing::Test {
protected:
    // HEAD 已提交的 Time API 只有 Init()；它是完整 reset（重置基准时间、
    // delta、deltaD、timeScale、gameTime）。因此 fixture 用 Init() 而非
    // Shutdown() 建立确定基线。
    //
    // 注意：已提交 API 无法把 s_Initialized 置回 false，所以"冷状态"与
    // "Shutdown 后惰性重初始化"在本 target 内不可测，见文件末尾说明。
    void SetUp() override { Time::Init(); }
    void TearDown() override { Time::Init(); }
};

} // namespace

// ── 生命周期 ───────────────────────────────────────────────────────────
// 只覆盖已提交的 Time::Init()。冷状态断言与 Shutdown 语义属于 HRC-3
// 未提交 API，见文件末尾"移出的 HRC 依赖覆盖"。

TEST_F(TimeTest, Init_ResetsTimeScaleToOne) {
    Time::SetTimeScale(0.5f);
    ASSERT_FLOAT_EQ(Time::GetTimeScale(), 0.5f);
    Time::Init();                      // Init 会重置 timeScale
    EXPECT_FLOAT_EQ(Time::GetTimeScale(), 1.0f);
}

TEST_F(TimeTest, Init_ResetsDeltaAndGameTimeToZero) {
    Time::Init();
    SleepMs(5);
    Time::UpdateDeltaTime(1.0f);
    ASSERT_GT(Time::GetDeltaTime(), 0.0f);
    Time::Init();
    EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.0f);
    EXPECT_DOUBLE_EQ(Time::GetGameTimeD(), 0.0);
}

// ── 惰性初始化 ─────────────────────────────────────────────────────────
TEST_F(TimeTest, GetElapsedSinceInit_TracksGetTimeD) {
    Time::Init();
    // 两者是同一个实现（GetElapsedSinceInit 直接返回 GetTimeD），
    // 但各自重新读取 steady_clock，因此只能在极小容差内比较，不能按位相等。
    EXPECT_NEAR(Time::GetElapsedSinceInit(), Time::GetTimeD(), 1e-4);
}

TEST_F(TimeTest, GetTime_IsFloatViewOfElapsedTime) {
    Time::Init();
    // GetTime() 内部先取 GetTimeD() 再截断为 float；此处再次调用 GetTimeD()
    // 时时钟已前进，故用容差比较，并断言其量级与 float 精度一致。
    const float ft = Time::GetTime();
    const double dt = Time::GetTimeD();
    EXPECT_GE(ft, 0.0f);
    EXPECT_NEAR(ft, dt, 1e-3);
    // float 视图不应超过 double 真值（单调推进）
    EXPECT_LE(ft, static_cast<float>(dt) + 1e-3f);
}

TEST_F(TimeTest, GetTime_IsNonDecreasing) {
    Time::Init();
    const float a = Time::GetTime();
    SleepMs(5);
    const float b = Time::GetTime();
    EXPECT_GE(b, a);
}

// ── UpdateDeltaTime 钳制 ───────────────────────────────────────────────
TEST_F(TimeTest, UpdateDeltaTime_FirstCallAfterInit_IsBoundedByMaxDt) {
    Time::Init();
    Time::UpdateDeltaTime();               // 默认 maxDt = 0.25
    EXPECT_GE(Time::GetDeltaTime(), 0.0f);
    EXPECT_LT(Time::GetDeltaTime(), 0.25f);
}

TEST_F(TimeTest, UpdateDeltaTime_MeasuresElapsedSinceLastFrame) {
    Time::Init();
    SleepMs(30);
    Time::UpdateDeltaTime(1.0f);           // maxDt 足够大，不触发钳制
    EXPECT_GE(Time::GetDeltaTime(), 0.025f);
    // s_DeltaTime 是 float、s_DeltaTimeD 是 double，二者是截断关系而非相等
    EXPECT_FLOAT_EQ(Time::GetDeltaTime(), static_cast<float>(Time::GetDeltaTimeD()));
}

TEST_F(TimeTest, UpdateDeltaTime_ClampsDownToMaxDt) {
    Time::Init();
    SleepMs(20);
    Time::UpdateDeltaTime(0.0f);           // 上限 0 → 必然被钳到 0
    EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.0f);
}

TEST_F(TimeTest, UpdateDeltaTime_DefaultMaxDtIsQuarterSecond) {
    Time::Init();
    SleepMs(300);                          // 远超默认 0.25s 上限
    Time::UpdateDeltaTime();
    EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.25f);
}

TEST_F(TimeTest, UpdateDeltaTime_SecondCallUsesPreviousFrameAsBaseline) {
    Time::Init();
    SleepMs(20);
    Time::UpdateDeltaTime(1.0f);
    const float first = Time::GetDeltaTime();
    ASSERT_GT(first, 0.0f);

    SleepMs(20);
    Time::UpdateDeltaTime(1.0f);
    const float second = Time::GetDeltaTime();
    // 第二帧不应把"自 Init 起"的时间算进来，只算与上一帧的间隔
    EXPECT_GT(second, 0.0f);
    EXPECT_LT(second, first + 0.020f);
}

// ── TimeScale ──────────────────────────────────────────────────────────
TEST_F(TimeTest, SetTimeScale_NegativeClampsToZero) {
    Time::Init();
    Time::SetTimeScale(-1.0f);
    EXPECT_FLOAT_EQ(Time::GetTimeScale(), 0.0f);
    Time::SetTimeScale(-0.0001f);
    EXPECT_FLOAT_EQ(Time::GetTimeScale(), 0.0f);
}

TEST_F(TimeTest, SetTimeScale_PositiveAndZeroStoredVerbatim) {
    Time::Init();
    Time::SetTimeScale(2.5f);
    EXPECT_FLOAT_EQ(Time::GetTimeScale(), 2.5f);
    Time::SetTimeScale(0.0f);
    EXPECT_FLOAT_EQ(Time::GetTimeScale(), 0.0f);
}

TEST_F(TimeTest, Pause_And_Resume_ToggleTimeScale) {
    Time::Init();
    Time::Pause();
    EXPECT_FLOAT_EQ(Time::GetTimeScale(), 0.0f);
    Time::Resume();
    EXPECT_FLOAT_EQ(Time::GetTimeScale(), 1.0f);
}

TEST_F(TimeTest, GetGameDeltaTime_EqualsDeltaTimeTimesTimeScale) {
    Time::Init();
    Time::SetTimeScale(0.25f);
    SleepMs(20);
    Time::UpdateDeltaTime(1.0f);
    const float dt = Time::GetDeltaTime();
    ASSERT_GT(dt, 0.0f);
    EXPECT_FLOAT_EQ(Time::GetGameDeltaTime(), dt * 0.25f);
}

TEST_F(TimeTest, GameTime_AdvancesByTimeScaleTimesDt) {
    Time::Init();
    Time::SetTimeScale(0.5f);
    SleepMs(20);
    Time::UpdateDeltaTime(1.0f);
    const double dt = Time::GetDeltaTimeD();
    ASSERT_GT(dt, 0.0);
    EXPECT_NEAR(Time::GetGameTimeD(), 0.5 * dt, 1e-9);
    EXPECT_FLOAT_EQ(Time::GetGameTime(), static_cast<float>(Time::GetGameTimeD()));
}

TEST_F(TimeTest, GameTime_DoesNotAdvanceWhileTimeScaleIsZero) {
    Time::Init();
    Time::Pause();                         // timeScale = 0
    SleepMs(20);
    Time::UpdateDeltaTime(1.0f);
    EXPECT_GT(Time::GetDeltaTime(), 0.0f); // dt 仍在推进
    EXPECT_DOUBLE_EQ(Time::GetGameTimeD(), 0.0);   // 但 gameTime 不推进
    EXPECT_FLOAT_EQ(Time::GetGameDeltaTime(), 0.0f);
}

// ── Ticks ──────────────────────────────────────────────────────────────
TEST_F(TimeTest, GetTickFrequency_IsPositiveAndMatchesSteadyClockPeriod) {
    const int64_t freq = Time::GetTickFrequency();
    EXPECT_GT(freq, 0);
    EXPECT_EQ(freq, static_cast<int64_t>(std::chrono::steady_clock::period::den));
}

TEST_F(TimeTest, GetTicks_IsMonotonicNonDecreasing) {
    const int64_t a = Time::GetTicks();
    SleepMs(5);
    const int64_t b = Time::GetTicks();
    EXPECT_GE(b, a);
}

// ── CalibrateAccumulator ───────────────────────────────────────────────
TEST_F(TimeTest, CalibrateAccumulator_IgnoresAccumulatorArgument) {
    Time::Init();
    SleepMs(10);
    const double a = Time::CalibrateAccumulator(0.0, 0.5);
    const double b = Time::CalibrateAccumulator(999.0, 0.5);
    const double c = Time::CalibrateAccumulator(-123.456, 0.5);
    // 传入的 accumulator 被显式丢弃（(void)accumulator）。若它真的被使用，
    // 三者会相差约 1000；此处只允许"两次调用之间的时钟漂移"量级差异。
    EXPECT_NEAR(a, b, 1e-3);
    EXPECT_NEAR(b, c, 1e-3);
}

TEST_F(TimeTest, CalibrateAccumulator_ReturnsElapsedModuloFixedDt) {
    Time::Init();
    SleepMs(10);
    const double fixedDt = 0.25;
    const double got = Time::CalibrateAccumulator(0.0, fixedDt);
    // 现有定义：基于"自 Init 起的总经过时间"，而非传入的 accumulator。
    // 函数内部每次都重新读时钟，故按漂移容差比较。
    EXPECT_NEAR(got, std::fmod(Time::GetElapsedSinceInit(), fixedDt), 1e-3);
}

TEST_F(TimeTest, CalibrateAccumulator_ResultIsBelowFixedDt) {
    Time::Init();
    SleepMs(10);
    for (double fixedDt : {0.5, 0.25, 1.0, 2.0}) {
        const double got = Time::CalibrateAccumulator(0.0, fixedDt);
        EXPECT_GE(got, 0.0);
        EXPECT_LT(got, fixedDt);
    }
}

TEST_F(TimeTest, CalibrateAccumulator_LargerFixedDtYieldsLargerRemainder) {
    Time::Init();
    SleepMs(30);
    const double small = Time::CalibrateAccumulator(0.0, 0.25);
    const double large = Time::CalibrateAccumulator(0.0, 4.0);
    EXPECT_LE(small, large);
}

TEST_F(TimeTest, CalibrateAccumulator_DoesNotAffectSubsequentState) {
    Time::Init();
    SleepMs(10);
    const double before = Time::GetGameTimeD();
    Time::CalibrateAccumulator(123.0, 0.5);

    // 已提交 API 无法查询 initialized 标志，因此改用可观测证据：校准不得
    // 扰动 gameTime，且计时仍从基准继续推进（未被重置为 0）。
    EXPECT_DOUBLE_EQ(Time::GetGameTimeD(), before);
    EXPECT_GE(Time::GetTimeD(), 0.0);
}

// ══════════════════════════════════════════════════════════════════════
// 移出的 HRC 依赖覆盖
//
// 以下用例在 9033610 中建立，但依赖 Time::Shutdown() 与
// Time::IsInitialized() —— 这两个 API 只存在于 HRC-3 未提交工作
// （engine/src/Core/Time.cpp 的 +10 行与 PlatformUtils.h 的 +2 行）。
//
// 它们使已提交历史无法独立构建：git clone → clean worktree →
// configure → build 会在本文件报 C2039/C3861。
//
// 本 target 因此只依赖已提交的 Time API。以上用例属于 HRC-specific
// coverage，应在 HRC-3 正式落地后由 HRC / Time 的后续提交重新加入，
// 不应让基础 Time test target 对未提交代码产生硬依赖。
//
//   ColdState_IsInitializedIsFalse
//   Init_SetsInitializedTrue
//   Shutdown_ClearsInitializedFlag
//   Shutdown_ResetsAllObservableState
//   Shutdown_IsNotTerminal_AnyGetterSilentlyReinitializes
//   Shutdown_ThenGetter_ElapsedRestartsFromZero
//   GetTimeD_LazilyInitializesWhenCold
//
// 说明：已提交的 Time API 无任何方式把内部 s_Initialized 置回 false，
// 因此"冷状态"与"Shutdown 后惰性重初始化"在 HRC 落地前无法以其它方式
// 等价覆盖 —— 不是断言写法问题，而是缺少可观测入口。
// ══════════════════════════════════════════════════════════════════════