/**
 * @file SystemTestApp.cpp
 * @brief 系统功能综合测试实现
 */

#include "SystemTestApp.h"
#include <Engine/Debug/CrashContext.h>
#include <Engine/ConsolePanel.h>
#include <Engine/ConsoleCommandRegistry.h>
#include <Engine/ConsoleVariable.h>
#include <Engine/MemoryTracker.h>
#include <Engine/MemoryPanel.h>
#include <Engine/Core/Input.h>
#include <Engine/Box2D/Box2DPhysicsWorld.h>
#include <imgui.h>
#include <cmath>
#include <random>

namespace Engine {

// ============================================================
// 构造 / 析构
// ============================================================

SystemTestApp::SystemTestApp(IGraphicsFactory& factory)
    : Application(factory)
{
}

SystemTestApp::~SystemTestApp() {
    // ── 退出时打印汇总报告 ──
    Log::Info("═══════════════════════════════════════");
    Log::Info("  SystemTest Summary");
    Log::Info("═══════════════════════════════════════");
    Log::Info("  Total frames:     {}", m_FrameCount);
    Log::Info("  Total time:       {:.2f}s", m_Elapsed);
    Log::Info("  JobSystem test:   {}", m_JobTestPassed ? "PASS" : "FAIL");
    Log::Info("  ParallelFor test: {}", m_ParallelForPassed ? "PASS" : "FAIL");
    Log::Info("  Time precision:   {}", m_TimeTestPassed ? "PASS" : "FAIL");
    Log::Info("  Physics avg:      {:.3f}ms", m_PhysicsTiming.avg * 1000.0f);
    Log::Info("  Particles avg:    {:.3f}ms", m_ParticleTiming.avg * 1000.0f);
    Log::Info("  Animation avg:    {:.3f}ms", m_AnimationTiming.avg * 1000.0f);
    Log::Info("  AI avg:           {:.3f}ms", m_AITiming.avg * 1000.0f);
    Log::Info("  Audio avg:        {:.3f}ms", m_AudioTiming.avg * 1000.0f);
    Log::Info("═══════════════════════════════════════");
}

// ============================================================
// 子系统回调
// ============================================================

void SystemTestApp::UpdatePhysics(float32 dt) {
    auto t0 = Time::GetTimeD();
    // 模拟 Box2D Step：固定耗时 ~2ms
    float32 work = 0.002f * (1.0f + 0.5f * std::sin(m_PhysicsAccum));
    m_PhysicsAccum += dt;
    // Busy-wait 模拟计算
    double target = Time::GetTimeD() + work;
    while (Time::GetTimeD() < target) { /* spin */ }
    auto t1 = Time::GetTimeD();
    RecordTiming(m_PhysicsTiming, static_cast<float32>(t1 - t0));
}

void SystemTestApp::UpdateParticles(float32 dt) {
    auto t0 = Time::GetTimeD();
    // 模拟粒子更新：~1ms，可变耗时
    float32 work = 0.001f * (1.0f + 0.3f * std::cos(m_ParticleAccum));
    m_ParticleAccum += dt;
    double target = Time::GetTimeD() + work;
    while (Time::GetTimeD() < target) { /* spin */ }
    auto t1 = Time::GetTimeD();
    RecordTiming(m_ParticleTiming, static_cast<float32>(t1 - t0));
}

void SystemTestApp::UpdateAnimation(float32 dt) {
    auto t0 = Time::GetTimeD();
    // 模拟动画混合：~1.5ms
    float32 work = 0.0015f;
    double target = Time::GetTimeD() + work;
    while (Time::GetTimeD() < target) { /* spin */ }
    auto t1 = Time::GetTimeD();
    RecordTiming(m_AnimationTiming, static_cast<float32>(t1 - t0));
}

void SystemTestApp::UpdateAI(float32 dt) {
    auto t0 = Time::GetTimeD();
    // 模拟 AI 决策：~0.5ms
    float32 work = 0.0005f;
    double target = Time::GetTimeD() + work;
    while (Time::GetTimeD() < target) { /* spin */ }
    auto t1 = Time::GetTimeD();
    RecordTiming(m_AITiming, static_cast<float32>(t1 - t0));
}

void SystemTestApp::UpdateAudio(float32 dt) {
    auto t0 = Time::GetTimeD();
    // 模拟音频混合：~0.8ms
    float32 work = 0.0008f;
    double target = Time::GetTimeD() + work;
    while (Time::GetTimeD() < target) { /* spin */ }
    auto t1 = Time::GetTimeD();
    RecordTiming(m_AudioTiming, static_cast<float32>(t1 - t0));
}

// ============================================================
// Job 系统测试
// ============================================================

void SystemTestApp::RunJobSystemTest() {
    Log::Info("[Test] Running JobSystem test...");
    auto* js = JobSystem::Get();
    if (!js) {
        Log::Error("[Test] JobSystem not initialized!");
        return;
    }

    // 测试 1：调度并行 Job，验证所有任务执行完毕
    m_JobCounter.store(0);
    constexpr int32 NUM_JOBS = 100;

    std::vector<JobHandle> handles;
    for (int32 i = 0; i < NUM_JOBS; ++i) {
        auto handle = js->Schedule([this, i](uint32) {
            m_JobCounter.fetch_add(1, std::memory_order_release);
            // 模拟少量计算
            volatile int32 sum = 0;
            for (int32 j = 0; j < 1000; ++j) sum += j;
        });
        handles.push_back(handle);
    }

    // 等待所有 Job 完成
    for (auto& h : handles) {
        js->Wait(h);
    }

    bool passed = (m_JobCounter.load(std::memory_order_acquire) == NUM_JOBS);
    Log::Info("[Test]   Schedule {} jobs: {} (count={})",
              NUM_JOBS, passed ? "PASS" : "FAIL", m_JobCounter.load());
    m_JobTestPassed = passed;
}

void SystemTestApp::RunParallelForTest() {
    Log::Info("[Test] Running ParallelFor test...");
    auto* js = JobSystem::Get();
    if (!js) return;

    constexpr int32 COUNT = 10000;
    std::atomic<int32> sum{ 0 };

    auto handle = js->ParallelFor(0, COUNT, [&sum](int32 i) {
        sum.fetch_add(i, std::memory_order_relaxed);
    });

    js->Wait(handle);

    int32 expected = COUNT * (COUNT - 1) / 2;
    bool passed = (sum.load() == expected);
    Log::Info("[Test]   ParallelFor 0..{} sum: {} (expected={}, got={})",
              COUNT - 1, passed ? "PASS" : "FAIL", expected, sum.load());
    m_ParallelForPassed = passed;
}

// ============================================================
// Time 类测试
// ============================================================

void SystemTestApp::RunTimePrecisionTest() {
    Log::Info("[Test] Running Time precision test...");

    // 测试 GetTimeD() 的单调性和精度
    double t0 = Time::GetTimeD();
    double t1 = Time::GetTimeD();
    double dt = t1 - t0;

    // 两次调用应该非常接近（< 1ms）
    bool monotonic = (dt >= 0.0);
    bool precise = (dt < 0.001);

    // 测试 GetTicks() 频率
    int64_t freq = Time::GetTickFrequency();
    Log::Info("[Test]   Tick frequency: {} Hz", freq);
    Log::Info("[Test]   Monotonic: {}", monotonic ? "PASS" : "FAIL");
    Log::Info("[Test]   Precision:  {} (dt={:.6f}ms)", precise ? "PASS" : "FAIL", dt * 1000.0);

    // 验证 ElapsedSinceInit
    double elapsed = Time::GetElapsedSinceInit();
    Log::Info("[Test]   Elapsed since init: {:.3f}s", elapsed);

    m_TimeTestPassed = monotonic && precise;
}

void SystemTestApp::RunTimeScaleTest() {
    Log::Info("[Test] Running TimeScale test...");

    // 设置半速
    Time::SetTimeScale(0.5f);
    Log::Info("[Test]   TimeScale set to 0.5");

    // 验证 GetTimeScale
    float32 scale = Time::GetTimeScale();
    Log::Info("[Test]   GetTimeScale() = {}", scale);

    // 验证 Pause/Resume
    Time::Pause();
    Log::Info("[Test]   Paused, GetTimeScale() = {}", Time::GetTimeScale());
    Time::Resume();
    Log::Info("[Test]   Resumed, GetTimeScale() = {}", Time::GetTimeScale());

    // 恢复
    Time::SetTimeScale(1.0f);
    Log::Info("[Test]   TimeScale restored to 1.0");
}

// ============================================================
// OnStartup — 注册所有测试子系统
// ============================================================

void SystemTestApp::OnStartup() {
    Log::Info("═══════════════════════════════════════");
    Log::Info("  SystemTest Starting");
    Log::Info("═══════════════════════════════════════");

    // ── 设置 Adaptive 模式 ──
    SetLoopMode(LoopMode::Adaptive);

    // ── 使用不同的 Logger 标签 ──
    auto sysLog = Log::Get("SystemTest");

    // ── 注册子系统，各阶段不同 ──

    // AI：PrePhysics 阶段，限频 30Hz
    RegisterUpdateSubsystem("AI",
        [this](float32 dt) { UpdateAI(dt); },
        SubsystemConfig::Throttled(30.0f)
            .WithPhase(SubsystemExecPhase::PrePhysics)
    );

    // 物理：Physics 阶段，固定步长 1/60s
    RegisterUpdateSubsystem("Physics",
        [this](float32 dt) { UpdatePhysics(dt); },
        SubsystemConfig::Fixed(1.0f / 60.0f)
    );

    // 粒子：PostPhysics 阶段，可变步长
    RegisterUpdateSubsystem("Particles",
        [this](float32 dt) { UpdateParticles(dt); },
        SubsystemConfig::Default()
            .WithPhase(SubsystemExecPhase::PostPhysics)
    );

    // 动画：PostPhysics 阶段（与粒子并行），可变步长
    RegisterUpdateSubsystem("Animation",
        [this](float32 dt) { UpdateAnimation(dt); },
        SubsystemConfig::Default()
            .WithPhase(SubsystemExecPhase::PostPhysics)
    );

    // 音频：LateUpdate 阶段，后台继续运行
    m_AudioSubsystemId = RegisterUpdateSubsystem("Audio",
        [this](float32 dt) { UpdateAudio(dt); },
        SubsystemConfig::Default()
            .WithPhase(SubsystemExecPhase::LateUpdate)
            .WithBackground(true)
    );

    // 设置独立的 Logger 级别
    Log::Get("Physics")->set_level(spdlog::level::warn);  // 物理只记录 warn+
    Log::Get("Audio")->set_level(spdlog::level::info);

    sysLog->info("All subsystems registered");

    // ── 注册 Job 系统关机的清理回调 ──
    RegisterSubsystem("JobSystem", SubsystemPhase::Custom,
        []() { return true; },
        []() { JobSystem::Shutdown(); }
    );

    // ── 初始化控制台系统 ──
    InitConsoleCommands();
    Application::SetConsolePanel(&m_ConsolePanel);
    sysLog->info("Console system initialized (press ~ to toggle)");

    // ── 默认启动精灵碰撞场景 ──
    InitSpriteScene();
    m_SpriteSceneActive = true;

    // ── 运行一次性测试 ──
    RunTimePrecisionTest();
    RunTimeScaleTest();
    RunJobSystemTest();
    RunParallelForTest();

    sysLog->info("Initialization complete, entering main loop");
    Log::Info("═══════════════════════════════════════");
}

// ============================================================
// InitConsoleCommands — 注册测试命令和 CVar
// ============================================================

void SystemTestApp::InitConsoleCommands()
{
    // 这些 CVar 已经在成员初始化列表中创建（m_PlayerSpeed, m_MaxEnemies, m_EnableDebugDraw）
    // 所以会自动注册到 CVarRegistry

    auto& registry = ConsoleCommandRegistry::Instance();

    // ── god — 切换无敌模式 ──
    registry.Register({"god", "切换无敌模式", "god",
        [this](const std::vector<std::string>&, std::string& out) {
            m_GodMode = !m_GodMode;
            out = m_GodMode ? "^2God mode ON^7 - 你已无敌！" : "^1God mode OFF^7";
        }
    });

    // ── noclip — 切换穿墙模式 ──
    registry.Register({"noclip", "切换穿墙模式", "noclip",
        [this](const std::vector<std::string>&, std::string& out) {
            m_NoClip = !m_NoClip;
            out = m_NoClip ? "^2Noclip ON^7 - 自由飞行！" : "^1Noclip OFF^7";
        }
    });

    // ── spawn — 生成实体 ──
    registry.Register({"spawn", "生成指定数量的实体", "spawn <count>",
        [this](const std::vector<std::string>& args, std::string& out) {
            int32 count = 1;
            if (args.size() > 1) {
                try { count = std::max(1, std::stoi(args[1])); }
                catch (...) { out = "Invalid count: " + args[1]; return; }
            }
            out = "Spawned " + std::to_string(count) + " entities! (simulated)";
        }
    });

    // ── tp — 传送 ──
    registry.Register({"tp", "传送到指定坐标", "tp <x> <y>",
        [this](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 3) { out = "Usage: tp <x> <y>"; return; }
            try {
                float x = std::stof(args[1]);
                float y = std::stof(args[2]);
                out = "Teleported to (" + std::to_string(x) + ", " + std::to_string(y) + ")";
            } catch (...) { out = "Invalid coordinates"; }
        }
    });

