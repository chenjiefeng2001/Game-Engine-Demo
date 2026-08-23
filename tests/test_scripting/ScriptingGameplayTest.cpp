/**
 * @file ScriptingGameplayTest.cpp
 * @brief Gameplay Vertical Slice 验收（API Contract v2）
 *
 * 链路验证：Input(mock) → Lua(Engine.input.*) → handle →
 *           ScriptSceneBridge → GameObject/TransformComponent
 *
 * 覆盖：
 *   G1 分域注册与 v2 契约
 *   G2 输入注入（mock 后端反射脚本查询）
 *   G3 实体生命周期（spawn/destroy/句柄失效）
 *   G4 Transform 读写与平移累积
 *   G5 全链路：Lua Player 脚本读输入驱动实体位移
 *   G6 Reload × Gameplay：_PERSIST 中的句柄跨 Reload 仍可操作
 *   G7 Console 能力：Execute() 直驱引擎 + 错误捕获
 */

#include <gtest/gtest.h>
#include "Engine/Scripting/LuaEngine.h"
#include "Engine/Scripting/ScriptInstance.h"
#include "Engine/Scripting/ScriptAPI.h"
#include "Engine/Scripting/GameplayAPI.h"
#include "Engine/Core/Scene/Scene.h"

#include <set>
#include <string>
#include <fstream>

using namespace Engine::Scripting;

namespace {

    class MockInput final : public GameplayAPI::IScriptInputProvider {
    public:
        std::set<std::string> down;
        bool IsKeyDown(const char* key) override {
            return down.count(key) > 0;
        }
    };

    const std::string kScratch = "script_gameplay_scratch";
    void WriteScript(const std::string& name, const std::string& body) {
        std::filesystem::create_directories(kScratch);
        std::ofstream f(kScratch + "/" + name, std::ios::binary | std::ios::trunc);
        f << body;
    }

} // namespace

/// 标准 gtest fixture —— 保证每个测试获得干净的 gameplay 上下文
class ScriptingGameplayTest : public ::testing::Test {
protected:
    Engine::Scene scene;
    MockInput input;
    LuaEngine eng;

    void SetUp() override {
        GameplayAPI::SetScene(&scene);
        GameplayAPI::SetInputProvider(&input);
        ASSERT_TRUE(eng.Init());
        ScriptAPI::RegisterAll(eng);          // 基础域（log/time/random）
        GameplayAPI::RegisterDomains(eng);    // gameplay 三域
    }
    void TearDown() override {
        eng.Shutdown();
        GameplayAPI::Reset();                 // 清空句柄表与注入点（防跨测试泄漏）
    }
};

// ── G1: v2 分域契约 ─────────────────────────────────────────
TEST_F(ScriptingGameplayTest, G1_DomainNamespaces_V2Contract) {
    EXPECT_TRUE(eng.RunString(
        "assert(type(Engine.input.is_down) == 'function')\n"
        "assert(type(Engine.entity.spawn) == 'function')\n"
        "assert(type(Engine.transform.translate) == 'function')\n"
        "assert(Engine.api_version >= 2.0)\n"))
        << eng.GetLastError().message;
}

// ── G2: 输入注入 ────────────────────────────────────────────
TEST_F(ScriptingGameplayTest, G2_Input_MockReflection) {
    EXPECT_TRUE(eng.RunString("assert(Engine.input.is_down('W') == false)"));
    input.down.insert("W");
    EXPECT_TRUE(eng.RunString("assert(Engine.input.is_down('W') == true)\n"
                              "assert(Engine.input.is_down('S') == false)"));
    input.down.erase("W");
    EXPECT_TRUE(eng.RunString("assert(Engine.input.is_down('W') == false)"));
}

