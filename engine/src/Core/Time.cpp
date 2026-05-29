/**
 * @file Time.cpp
 * @brief 高精度跨平台计时器实现
 *
 * 基于 std::chrono::steady_clock，在所有 C++11 平台一致工作。
 * 使用 elapsed 方式（now - initTime）避免 double 在大数值时的精度损失。
 * 提供时间缩放、内置 dt 计算和 Fixed 累加器漂移修正。
 */

#include "Engine/Platform/PlatformUtils.h"
#include <chrono>
#include <cmath>

namespace Engine {

// ============================================================
// 静态成员定义
// ============================================================

bool   Time::s_Initialized    = false;
double Time::s_InitTimeD      = 0.0;
double Time::s_LastFrameTimeD = 0.0;
float  Time::s_DeltaTime      = 0.0f;
double Time::s_DeltaTimeD     = 0.0;
float  Time::s_TimeScale      = 1.0f;
double Time::s_GameTime       = 0.0;

// ============================================================
// 辅助：获取当前高精度时间（秒，double）
//
// 使用 duration_cast<duration<double>> 自动处理各平台
// 不同 tick 频率到秒的转换，代码完全跨平台。
// ============================================================

static double NowD() {
    using namespace std::chrono;
    auto now = steady_clock::now().time_since_epoch();
    return duration_cast<duration<double>>(now).count();
}

// ============================================================
// 生命周期
// ============================================================

void Time::Init() {
    s_InitTimeD      = NowD();
    s_LastFrameTimeD = 0.0;  // 与 GetTimeD() 单位一致：经过时间
    s_DeltaTime      = 0.0f;
    s_DeltaTimeD     = 0.0;
    s_TimeScale      = 1.0f;
    s_GameTime       = 0.0;
    s_Initialized    = true;
}

// ============================================================
// 时间获取
// ============================================================

float Time::GetTime() {
    return static_cast<float>(GetTimeD());
}

double Time::GetTimeD() {
    if (!s_Initialized) Init();
    // ✅ elapsed 方式：值域始终在 [0, 几小时]，double 保持 ~1μs 精度
    return NowD() - s_InitTimeD;
}

int64_t Time::GetTicks() {
    using namespace std::chrono;
    return steady_clock::now().time_since_epoch().count();
}

int64_t Time::GetTickFrequency() {
    using namespace std::chrono;
    // steady_clock::period =  tick 之间的秒数（分数）
    // period::den = 每秒的 tick 数
    return steady_clock::period::den;
}

// ============================================================
// 游戏时间（受 TimeScale 影响）
// ============================================================

float Time::GetGameTime() {
    return static_cast<float>(s_GameTime);
}

double Time::GetGameTimeD() {
    return s_GameTime;
}

float Time::GetTimeScale() {
    return s_TimeScale;
}

void Time::SetTimeScale(float scale) {
    s_TimeScale = (scale < 0.0f) ? 0.0f : scale;
}

// ============================================================
// Delta Time
// ============================================================

float Time::GetDeltaTime() {
    return s_DeltaTime;
}

double Time::GetDeltaTimeD() {
    return s_DeltaTimeD;
}

float Time::GetGameDeltaTime() {
    return s_DeltaTime * s_TimeScale;
}

void Time::UpdateDeltaTime(float maxDt) {
    double now = GetTimeD();    // elapsed 时间
    double dt  = now - s_LastFrameTimeD;

    // ── 钳制，防止长时间卡顿导致的 dt 爆炸 ──
    if (dt > maxDt)  dt = maxDt;
    if (dt < 0.0)    dt = 0.0;  // 防止计时器向后跳（极少发生）

    s_DeltaTime    = static_cast<float>(dt);
    s_DeltaTimeD   = dt;
    s_LastFrameTimeD = now;

    // ── 更新游戏时间 ──
    s_GameTime += static_cast<double>(s_TimeScale) * dt;
}

// ============================================================
// 漂移修正工具
// ============================================================

double Time::GetElapsedSinceInit() {
    return GetTimeD();  // 等价于 NowD() - s_InitTimeD
}

double Time::CalibrateAccumulator(double accumulator, double fixedDt) {
    (void)accumulator;  // 忽略旧累加器值
    // ✅ 用绝对 elapsed 时间计算理论上应有的 accumulator
    // 彻底消除长期累加带来的浮点累积误差
    double elapsed = GetElapsedSinceInit();
    return std::fmod(elapsed, fixedDt);
}

} // namespace Engine