    // ── test_log — 测试日志输出 ──
    registry.Register({"test_log", "测试各级别日志输出", "test_log [info|warn|error]",
        [this](const std::vector<std::string>& args, std::string& out) {
            std::string level = (args.size() > 1) ? args[1] : "info";
            if (level == "info") {
                Log::Info("[ConsoleTest] This is an INFO message");
                out = "Logged INFO";
            } else if (level == "warn") {
                Log::Warn("[ConsoleTest] This is a WARNING message");
                out = "Logged WARN";
            } else if (level == "error") {
                Log::Error("[ConsoleTest] This is an ERROR message");
                out = "Logged ERROR";
            } else {
                out = "Unknown level: " + level + " (use info|warn|error)";
            }
        }
    });

    // ── status — 显示系统状态 ──
    registry.Register({"status", "显示当前系统状态", "status",
        [this](const std::vector<std::string>&, std::string& out) {
            out = "^5=== System Status ===^7\n";
            out += "  ^5God mode:^7       " + std::string(m_GodMode ? "^2ON^7" : "^1OFF^7") + "\n";
            out += "  ^5Noclip:^7         " + std::string(m_NoClip ? "^2ON^7" : "^1OFF^7") + "\n";
            out += "  ^5Player speed:^7   " + std::to_string(m_PlayerSpeed.Get()) + "\n";
            out += "  ^5Max enemies:^7    " + std::to_string(m_MaxEnemies.Get()) + "\n";
            out += "  ^5Debug draw:^7     " + std::string(m_EnableDebugDraw ? "^2ON^7" : "^1OFF^7") + "\n";
            out += "  ^5Frame count:^7    " + std::to_string(m_FrameCount) + "\n";
            out += "  ^5Elapsed time:^7   " + std::to_string(m_Elapsed) + "s";
        }
    });

