#pragma once

#include <cstdint>

namespace Engine {

/**
 * @file PlatformUtils.h
 * @brief 高精度跨平台计时器
 *
 * 设计要点：
 *   - 基于 std::chrono::steady_clock，跨平台（C++11 标准）
 *   - 内部记录起始 tick，返回自 Init() 起的 elapsed 时间
 *   - 避免 float64 在极大值时的精度损失（elapsed 方式值域始终在 [0, 几小时]）
 *   - GetTime() 返回 float（向后兼容），GetTimeD() 返回 double（高精度）
 *   - 支持时间缩放（TimeScale），实现慢放/暂停
 *   - 内置 dt 计算，消除各沙盒的重复代码
 *   - 漂移修正：提供 CalibrateAccumulator() 校准 Fixed 累加器
 *
 * 使用方式：
 * @code
 *   // 初始化（主循环前调用一次）
 *   Time::Init();
 *
 *   // 每帧开头（Application 自动调用）
 *   Time::UpdateDeltaTime();
 *
 *   // 获取 dt 和时间
 *   float dt = Time::GetDeltaTime();
 *   double elapsed = Time::GetTimeD();
 *
 *   // 游戏时间（受 TimeScale 影响）
 *   Time::SetTimeScale(0.5f);  // 半速慢放
 *   float gameDt = Time::GetGameDeltaTime();
 *
 *   // 校准 Fixed 累加器，防止累积漂移
 *   accumulator = Time::CalibrateAccumulator(accumulator, fixedDt);
 * @endcode
 */

class Time {
public:
    // ── 生命周期 ──
    static void Init();

    // ── 时间获取（自 Init 起的经过时间） ──
    static float  GetTime();       // 秒（float，向后兼容）
    static double GetTimeD();      // 秒（double，高精度，推荐）
    static int64_t GetTicks();     // 原始 tick
    static int64_t GetTickFrequency();  // tick/秒

    // ── 游戏时间（受 TimeScale 影响） ──
    static float  GetGameTime();
    static double GetGameTimeD();
    static float  GetTimeScale();
    static void   SetTimeScale(float scale);
    static void   Pause()  { SetTimeScale(0.0f); }
    static void   Resume() { SetTimeScale(1.0f); }

    // ── Delta Time（引擎自动维护） ──
    static float  GetDeltaTime();      // 上一帧 dt（float）
    static double GetDeltaTimeD();     // 上一帧 dt（double 高精度）
    static float  GetGameDeltaTime();  // 受 TimeScale 影响的 dt
    static void   UpdateDeltaTime(float maxDt = 0.25f);  // 每帧调用一次

    // ── 漂移修正工具 ──
    static double GetElapsedSinceInit();
    static double CalibrateAccumulator(double accumulator, double fixedDt);

private:
    static bool    s_Initialized;
    static double  s_InitTimeD;       // Init 时的基准时间（double 秒）
    static double  s_LastFrameTimeD;  // 上一帧时间
    static float   s_DeltaTime;       // 当前帧 dt
    static double  s_DeltaTimeD;      // 高精度 dt
    static float   s_TimeScale;       // 时间缩放
    static double  s_GameTime;        // 累计游戏时间
};

} // namespace Engine