// ── G3: 实体生命周期 ────────────────────────────────────────
TEST_F(ScriptingGameplayTest, G3_Entity_LifecycleAndHandleInvalidation) {
    EXPECT_TRUE(eng.RunString(
        "h = Engine.entity.spawn('cube')\n"
        "assert(h and h > 0)\n"
        "assert(Engine.transform.set_position(h, 1.0, 2.0, 3.0))\n"
        "local x, y, z = Engine.transform.get_position(h)\n"
        "assert(x == 1.0 and y == 2.0 and z == 3.0)\n"
        "assert(Engine.entity.destroy(h))\n"
        "assert(Engine.transform.set_position(h, 9, 9, 9) == false)\n"))
        << eng.GetLastError().message;

    EXPECT_EQ(GameplayAPI::HandleCount(), 0u);
    EXPECT_EQ(scene.GetObjectCount(), 0u);
}

// ── G4: 平移累积 ────────────────────────────────────────────
TEST_F(ScriptingGameplayTest, G4_Transform_TranslateAccumulates) {
    EXPECT_TRUE(eng.RunString(
        "h = Engine.entity.spawn('mover')\n"
        "assert(Engine.transform.set_position(h, 0, 10, 0))\n"
        "assert(Engine.transform.translate(h, 1, -1, 2))\n"
        "assert(Engine.transform.translate(h, 1, -1, 2))\n"
        "local x, y, z = Engine.transform.get_position(h)\n"
        "assert(x == 2.0 and y == 8.0 and z == 4.0)\n"))
        << eng.GetLastError().message;
}

// ── G5: 全链路 —— Lua Player 脚本由输入驱动 ────────────────
TEST_F(ScriptingGameplayTest, G5_VerticalSlice_PlayerDrivenByInput) {
    WriteScript("player.lua",
        "_PERSIST = _PERSIST or {}\n"
        "_PERSIST.h = _PERSIST.h or Engine.entity.spawn('player')\n"
        "local SPEED = 2.0\n"
        "function OnUpdate(dt)\n"
        "    if Engine.input.is_down('W') then\n"
        "        Engine.transform.translate(_PERSIST.h, 0, 0, -SPEED * dt)\n"
        "    end\n"
        "end\n");

    ScriptInstance inst;
    ScriptInstance::Config cfg; cfg.instructionBudget = 2000000;
    ASSERT_TRUE(inst.Initialize(kScratch + "/player.lua", cfg));

    float x = 0, y = 0, z = 0;
    ASSERT_TRUE(GameplayAPI::HandleGetPosition(1, x, y, z));   // 首个句柄 = 1

    // 无输入 → 不动
    inst.OnUpdate(0.5f);
    ASSERT_TRUE(GameplayAPI::HandleGetPosition(1, x, y, z));
    EXPECT_FLOAT_EQ(z, 0.0f);

    // 按下 W → 沿 -z 移动 SPEED*dt
    input.down.insert("W");
    inst.OnUpdate(0.5f);
    inst.OnUpdate(0.25f);
    ASSERT_TRUE(GameplayAPI::HandleGetPosition(1, x, y, z));
    EXPECT_FLOAT_EQ(z, -1.5f);

    // 松开 → 停止
    input.down.erase("W");
    inst.OnUpdate(0.5f);
    ASSERT_TRUE(GameplayAPI::HandleGetPosition(1, x, y, z));
    EXPECT_FLOAT_EQ(z, -1.5f);
}

// ── G6: Reload × Gameplay —— 句柄存于 _PERSIST 跨 Reload 有效 ──
TEST_F(ScriptingGameplayTest, G6_Reload_HandleSurvivesViaPersist) {
    WriteScript("player_reload.lua",
        "_PERSIST = _PERSIST or {}\n"
        "_PERSIST.h = _PERSIST.h or Engine.entity.spawn('player')\n"
        "function OnUpdate(dt)\n"
        "    if Engine.input.is_down('W') then\n"
        "        Engine.transform.translate(_PERSIST.h, 0, 0, -1.0 * dt)\n"
        "    end\n"
        "end\n");

    ScriptInstance inst;
    ScriptInstance::Config cfg; cfg.instructionBudget = 2000000;
    ASSERT_TRUE(inst.Initialize(kScratch + "/player_reload.lua", cfg));

    ASSERT_TRUE(inst.Reload());     // 新代码生效、实体句柄保留

    // 正确引用：实例内部 VM（而非 fixture 的独立 eng）
    auto* veng = inst.GetEngine();
    std::printf("    [DBG] hasOnUpdate=%d\n", veng->HasFunction("OnUpdate") ? 1 : 0);
    fflush(stdout);

    input.down.insert("W");
    veng->RunString("print('[PRE] h=' .. tostring(_PERSIST.h))");
    inst.OnUpdate(1.0f);
    if (!veng->RunString(
            "local x,y,z = Engine.transform.get_position(_PERSIST.h)\n"
            "assert(z == -1.0, 'z=' .. tostring(z))\n"))
        std::printf("    [ERR] %s\n", veng->GetLastError().message.c_str());
    float x = 0, y = 0, z = 0;
    ASSERT_TRUE(GameplayAPI::HandleGetPosition(1, x, y, z));
    EXPECT_FLOAT_EQ(z, -1.0f);
}