    // ── test_colors — 输出所有颜色代码的示例 ──
    registry.Register({"test_colors", "输出颜色代码渲染测试", "test_colors", 
        [](const std::vector<std::string>&, std::string& out) {
            out = "^5=== Console Color Palette ===^7\n";
            out += "^0^0 White (#FFFFFF)   ^1^1 Red (#FF3333)^7\n";
            out += "^2^2 Green (#88CC00)   ^3^3 Yellow (#00CCFF)^7\n";
            out += "^4^4 Blue (#FF8800)    ^5^5 Cyan (#FFCC00)^7\n";
            out += "^6^6 Magenta (#AA88FF) ^7^7 Light Gray (#CCCCCC)^7\n";
            out += "^8^8 Orange (#2288FF)  ^9^9 Dark Gray (#888888)^7\n";
            out += "\n^5Usage in commands:^7\n";
            out += "  Use ^2^2text^7  for green\n";
            out += "  Use ^1^1text^7  for red\n";
            out += "  Use ^5^5label:^7 for labels\n";
            out += "  Use ^^7 for literal caret\n";
            out += "\n^3Tip:^7 Colors work in any command that uses ^0~^9 codes!^7";
        }
    });

    // ── memory_test — 模拟内存分配活动（用于验证 MemoryPanel） ──
    registry.Register({"memory_test", "模拟内存分配活动，测试 MemoryPanel", "memory_test [burst|steady|clear]",
        [](const std::vector<std::string>& args, std::string& out) {
            std::string mode = (args.size() > 1) ? args[1] : "burst";
            if (mode == "clear") {
                // 仅演示，无法真正"清空"已有分配 — 但模拟停止
                out = "^2Memory test stopped.^7";
                return;
            }

            if (mode == "burst") {
                // 突发分配：多次 new + delete，产生锯齿状内存图
                out = "^2Burst test:^7 allocating/deallocating blocks...\n";
                for (int i = 0; i < 50; ++i) {
                    size_t sizes[] = { 64, 128, 256, 512, 1024, 2048 };
                    size_t sz = sizes[i % 6];
                    char* p = new char[sz];
                    delete[] p;
                }
                out += "  50 alloc/free cycles completed.\n";
                out += "Check MemoryPanel to see the activity.";
            } else if (mode == "steady") {
                // 持续分配（保留不释放），模拟内存增长
                out = "^2Steady growth test:^7 allocating retained blocks...\n";
                for (int i = 0; i < 20; ++i) {
                    char* p = new char[4096]; // 4KB, 故意不释放
                    (void)p;
                }
                out += "  20 x 4KB allocated (retained).\n";
                out += "Memory usage should increase by ~80KB.";
            } else {
                out = "^1Unknown mode:^7 " + mode + ". Use: burst | steady | clear";
            }
        }
    });

