#pragma once

/**
 * @file SystemTestApp.h
 * @brief 系统功能综合测试 — 日志、线程调度、计时器、混合驱动、Adaptive 模式、控制台
 *
 * 测试内容：
 *   1. Log 系统：各级别日志输出、子系统独立 Logger
 *   2. JobSystem：ParallelFor 并行 + 依赖调度
 *   3. Time 类：高精度计时器、时间缩放、漂移修正
 *   4. SubsystemConfig：多阶段并行子系统注册
 *   5. Adaptive 双模式消息泵
 *   6. 游戏内置控制台：命令注册、执行、CVar、历史、Tab 补全
 *   7. 退出时打印汇总报告
 */

#include <Engine/Application.h>
#include <Engine/Platform/PlatformUtils.h>
#include <Engine/Core/Log.h>
#include <Engine/Core/JobSystem.h>
#include <Engine/ConsolePanel.h>
#include <Engine/ConsoleCommandRegistry.h>
#include <Engine/ConsoleVariable.h>
#include <Engine/MemoryPanel.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/Physics/IPhysicsWorld.h>
#include <Engine/Core/Physics/PhysicsDefs.h>
#include <Engine/Core/GameObject/SpriteComponent.h>
#include <Engine/Core/Physics/PhysicsComponent.h>
#include <Engine/Core/Renderer/SpriteBatch.h>
#include <Engine/Core/Renderer/OrthographicCamera.h>
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

        // ── 控制台测试 ──
        /** 注册测试用的控制台命令和 CVar */
        void InitConsoleCommands();

        // ── 内存压力测试 ──
        void RunMemoryStressTest(float32 dt);

        /** 是否启用无敌模式（由 god 命令切换） */
        bool m_GodMode = false;

        /** 是否启用穿墙模式（由 noclip 命令切换） */
        bool m_NoClip = false;

        /** 是否启用内存压力测试（由 memory_stress 命令切换） */
        bool m_EnableMemoryStress = false;

        /** 测试用 CVar 示例 */
        CVar<float32> m_PlayerSpeed{"player_speed", "玩家速度倍率", 1.0f};
        CVar<int32>   m_MaxEnemies{"max_enemies", "最大敌人数", 10};
        CVar<bool>    m_EnableDebugDraw{"debug_draw", "启用调试绘制", false};

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

        // ── 精灵碰撞场景 ──
        void InitSpriteScene();
        void UpdateSpriteScene(float32 dt);
        void RenderSpriteScene();
        void SpawnRandomSprite();

        bool m_SpriteSceneActive = false;
        std::shared_ptr<IPhysicsWorld> m_PhysicsWorld;
        Scene m_Scene;
        std::shared_ptr<ISpriteBatch> m_SpriteBatch;
        std::shared_ptr<Texture> m_TestTexture;
        OrthographicCamera m_SpriteCamera;
        std::vector<std::shared_ptr<GameObject>> m_SpriteObjects;

        // ── 控制台面板 ──
        ConsolePanel m_ConsolePanel;
        MemoryPanel  m_MemoryPanel;

        // ── 内存压力测试状态 ──
        float32 m_MemStressTimer = 0.0f;
        int32   m_MemStressCycle = 0;
        std::vector<std::vector<char>> m_LeakedBlocks;  // 模拟持续泄漏

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
