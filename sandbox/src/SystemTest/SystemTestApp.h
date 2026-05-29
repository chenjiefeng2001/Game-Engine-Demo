#pragma once

/**
 * @file SystemTestApp.h
 * @brief 系统功能综合测试 — 日志、线程调度、计时器、混合驱动、Adaptive 模式
 *
 * 测试内容：
 *   1. Log 系统：各级别日志输出、子系统独立 Logger
 *   2. JobSystem：ParallelFor 并行 + 依赖调度
 *   3. Time 类：高精度计时器、时间缩放、漂移修正
 *   4. SubsystemConfig：多阶段并行子系统注册
 *   5. Adaptive 双模式消息泵
 *   6. 退出时打印汇总报告
 */

#include <Engine/Application.h>
#include <Engine/Platform/PlatformUtils.h>
#include <Engine/Core/Log.h>
#include <Engine/Core/JobSystem.h>
#include <memory>
#include <vector>
#include <atomic>

namespace Engine {

    class SystemTestApp : public Application {
    public:
        explicit SystemTestApp(IGraphicsFactory& factory);
        ~SystemTestApp() override;

    protected:
        void OnStartup() override;
        void OnUpdate(float32 dt) override;
        void OnImGui() override;
        void OnRender() override;

    private:
        // ── 测试子系统回调 ──

        /** 模拟物理 Step（固定步长，Physics 阶段） */
        void UpdatePhysics(float32 dt);

        /** 模拟粒子更新（可变步长，PostPhysics 阶段） */
        void UpdateParticles(float32 dt);

        /** 模拟动画混合（可变步长，PostPhysics 阶段） */
        void UpdateAnimation(float32 dt);

        /** 模拟 AI 决策（限频，PrePhysics 阶段） */
        void UpdateAI(float32 dt);

        /** 模拟音频混合（可变步长，LateUpdate 阶段，后台继续） */
        void UpdateAudio(float32 dt);

        // ── Job 系统测试 ──
        void RunJobSystemTest();
        void RunParallelForTest();

        // ── Time 类测试 ──
        void RunTimePrecisionTest();
        void RunTimeScaleTest();

        // ── 指标 ──
        uint32 m_FrameCount = 0;
        float32 m_Elapsed = 0.0f;
        float32 m_PhysicsAccum = 0.0f;
        float32 m_ParticleAccum = 0.0f;
        float32 m_AnimationAccum = 0.0f;
        float32 m_LastLogTime = 0.0f;

        // ── 子系统注册 ID ──
        uint32 m_AudioSubsystemId = 0;

        // ── Job 系统测试结果 ──
        std::atomic<int32> m_JobCounter{ 0 };
        bool m_JobTestPassed = false;
        bool m_TimeTestPassed = false;
        bool m_ParallelForPassed = false;

        // ── 统计 ──
        struct PhaseTiming {
            float32 min = 1e6f;
            float32 max = 0.0f;
            float32 avg = 0.0f;
            uint32  count = 0;
        };
        PhaseTiming m_PhysicsTiming;
        PhaseTiming m_ParticleTiming;
        PhaseTiming m_AnimationTiming;
        PhaseTiming m_AITiming;
        PhaseTiming m_AudioTiming;
        PhaseTiming m_JobSystemTiming;

        void RecordTiming(PhaseTiming& t, float32 dt);
    };

} // namespace Engine