    // ── memory_stress — 持续内存压力测试 ──
    registry.Register({"memory_stress", "启动/停止持续内存压力测试", "memory_stress [on|off]",
        [this](const std::vector<std::string>& args, std::string& out) {
            if (args.size() > 1 && args[1] == "off") {
                m_EnableMemoryStress = false;
                m_LeakedBlocks.clear();
                out = "^2Memory stress test STOPPED.^7 All leaked blocks cleared.";
            } else {
                m_EnableMemoryStress = true;
                out = "^2Memory stress test STARTED.^7\n"
                      "  ~0.5s: temp alloc/free (4-64KB)\n"
                      "  ~2s:   persistent 16KB blocks\n"
                      "  ~5s:   release oldest block\n"
                      "Check ^5Memory Panel^7 for live graph!";
            }
        }
    });

    // ── sprites — 切换精灵碰撞场景 ──
    registry.Register({"sprites", "启动/停止精灵碰撞场景", "sprites [on|off]",
        [this](const std::vector<std::string>& args, std::string& out) {
            if (args.size() > 1 && args[1] == "off") {
                m_SpriteSceneActive = false;
                out = "^2Sprite scene DEACTIVATED.^7";
            } else {
                m_SpriteSceneActive = true;
                out = "^2Sprite scene ACTIVE.^7\n"
                      "  " + std::to_string(m_SpriteObjects.size()) + " sprites\n"
                      "  Gravity: 9.81 m/s^2\n"
                      "  Collision: random color change\n"
                      "  Auto-spawn: 1 sprite/sec (max 100)";
            }
        }
    });

    // ── 输出帮助提示 ──
    auto sysLog = Log::Get("SystemTest");
    sysLog->info("--- Console Test Commands ---");
    sysLog->info("  ~              : Toggle console");
    sysLog->info("  god            : Toggle god mode");
    sysLog->info("  noclip         : Toggle noclip");
    sysLog->info("  spawn <n>      : Spawn entities");
    sysLog->info("  tp <x> <y>     : Teleport");
    sysLog->info("  set/get        : CVar manipulation");
    sysLog->info("  status         : Show system state");
    sysLog->info("  test_log       : Test log levels");
    sysLog->info("  test_colors    : Show color palette test");
    sysLog->info("  timescale <v>  : Set game speed (0=pause, 0.5=slow, 1=normal, 2=fast)");
    sysLog->info("  pause/resume   : Pause / Resume game");
    sysLog->info("  slowmo         : Toggle slow motion (0.5x)");
    sysLog->info("  speed          : Show current speed");
    sysLog->info("  profiler       : Show Tracy Profiler status");
    sysLog->info("  help           : List all commands");
}

