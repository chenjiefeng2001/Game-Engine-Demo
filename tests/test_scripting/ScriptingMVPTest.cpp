/**
 * @file ScriptingMVPTest.cpp
 * @brief Scripting MVP 验收测试（S1-S5，见 docs/GPU-Physics-v3.0 · Scripting 主线）
 *
 *   S1 Runtime        : Lua 5.4 VM 启动 / 执行 / 返回值
 *   S2 API Boundary   : Engine.* 稳定边界（log / time_now / random / api_version）
 *   S3 Lifecycle      : OnCreate -> OnUpdate* -> OnFixedUpdate -> OnDestroy
 *   S4 Reload State   : _PERSIST 表跨 Reload 保留；瞬态全局重建
 *   S5 Isolation      : 语法错误 / 运行时错误 / 死循环预算 / 沙箱剥离
 */

#include <gtest/gtest.h>
#include "Engine/Scripting/LuaEngine.h"
#include "Engine/Scripting/ScriptInstance.h"
#include "Engine/Scripting/ScriptAPI.h"

#include <fstream>
#include <filesystem>
#include <chrono>
#include <cstdio>

using namespace Engine::Scripting;

namespace {
    const std::string kScratchDir = "script_mvp_scratch";

    void WriteScript(const std::string& name, const std::string& content) {
        std::error_code ec;
        std::filesystem::create_directories(kScratchDir, ec);
        const std::string path = kScratchDir + "/" + name;
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << content;
    }

    std::string ScriptPath(const std::string& name) { return kScratchDir + "/" + name; }
}

// ── 探针：ResetGlobalState 孤立验证 ─────────────────────────
TEST(ScriptingMVPTest, S4b_ResetGlobalState_CleanWipe) {
    LuaEngine eng;
    ASSERT_TRUE(eng.Init());
    ASSERT_TRUE(eng.RunString("x = 1"));
    std::printf("    [PROBE-R] calling reset...\n"); fflush(stdout);
    eng.ResetGlobalState();
    std::printf("    [PROBE-R] survived\n"); fflush(stdout);
    EXPECT_TRUE(eng.RunString("assert(x == nil)"));
}
TEST(ScriptingMVPTest, S1_Runtime_Basics) {
    LuaEngine eng;
    ASSERT_TRUE(eng.Init());
    EXPECT_TRUE(eng.RunString("function add(a) return a + 2 end"));
    EXPECT_EQ(eng.CallFunctionInt("add", 40), 42);
    eng.Shutdown();
}

// ── S2: API Boundary ────────────────────────────────────────
TEST(ScriptingMVPTest, S2_EngineAPIBoundary) {
    LuaEngine eng;
    ASSERT_TRUE(eng.Init());
    ScriptAPI::RegisterAll(eng);

    EXPECT_TRUE(eng.RunString(
        "assert(type(Engine) == 'table')\n"
        "assert(type(Engine.log.info) == 'function')\n"
        "assert(type(Engine.time.now) == 'function')\n"
        "assert(Engine.api_version >= 2.0)\n"));

    EXPECT_TRUE(eng.RunString(
        "local t = Engine.time.now()\n"
        "assert(type(t) == 'number' and t >= 0, 't=' .. tostring(t))\n"
        "local r = Engine.random(1.0, 2.0)\n"
        "assert(r >= 1.0 and r <= 2.0, 'r=' .. tostring(r))\n"))
        << eng.GetLastError().message;
    eng.Shutdown();
}

// ── S3: Lifecycle ───────────────────────────────────────────
TEST(ScriptingMVPTest, S3_Lifecycle_CallbackOrderAndCounts) {
    WriteScript("lifecycle.lua",
        "_PERSIST = _PERSIST or {}\n"
        "created  = 0\n"
        "updated  = 0\n"
        "fixed    = 0\n"
        "function OnCreate()          created = created + 1; _PERSIST.created_total = (_PERSIST.created_total or 0) + 1 end\n"
        "function OnUpdate(dt)        updated = updated + dt end\n"
        "function OnFixedUpdate(fdt)  fixed   = fixed + 1 end\n");

    ScriptInstance inst;
    ScriptInstance::Config cfg;
    cfg.instructionBudget = 5000000;
    ASSERT_TRUE(inst.Initialize(ScriptPath("lifecycle.lua"), cfg));

    inst.OnCreate();
    inst.OnUpdate(1.0f);
    inst.OnUpdate(2.0f);
    inst.OnFixedUpdate(0.016f);

    auto* eng = inst.GetEngine();
    EXPECT_EQ(eng->GetGlobalInt("created"), 1);
    // dt 累加（浮点求和容差）
    EXPECT_NEAR(eng->GetGlobalDouble("updated"), 3.0, 1e-5);
    EXPECT_EQ(eng->GetGlobalInt("fixed"), 1);

    inst.Shutdown();   // 触发 OnDestroy（脚本未定义 → 安全空操作）
}

