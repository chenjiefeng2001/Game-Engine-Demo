/**
 * @file TestTime.h
 * @brief 测试用模拟时间控制
 *
 * 替代引擎内部的实时时钟，使测试可重复、确定。
 */
#pragma once

class TestTime {
public:
    static double GetNow() { return currentTime; }
    static void SetTime(double t) { currentTime = t; }
    static void Advance(double dt) { currentTime += dt; }
    static void Reset() { currentTime = 0.0; }

private:
    static inline double currentTime = 0.0;
};