// ============================================================
// OnUpdate — 每帧统计
// ============================================================

void SystemTestApp::OnUpdate(float32 dt) {
    m_FrameCount++;
    m_Elapsed += dt;

    // ── 控制台测试：演示 god_mode / noclip 对游戏逻辑的影响 ──
    if (m_GodMode) {
        // 无敌模式：模拟自动回血等效果（仅用于演示）
        // 实际游戏中这里会跳过伤害检测
    }

    if (m_NoClip) {
        // 穿墙模式：模拟飞行或碰撞忽略（仅用于演示）
        // 实际游戏中这里会禁用物理碰撞
    }

    // 使用 CVar 值影响游戏运行（演示）
    float32 speedMultiplier = m_PlayerSpeed.Get();
    int32   maxEnemies      = m_MaxEnemies.Get();
    bool    debugDraw       = m_EnableDebugDraw.Get();

    // ── 内存压力测试 ──
    if (m_EnableMemoryStress) {
        RunMemoryStressTest(dt);
    }

    // ── 精灵场景更新 ──
    if (m_SpriteSceneActive) {
        UpdateSpriteScene(dt);
    }

    // 仅在 debug_draw 开启时记录（减少日志噪声）
    if (debugDraw && m_FrameCount % 60 == 0) {
        Log::Get("SystemTest")->debug("[DebugDraw] speed={:.1f}, maxEnemies={}",
                                       speedMultiplier, maxEnemies);
    }

    // 每 5 秒输出一次汇总日志
    if (m_Elapsed - m_LastLogTime >= 5.0f) {
        m_LastLogTime = m_Elapsed;
        Log::Info("[Stats] Frame={} | Time={:.1f}s | Physics={:.2f}ms | "
                  "Particles={:.2f}ms | Anim={:.2f}ms | AI={:.2f}ms | Audio={:.2f}ms",
                  m_FrameCount, m_Elapsed,
                  m_PhysicsTiming.avg * 1000.0f,
                  m_ParticleTiming.avg * 1000.0f,
                  m_AnimationTiming.avg * 1000.0f,
                  m_AITiming.avg * 1000.0f,
                  m_AudioTiming.avg * 1000.0f);

        // 测试 MarkSubsystemDirty（EventDriven 模式预留）
        Log::Get("SystemTest")->debug("Frame {}: dt={:.6f}s, pending jobs={}",
                                       m_FrameCount, dt,
                                       JobSystem::Get()->GetPendingJobCount());
    }
}

// ============================================================
// RunMemoryStressTest — 生成真实的内存分配/释放模式
// ============================================================

void SystemTestApp::RunMemoryStressTest(float32 dt) {
    m_MemStressTimer += dt;
    m_MemStressCycle++;

    // ── 每 0.5 秒：分配+释放临时对象（模拟游戏逻辑对象生命周期） ──
    if (m_MemStressTimer >= 0.5f) {
        m_MemStressTimer = 0.0f;

        // Retail: 分配临时 buffer 然后立即释放
        std::vector<char> temp;
        temp.resize((m_MemStressCycle % 16 + 1) * 4096); // 4KB ~ 64KB 波动

        // Debug: 分配一些诊断数据（仅在 Debug 构建时有额外开销）
        std::vector<int> debugData;
        debugData.resize(256);
        Engine::MemoryTracker::OnAlloc(Engine::MemCategory::Debug, debugData.size() * sizeof(int));

        // Stack: 模拟 StackAllocator 用量波动
        size_t stackUse = (m_MemStressCycle % 8 + 1) * 2048;
        Engine::MemoryTracker::OnAlloc(Engine::MemCategory::Stack, stackUse);
        Engine::MemoryTracker::OnFree(Engine::MemCategory::Stack, stackUse); // 立即释放（栈回滚）
    }

    // ── 每 2 秒：分配一小块长期存在的内存（模拟资源加载） ──
    if (m_MemStressCycle % 120 == 0) {
        std::vector<char> persistent;
        persistent.resize(16384); // 16KB
        m_LeakedBlocks.push_back(std::move(persistent));
    }

    // ── 每 5 秒：释放一块最早的内存（模拟资源卸载） ──
    if (m_MemStressCycle % 300 == 0 && !m_LeakedBlocks.empty()) {
        m_LeakedBlocks.erase(m_LeakedBlocks.begin());
    }

    // ── 每 60 帧：少量分散分配（模拟 UI/字符串临时对象） ──
    if (m_MemStressCycle % 60 == 0) {
        std::string tempStr = "Frame " + std::to_string(m_FrameCount);
        Engine::MemoryTracker::OnAlloc(Engine::MemCategory::Retail, tempStr.capacity());
    }
}

// ============================================================
// InitSpriteScene — 创建精灵碰撞场景
// ============================================================