// ── G7: Console 能力（Execute + 错误捕获）───────────────────
TEST_F(ScriptingGameplayTest, G7_Console_ExecuteDirectDriveAndErrorCapture) {
    WriteScript("console_target.lua",
        "h = Engine.entity.spawn('probe')\n"
        "function OnUpdate(dt) end\n");

    ScriptInstance inst;
    ScriptInstance::Config cfg; cfg.instructionBudget = 1000000;
    ASSERT_TRUE(inst.Initialize(kScratch + "/console_target.lua", cfg));

    // 直接执行语句驱动引擎（等价于 Console 输入）
    EXPECT_TRUE(inst.Execute("Engine.transform.set_position(h, 5, 6, 7)"));
    float x = 0, y = 0, z = 0;
    ASSERT_TRUE(GameplayAPI::HandleGetPosition(1, x, y, z));
    EXPECT_FLOAT_EQ(x, 5.0f);
    EXPECT_FLOAT_EQ(y, 6.0f);
    EXPECT_FLOAT_EQ(z, 7.0f);

    // 错误被捕获且实例存活（Console 显示错误，不崩溃）
    EXPECT_FALSE(inst.Execute("this_call_does_not_exist()"));
    EXPECT_NE(inst.GetEngine()->GetLastError().message.find("nil"),
              std::string::npos);
    EXPECT_TRUE(inst.IsValid());
}

// ── G7: Console 能力（Execute + 错误捕获）───────────────────
TEST_F(ScriptingGameplayTest, G8_SandboxRuntimeSmoke_RealAssetChain) {
    // 真实资产：assets/scripts/sandbox_player.lua（与 Sandbox Demo 同一份文件）
    ScriptInstance inst;
    ScriptInstance::Config cfg; cfg.instructionBudget = 5000000;
    ASSERT_TRUE(inst.Initialize("assets/scripts/sandbox_player.lua", cfg));
    inst.OnCreate();                                   // OnCreate 设初始位 (2,0,0)

    float x = 0, y = 0, z = 0;
    ASSERT_TRUE(GameplayAPI::HandleGetPosition(1, x, y, z));
    EXPECT_FLOAT_EQ(x, 2.0f);                          // OnCreate 初始位生效

    // W 按下 → S 键速度 dt=0.5 → z += 3.0
    input.down.insert("S");
    inst.OnUpdate(0.5f);
    ASSERT_TRUE(GameplayAPI::HandleGetPosition(1, x, y, z));
    EXPECT_FLOAT_EQ(z, 3.0f);

    // Reload（真实热重载路径）：同句柄继续有效并继续响应输入
    ASSERT_TRUE(inst.Reload());
    inst.OnUpdate(0.25f);
    ASSERT_TRUE(GameplayAPI::HandleGetPosition(1, x, y, z));
    EXPECT_FLOAT_EQ(z, 4.5f);

    // Console 能力收尾查询：Execute 内断言状态正确
    EXPECT_TRUE(inst.Execute(
        "assert(_PERSIST.h == 1)\n"
        "local x,y,z = Engine.transform.get_position(_PERSIST.h)\n"
        "assert(z == 4.5)\n"))
        << inst.GetEngine()->GetLastError().message;
}