// ── S4: Reload 持久状态 ─────────────────────────────────────
TEST(ScriptingMVPTest, S4_Reload_PreservesPersistState) {
    WriteScript("persist.lua",
        "_PERSIST = _PERSIST or {}\n"
        "_PERSIST.counter = (_PERSIST.counter or 0) + 1\n"
        "transient_runs = (transient_runs or 0) + 1\n");

    ScriptInstance inst;
    ASSERT_TRUE(inst.Initialize(ScriptPath("persist.lua")));

    auto* eng = inst.GetEngine();
    EXPECT_EQ(eng->GetGlobalInt("transient_runs"), 1);
    // 嵌套表字段断言须在 Lua 内完成（GetGlobal* 只读顶层全局）
    EXPECT_TRUE(eng->RunString("assert(_PERSIST.counter == 1, 'c=' .. tostring(_PERSIST.counter))"))
        << eng->GetLastError().message;

    ASSERT_TRUE(inst.Reload());
    // 持久状态：counter 继续累加（= 2）；瞬态全局：随新代码重建归位
    EXPECT_TRUE(eng->RunString(
        "assert(_PERSIST.counter == 2, 'c=' .. tostring(_PERSIST and _PERSIST.counter))\n"
        "assert(transient_runs == 1)\n"))
        << eng->GetLastError().message;
}

// ── S5a: 语法错误隔离 ───────────────────────────────────────
TEST(ScriptingMVPTest, S5a_SyntaxError_GracefulFailure) {
    WriteScript("broken_syntax.lua", "function broken( end end end");
    ScriptInstance inst;
    EXPECT_FALSE(inst.Initialize(ScriptPath("broken_syntax.lua")));
    EXPECT_FALSE(inst.IsValid());
}

// ── S5b: 运行时错误隔离 ─────────────────────────────────────
TEST(ScriptingMVPTest, S5b_RuntimeError_InstanceSurvives) {
    WriteScript("runtime_err.lua",
        "function OnUpdate(dt) error('boom from OnUpdate') end\n"
        "function ping() return 7 end\n");

    ScriptInstance inst;
    ScriptInstance::Config cfg;
    cfg.instructionBudget = 2000000;
    ASSERT_TRUE(inst.Initialize(ScriptPath("runtime_err.lua"), cfg));

    inst.OnUpdate(0.016f);                       // 错误被 pcall 捕获并记录
    EXPECT_NE(inst.GetEngine()->GetLastError().message.find("boom"),
              std::string::npos);

    // 引擎仍可用：后续调用正常
    EXPECT_TRUE(inst.GetEngine()->RunString("assert(true)"));
    EXPECT_EQ(inst.GetEngine()->CallFunctionInt("ping"), 7);
    EXPECT_TRUE(inst.IsValid());
}

// ── S5c: 死循环防护（指令预算）─────────────────────────────
TEST(ScriptingMVPTest, S5c_InfiniteLoop_AbortedByBudget) {
    WriteScript("infinite.lua",
        "budget_hits = 0\n"
        "function OnUpdate(dt) while true do budget_hits = budget_hits + 1 end end\n");

    ScriptInstance inst;
    ScriptInstance::Config cfg;
    cfg.instructionBudget = 200000;            // 极小预算 → 快速中止
    ASSERT_TRUE(inst.Initialize(ScriptPath("infinite.lua"), cfg));

    const auto t0 = std::chrono::steady_clock::now();
    inst.OnUpdate(0.016f);
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0).count();

    std::printf("    [S5c] abort took %.2f ms\n", ms);
    EXPECT_LT(ms, 2000.0) << "instruction budget failed to abort infinite loop";
    EXPECT_NE(inst.GetEngine()->GetLastError().message.find("budget"),
              std::string::npos);
    // 引擎存活
    EXPECT_TRUE(inst.GetEngine()->RunString("assert(true)"));
}

// ── S5d: 沙箱剥离危险全局 ───────────────────────────────────
TEST(ScriptingMVPTest, S5d_Sandbox_StripsDangerousGlobals) {
    LuaEngine eng;
    eng.EnableSandbox(true);
    ASSERT_TRUE(eng.Init());

    EXPECT_TRUE(eng.RunString(
        "assert(os ~= nil and os.execute == nil and os.exit == nil)\n"
        "assert(io == nil)\n"
        "assert(require == nil and loadfile == nil and dofile == nil)\n"
        "assert(type(os.time) == 'function')\n"));  // 安全子集保留
    eng.Shutdown();
}