void SystemTestApp::InitSpriteScene() {
    auto& factory = GetFactory();

    // ── 1. 创建物理世界 ──
    m_PhysicsWorld = std::make_shared<Box2DPhysicsWorld>(Vec2(0.0f, -9.81f));

    // ── 2. 加载纹理 ──
    m_TestTexture = GetTextureManager().Load("assets/textures/test.png");

    // ── 3. 创建 SpriteBatch ──
    auto* ctx = GetRenderContext();
    if (ctx) {
        m_SpriteBatch = factory.CreateSpriteBatch(*ctx);
    }

    // ── 4. 设置摄像机（0-800 x 0-600 与 Box2D 坐标系一致） ──
    m_SpriteCamera = OrthographicCamera(0.0f, 800.0f, 0.0f, 600.0f);

    // ── 5. 创建边界墙（Static 刚体） ──
    auto createWall = [&](float32 x, float32 y, float32 hw, float32 hh) {
        auto wall = std::make_shared<GameObject>("Wall");
        BodyDef def;
        def.type = BodyType::Static;
        def.position = {x, y};
        def.shape.type = ShapeType::Box;
        def.shape.boxSize = {hw, hh};
        wall->AddComponent<PhysicsComponent>()->CreateBody(m_PhysicsWorld, def);
        m_Scene.AddObject(wall);
    };

    float32 wallThickness = 0.5f;
    createWall(400.0f, wallThickness, 400.0f, wallThickness);        // 地面
    createWall(wallThickness, 300.0f, wallThickness, 300.0f);        // 左墙
    createWall(800.0f - wallThickness, 300.0f, wallThickness, 300.0f); // 右墙

    // ── 6. 生成初始精灵 ──
    m_SpriteObjects.clear();
    for (int i = 0; i < 30; ++i) {
        SpawnRandomSprite();
    }

    Log::Info("[SpriteScene] Created: 30 sprites + walls. Gravity: 9.81 m/s^2");
}

void SystemTestApp::SpawnRandomSprite() {
    if (!m_TestTexture) return;

    auto obj = std::make_shared<GameObject>("Sprite");
    float32 x = 100.0f + (float32)(rand() % 600);
    float32 y = 300.0f + (float32)(rand() % 200);

    auto* sprite = obj->AddComponent<SpriteComponent>();
    sprite->SetTexture(m_TestTexture);
    sprite->SetColor((rand() % 100) / 100.0f + 0.3f,
                     (rand() % 100) / 100.0f + 0.3f,
                     (rand() % 100) / 100.0f + 0.3f, 1.0f);

    BodyDef def;
    def.type = BodyType::Dynamic;
    def.position = {x, y};
    def.shape.type = ShapeType::Box;
    def.shape.boxSize = {0.4f, 0.4f};
    def.material.density = 1.0f;
    def.material.friction = 0.5f;
    def.material.restitution = 0.3f;

    auto* physics = obj->AddComponent<PhysicsComponent>();
    physics->CreateBody(m_PhysicsWorld, def);

    physics->SetOnCollisionEnter([this, obj](const ContactInfo&) {
        auto* spr = obj->GetComponent<SpriteComponent>();
        if (spr) spr->SetColor((rand() % 100) / 100.0f + 0.2f,
                                (rand() % 100) / 100.0f + 0.2f,
                                (rand() % 100) / 100.0f + 0.2f, 1.0f);
    });

    m_Scene.AddObject(obj);
    m_SpriteObjects.push_back(obj);
}

void SystemTestApp::UpdateSpriteScene(float32 dt) {
    PROFILE_ZONE_NAME("SpriteScene");
    if (!m_PhysicsWorld) return;
    m_PhysicsWorld->Step(dt, 8, 3);
    m_Scene.Update(dt);

    static float32 spawnTimer = 0.0f;
    spawnTimer += dt;
    if (spawnTimer > 1.0f && m_SpriteObjects.size() < 100) {
        spawnTimer = 0.0f;
        SpawnRandomSprite();
    }
}

void SystemTestApp::RenderSpriteScene() {
    PROFILE_ZONE_NAME("SpriteRender");
    if (!m_SpriteBatch || !m_TestTexture) return;

    m_SpriteBatch->Begin(m_TestTexture);

    for (auto& obj : m_SpriteObjects) {
        auto* pc = obj->GetComponent<PhysicsComponent>();
        auto* sprite = obj->GetComponent<SpriteComponent>();
        if (!pc || !sprite) continue;

        Vec2 pos; float32 angle = 0;
        pc->SyncPhysicsToTransform(pos, angle);

        SpriteData sd;
        sd.transform = SpriteTransform::FromScale(pos.x, pos.y, 0.4f, 0.4f);
        sd.colorR = sprite->GetColor().x;
        sd.colorG = sprite->GetColor().y;
        sd.colorB = sprite->GetColor().z;
        sd.colorA = sprite->GetColor().w;
        m_SpriteBatch->Draw(sd);
    }

    m_SpriteBatch->End();
}

// ============================================================
// OnImGui — 性能面板
// ============================================================

