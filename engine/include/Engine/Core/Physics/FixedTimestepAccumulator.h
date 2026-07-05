#pragma once

/**
 * @file FixedTimestepAccumulator.h
 * @brief 固定时间步长累加器 — 物理步进与渲染帧率解耦
 *
 * 原理：
 *   物理必须以固定时间步长（如 1/60s）运行以保证数学稳定性。
 *   但渲染帧率是浮动的（144fps → 7ms, 30fps → 33ms）。
 *   累加器负责：
 *     1. 将真实 dt 分割为多个固定步长
 *     2. 记录剩余时间比例作为 alpha（用于渲染插值）
 *
 * 渲染插值只在渲染系统局部计算，不写回 TransformComponent，
 * 避免"状态漂移"（AI 寻路等逻辑读取插值坐标导致的物理不稳定）。
 */

#include "Engine/Types.h"

namespace Engine {

class FixedTimestepAccumulator {
public:
    explicit FixedTimestepAccumulator(float32 fixedDt = 1.0f / 60.0f,
                                       uint32 maxSubSteps = 4)
        : m_FixedDt(fixedDt)
        , m_MaxSubSteps(maxSubSteps)
    {}

    /**
     * @brief 推进累加器
     * @param realDt 实际帧时间（秒）
     * @return 需要执行的物理步数（可能为 0）
     *
     * 累计超过固定步长时，每满一个步长返回 1。
     * 为防止螺旋式死亡（spiral of death），当累计器超过上限时截断。
     * 上限由 m_MaxSubSteps 控制（默认 4 步，对应约 66ms 帧时间）。
     */
    int32 Advance(float32 realDt) {
        m_Accumulator += realDt;

        // 防止螺旋式死亡：上限 = maxSubSteps * fixedDt
        float32 maxAccum = m_FixedDt * static_cast<float32>(m_MaxSubSteps);
        if (m_Accumulator > maxAccum) {
            m_Accumulator = maxAccum;
        }

        int32 steps = 0;
        while (m_Accumulator >= m_FixedDt) {
            m_Accumulator -= m_FixedDt;
            steps++;
        }

        // alpha 用于渲染插值：0.0=上一帧位置, 1.0=下一帧位置
        m_Alpha = m_Accumulator / m_FixedDt;
        return steps;
    }

    /** 获取渲染插值因子（0~1） */
    float32 GetAlpha() const { return m_Alpha; }

    /** 获取固定步长 */
    float32 GetFixedDt() const { return m_FixedDt; }

    /** 获取最大子步数 */
    uint32 GetMaxSubSteps() const { return m_MaxSubSteps; }

    /** 重置累加器 */
    void Reset() {
        m_Accumulator = 0.0f;
        m_Alpha = 0.0f;
    }

private:
    float32 m_FixedDt      = 1.0f / 60.0f;   // 固定物理步长
    uint32  m_MaxSubSteps  = 4;               // 最大子步数上限
    float32 m_Accumulator  = 0.0f;             // 累积时间
    float32 m_Alpha        = 0.0f;             // 渲染插值因子
};

} // namespace Engine