void SystemTestApp::OnImGui() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("SystemTest Dashboard", nullptr,
                     ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("Frame: %u", m_FrameCount);
        ImGui::Text("Elapsed: %.1fs", m_Elapsed);
        ImGui::Text("LoopMode: Adaptive");
        ImGui::Separator();

        ImGui::Text("Job System threads: %u", JobSystem::Get()->GetThreadCount());
        ImGui::Text("Pending jobs: %u", JobSystem::Get()->GetPendingJobCount());
        ImGui::Text("Completed jobs: %u", JobSystem::Get()->GetCompletedJobCount());
        ImGui::Separator();

        ImGui::Text("Tests:");
        ImGui::Text("  JobSystem:       %s", m_JobTestPassed ? "PASS" : "FAIL");
        ImGui::Text("  ParallelFor:     %s", m_ParallelForPassed ? "PASS" : "FAIL");
        ImGui::Text("  Time Precision:  %s", m_TimeTestPassed ? "PASS" : "FAIL");
        ImGui::Separator();

        auto showTiming = [](const char* name, const PhaseTiming& t) {
            ImGui::Text("  %s: avg=%.2fms min=%.2fms max=%.2fms",
                        name, t.avg * 1000, t.min * 1000, t.max * 1000);
        };
        showTiming("Physics",   m_PhysicsTiming);
        showTiming("Particles", m_ParticleTiming);
        showTiming("Animation", m_AnimationTiming);
        showTiming("AI",        m_AITiming);
        showTiming("Audio",     m_AudioTiming);
        ImGui::Separator();

        // ── 控制台测试区域 ──
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Console System:");
        ImGui::Text("  Press ~ to toggle console");
        ImGui::Text("  Console visible:  %s", m_ConsolePanel.IsVisible() ? "YES" : "no");
        ImGui::Text("  Capturing input:  %s", m_ConsolePanel.IsCapturingInput() ? "YES" : "no");

        // 作弊状态
        ImGui::Text("  God mode:         %s", m_GodMode ? "ON" : "off");
        ImGui::Text("  Noclip:           %s", m_NoClip ? "ON" : "off");

        // 按钮快速打开控制台
        if (ImGui::Button("Open Console (~)", ImVec2(160, 0))) {
            m_ConsolePanel.SetVisible(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Execute 'help'", ImVec2(120, 0))) {
            m_ConsolePanel.ExecuteCommand("help");
            m_ConsolePanel.SetVisible(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Test Colors", ImVec2(100, 0))) {
            m_ConsolePanel.ExecuteCommand("test_colors");
            m_ConsolePanel.SetVisible(true);
        }

        ImGui::Separator();

        // ── Profiler 状态 ──
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Profiler:");
        ImGui::Text("  %s", Profiler::IsConnected() ? "CONNECTED" : "broadcasting...");
        if (ImGui::Button("Profiler Status", ImVec2(120, 0))) {
            m_ConsolePanel.ExecuteCommand("profiler status");
            m_ConsolePanel.SetVisible(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Launch Server", ImVec2(110, 0))) {
            m_ConsolePanel.ExecuteCommand("profiler server");
            m_ConsolePanel.SetVisible(true);
        }

        ImGui::Separator();

        // ── 内存统计 ──
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Memory:");
        {
            size_t usage    = MemoryTracker::GetCurrentUsage();
            size_t peak     = MemoryTracker::GetPeakUsage();
            size_t fAlloc   = MemoryTracker::GetFrameAllocBytes();
            size_t fFree    = MemoryTracker::GetFrameFreeBytes();

            ImGui::Text("  Heap: %s  |  Peak: %s",
                MemoryTracker::FormatBytes(usage).c_str(),
                MemoryTracker::FormatBytes(peak).c_str());
            ImGui::Text("  Frame: +%s  /  -%s",
                MemoryTracker::FormatBytes(fAlloc).c_str(),
                MemoryTracker::FormatBytes(fFree).c_str());
        }
        if (ImGui::Button("Memory Panel", ImVec2(120, 0))) {
            m_MemoryPanel.Toggle();
        }
        ImGui::SameLine();
        if (ImGui::Button("Mem Test", ImVec2(80, 0))) {
            m_ConsolePanel.ExecuteCommand("memory_test burst");
            m_ConsolePanel.SetVisible(true);
        }

        // 压力测试开关
        ImGui::Text("  Stress: %s", m_EnableMemoryStress ? "^2ON^7" : "off");
        ImGui::SameLine(0, 10);
        if (ImGui::Button(m_EnableMemoryStress ? "Stop" : "Start", ImVec2(50, 0))) {
            m_ConsolePanel.ExecuteCommand(m_EnableMemoryStress ? "memory_stress off" : "memory_stress on");
        }

        // 精灵场景开关
        ImGui::Text("  Sprites: %zu %s", m_SpriteObjects.size(), m_SpriteSceneActive ? "^2ON^7" : "off");
        ImGui::SameLine(0, 10);
        if (ImGui::Button(m_SpriteSceneActive ? "Stop Spr" : "Start Spr", ImVec2(70, 0))) {
            m_ConsolePanel.ExecuteCommand(m_SpriteSceneActive ? "sprites off" : "sprites on");
        }

        ImGui::Separator();

        // ── CVar 状态 ──
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Console Variables:");
        ImGui::Text("  player_speed = %.2f  (set player_speed <val>)", m_PlayerSpeed.Get());
        ImGui::Text("  max_enemies  = %d    (set max_enemies <val>)", m_MaxEnemies.Get());
        ImGui::Text("  debug_draw   = %s  (set debug_draw 0|1)", m_EnableDebugDraw.Get() ? "true" : "false");

        ImGui::Separator();

        // ── 时间控制区域 ──
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.6f, 1.0f), "Time Control:");
        float currentScale = Time::GetTimeScale();
        float gameDt = Time::GetGameDeltaTime();
        float rawDt = Time::GetDeltaTime();

        ImGui::Text("  Timescale:    %.2f", currentScale);
        ImGui::Text("  Game dt:      %.4fs (raw: %.4fs)", gameDt, rawDt);

        // 速率指示器（带颜色）
        const char* speedLabel = "UNKNOWN";
        ImVec4 speedColor(1,1,1,1);
        if (currentScale <= 0.0f)      { speedLabel = "PAUSED";    speedColor = ImVec4(1,0.3f,0.3f,1); }
        else if (currentScale < 0.5f)  { speedLabel = "VERY SLOW"; speedColor = ImVec4(0.5f,0.8f,1,1); }
        else if (currentScale < 0.9f)  { speedLabel = "SLOW-MO";   speedColor = ImVec4(0.5f,1,0.8f,1); }
        else if (currentScale < 1.1f)  { speedLabel = "NORMAL";    speedColor = ImVec4(0.5f,1,0.5f,1); }
        else if (currentScale < 2.5f)  { speedLabel = "FAST";      speedColor = ImVec4(1,1,0.5f,1); }
        else                           { speedLabel = "LUDICROUS"; speedColor = ImVec4(1,0.3f,0.3f,1); }

        ImGui::SameLine(); ImGui::TextColored(speedColor, " [ %s ]", speedLabel);

        // 控制按钮
        if (ImGui::Button("|| Pause", ImVec2(70, 0))) {
            m_ConsolePanel.ExecuteCommand("pause");
        }
        ImGui::SameLine();
        if (ImGui::Button("> 1x", ImVec2(45, 0))) {
            m_ConsolePanel.ExecuteCommand("resume");
        }
        ImGui::SameLine();
        if (ImGui::Button("< 0.5x", ImVec2(55, 0))) {
            m_ConsolePanel.ExecuteCommand("slowmo");
        }
        ImGui::SameLine();
        if (ImGui::Button(">> 2x", ImVec2(50, 0))) {
            m_ConsolePanel.ExecuteCommand("timescale 2.0");
        }
        ImGui::SameLine();
        if (ImGui::Button(">>> 5x", ImVec2(55, 0))) {
            m_ConsolePanel.ExecuteCommand("timescale 5.0");
        }

        // 速率滑块
        float sliderScale = currentScale;
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderFloat("##SpeedSlider", &sliderScale, 0.0f, 5.0f, "Speed: %.1fx")) {
            std::string cmd = "timescale " + std::to_string(sliderScale);
            m_ConsolePanel.ExecuteCommand(cmd);
        }

        ImGui::Separator();

        // ── 崩溃测试按钮 ──
        ImGui::Text("Crash Test:");
        if (ImGui::Button("Trigger Nullptr Crash", ImVec2(200, 0))) {
            // 先更新崩溃上下文
            CrashContext::SetLevel("SystemTest_Level");
            CrashContext::SetPosition(100.0f, 200.0f, 300.0f);
            CrashContext::SetPlayerState("crash_test|running");
            CrashContext::SetScriptStack({"SystemTestApp.cpp", "imgui_demo.cpp"});
            CrashContext::SetCustom("frame_on_crash", std::to_string(m_FrameCount));
            CrashContext::Register("OnImGui_Provider", [this]() {
                return "Frame=" + std::to_string(m_FrameCount);
            });

            // 触发访问违例
            volatile int* p = nullptr;
            *p = 0xDEAD;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Click to trigger an intentional crash.\n"
                              "A crash report will be generated in the crashes/\n"
                              "directory, including a screenshot (.tga).");
        }
    }
    ImGui::End();

    // ── 渲染控制台面板 ──
    m_ConsolePanel.OnImGui();

    // ── 渲染内存监控面板 ──
    m_MemoryPanel.OnImGui();
}

// ============================================================
// OnRender — 可选测试绘图
// ============================================================

void SystemTestApp::OnRender() {
    if (m_SpriteSceneActive) {
        // 禁用默认四边形，渲染精灵场景
        m_Shader.reset();
        m_VAO.reset();
        m_Texture.reset();
        RenderSpriteScene();
    }
}

// ============================================================
// 工具
// ============================================================

void SystemTestApp::RecordTiming(PhaseTiming& t, float32 dtMs) {
    if (dtMs < t.min) t.min = dtMs;
    if (dtMs > t.max) t.max = dtMs;
    t.avg = (t.avg * static_cast<float32>(t.count) + dtMs)
          / static_cast<float32>(t.count + 1);
    t.count++;
}

} // namespace Engine
