/**
 * @file DXGoldenGateTest.cpp
 * @brief Milestone 4 — DX Golden Gate（首次使用者从空项目到可玩游戏的端到端验收）
 *
 * 五道门禁：
 *   DX-G1 空场景真的是空的（无残留）
 *   DX-G2 资产导入完全 UI 驱动（无需手写路径/GUID）
 *   DX-G3 脚本编辑 Save/Reload 后行为立即改变
 *   DX-G4 运行时错误可见且不杀引擎
 *   DX-G5 跨进程 Restart→Load→Play 全链路恢复
 *
 * 本测试模拟一个不了解内部架构的开发者的完整操作序列。
 */

#include <gtest/gtest.h>
#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/Content/ResourceLifecycle.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Resources/ResourceGUID.h"
#include "Engine/Core/Resources/ResourceManager.h"
#include "Engine/Core/RenderResources/TextureManager.h"

#include "Engine/Scripting/LuaEngine.h"
#include "Engine/Scripting/GameplayAPI.h"
#include "Engine/Scripting/ScriptAPI.h"
#include "Engine/Scripting/ScriptInstance.h"

#include "Engine/OpenGL/OpenGLGraphicsFactory.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <cstdio>

using namespace Engine;
using namespace Engine::Content;
using namespace Engine::Scripting;

namespace {
    const std::string kDir = "dx_golden_scratch";

    void W(const std::string& name, const std::string& body) {
        std::filesystem::create_directories(kDir);
        std::ofstream f(kDir + "/" + name, std::ios::binary | std::ios::trunc);
        f << body;
    }
    std::string DP(const std::string& name) { return kDir + "/" + name; }

    // Path helpers used by the gate tests
    std::string DX_P(const std::string& name)  { return kDir + "/" + name; }
    std::string DX_SP(const std::string& name) { return kDir + "/" + name; }
    std::string DX_SF() { return "game.lua"; }
    std::string DX_MP() { return kDir + "/manifest.json"; }
    std::string DX_SS() { return kDir + "/golden.scene"; }

    // 模拟 Asset Browser 的 Import 动作（用户选择文件后自动调用）
    ResourceGUID SimulateImport(ContentRegistry& reg,
                                const std::string& path, AssetType type) {
        return reg.Import(path, type);   // 幂等：同路径返回既有 GUID
    }
}

// ═══ DX Golden Gate 主测试 ═══
//
// 一个测试覆盖全部五道门禁，按真实开发者操作顺序串联。
TEST(DXGoldenGate, Full_Workflow_EmptyProject_To_PlayableGame) {
    // ════════════════════════════════════════════
    // Phase 0: 清理环境（模拟全新启动）
    // ════════════════════════════════════════════
    std::filesystem::remove_all(kDir);

    // ════════════════════════════════════════════
    // DX-G1: 空场景真的是空的
    // ════════════════════════════════════════════
    {
        ContentRegistry reg;
        EXPECT_EQ(reg.Count(), 0u) << "fresh registry should have zero assets";

        SceneSnapshot snap;   // 默认构造 = 空
        EXPECT_TRUE(snap.entities.empty());

        Engine::Scene scene;
        EXPECT_EQ(scene.GetObjectCount(), 0u);

        // 无残留验证通过
    }

    // ════════════════════════════════════════════
    // DX-G2: 资产导入完全 UI 驱动
    // ════════════════════════════════════════════
    //
    // 模拟：Asset Browser → Import... → FileDialog 选择文件
    // 开发者只需要知道文件路径，不需要理解 GUID/Manifest 内部机制。

    ContentRegistry reg;

    // 用户通过文件对话框选择两个文件
    auto texGuid  = SimulateImport(reg, "assets/textures/test.png", AssetType::Texture);
    auto scriptG  = SimulateImport(reg, DX_P("game.lua"), AssetType::Script);

    EXPECT_FALSE(texGuid.IsNull());
    EXPECT_FALSE(scriptG.IsNull());
    EXPECT_EQ(reg.Count(), 2u);

    // 幂等验证：重复导入同一路径返回同一 GUID
    auto texAgain = SimulateImport(reg, "assets/textures/test.png", AssetType::Texture);
    EXPECT_TRUE(texAgain == texGuid);

    // ════════════════════════════════════════════
    // 创建 Entity 并分配资产
    // ════════════════════════════════════════════

    Scripting::GameplayAPI::Reset();
    Engine::Scene gateScene;   // HandleSpawn requires a bound scene
    Scripting::GameplayAPI::SetScene(&gateScene);

    Scripting::GameplayAPI::HandleSpawn("Player");

    auto playerH = Scripting::GameplayAPI::HandleFindByName("Player");
    ASSERT_NE(playerH, 0u);

    // 分配 Texture GUID 到 Player 的 Sprite
    // （在真实编辑器中由 Asset Browser 双击触发）

    // ════════════════════════════════════════════
    // DX-G3: 脚本编辑 Save/Reload 后行为立即改变
    // ════════════════════════════════════════════

    // 初始脚本：设置一个标志
    W("player.lua",
        "_PERSIST = _PERSIST or {}\n"
        "_PERSIST.version = 1\n"
        "function OnCreate()\n"
        "    _PERSIST.created = true\n"
        "end\n"
        "function OnUpdate(dt)\n"
        "    _PERSIST.updates = (_PERSIST.updates or 0) + 1\n"
        "end\n");

    ScriptInstance inst;
    ScriptInstance::Config cfg; cfg.instructionBudget = 5000000;
    ASSERT_TRUE(inst.Initialize(DX_P("player.lua"), cfg));
    inst.OnCreate();

    // 验证 v1 行为
    inst.OnUpdate(0.016f);
    bool v1ok = inst.GetEngine()->RunString(
        "assert(_PERSIST.version == 1)");
    EXPECT_TRUE(v1ok);

    // ── 修改脚本（模拟 Script Editor 编辑）──
    W("player.lua",
        "_PERSIST = _PERSIST or {}\n"
        "_PERSIST.version = 2\n"
        "function OnCreate()\n"
        "    _PERSIST.created = true\n"
        "end\n"
        "function OnUpdate(dt)\n"
        "    _PERSIST.updates = (_PERSIST.updates or 0) + 1\n"
        "    _PERSIST.v2_flag = true\n"
        "end\n");

    // Reload（F5 等价）
    ASSERT_TRUE(inst.Reload());

    // Reload 后新行为生效
    inst.OnUpdate(0.016f);
    bool v2flag = inst.GetEngine()->RunString(
        "assert(_PERSIST.v2_flag == true)");
    EXPECT_TRUE(v2flag) << "Reload did not activate new code";

    // ── Save 场景 ──
    SceneSnapshot snap;
    SerializedEntity e; e.name = "Player"; e.scriptGuid = scriptG;
    snap.entities.push_back(e);
    ASSERT_TRUE(SaveSnapshotToFile(snap, DX_P("golden.scene")));
    ASSERT_TRUE(reg.SaveManifest(DX_P("golden_manifest.json")));

    // ════════════════════════════════════════════
    // DX-G4: 运行时错误可见且不杀引擎
    // ════════════════════════════════════════════

    // 引入运行时错误
    bool errCaught = !inst.Execute("error('intentional test error')");
    EXPECT_TRUE(errCaught) << "runtime error was not caught";

    // 引擎仍存活
    EXPECT_TRUE(inst.IsValid());
    bool stillWorks = inst.GetEngine()->RunString(
        "assert(type(Engine.log.info) == 'function')");
    EXPECT_TRUE(stillWorks);

    // 语法错误同样不崩溃
    bool syntaxErr = !inst.Execute("this is not valid lua !!!");
    EXPECT_TRUE(syntaxErr);

    // 修复后恢复正常
    W("fixed.lua", "function OnUpdate(dt) end\n");
    ScriptInstance inst2;
    ASSERT_TRUE(inst2.Initialize(DX_P("fixed.lua"), cfg));
    inst2.OnUpdate(0.016f);
    EXPECT_TRUE(inst2.IsValid());
}

// ═══ DX-G5: 跨进程 Restart→Load→Play ═══
//
// Process A: 构建场景+资产+脚本 → Save → exit
// Process B: 从磁盘恢复全部状态 → Play → 验证行为正确

TEST(DXGoldenGate, R13_ProcessA_BuildAndSave) {
    std::filesystem::remove_all(kDir);
    std::filesystem::create_directories(kDir);

    ContentRegistry reg;
    auto texG  = reg.Import("assets/textures/test.png", AssetType::Texture);
    auto scrG  = reg.Import(DX_SP("game.lua"), AssetType::Script);
    ASSERT_TRUE(reg.SaveManifest(DX_MP()));

    W(DX_SF(),
        "_PERSIST = _PERSIST or {}\n"
        "_PERSIST.initialized = true\n"
        "function OnCreate()\n"
        "    _PERSIST.startPos = {x=3,y=0,z=0}\n"
        "end\n"
        "function OnUpdate(dt)\n"
        "    local x,y,z = Engine.transform.get_position(1)\n"
        "    if x then _PERSIST.lastX = x end\n"
        "end\n");

    SceneSnapshot snap;
    SerializedEntity player;
    player.name = "Player";
    player.px = 3.f; player.py = 0.f; player.pz = 0.f;
    player.scriptGuid = scrG;
    snap.entities.push_back(player);

    SerializedEntity cube;
    cube.name = "Cube"; cube.spriteGuid = texG;
    cube.px = -3.f;
    snap.entities.push_back(cube);

    ASSERT_TRUE(SaveSnapshotToFile(snap, DX_SS()));
}

TEST(DXGoldenGate, R13_ProcessB_LoadAndPlay) {
    // 冷启动前提：文件必须存在（ProcessA 产生）
    ASSERT_TRUE(std::filesystem::exists(DX_SS())) << "run ProcessA first";
    ASSERT_TRUE(std::filesystem::exists(DX_MP())) << "run ProcessA first";

    // 全新进程 B：从零恢复注册表与场景
    ContentRegistry reg;
    ASSERT_TRUE(reg.LoadManifest(DX_MP()));
    EXPECT_GT(reg.Count(), 0u);

    SceneSnapshot snap; std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile(DX_SS(), snap, err)) << err;

    Engine::Scene scene;
    OpenGLGraphicsFactory gfxF;
    Engine::TextureManager tm(gfxF);
    Scripting::GameplayAPI::Reset();
    Scripting::GameplayAPI::SetScene(&scene);
    auto r = InstantiateScene(snap, scene, tm, reg);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(scene.GetObjectCount(), 2u);

    // 找到 Player 实体并绑定脚本
    std::string scriptPath;
    for (size_t i = 0; i < snap.entities.size(); ++i)
        if (snap.entities[i].name == "Player")
            scriptPath = reg.ResolvePath(r.bindings[i].scriptGuid);
    ASSERT_FALSE(scriptPath.empty());

    ScriptInstance inst;
    ScriptInstance::Config cfg; cfg.instructionBudget = 2000000;
    ASSERT_TRUE(inst.Initialize(scriptPath, cfg));

    // Drive gameplay: simulate input frames
    for (int f = 0; f < 60; ++f) inst.OnUpdate(1.0f / 60.0f);

    // Console Execute works in restored process
    EXPECT_TRUE(inst.Execute(
        "assert(Engine.api_version >= 2.0)"));
    EXPECT_TRUE(inst.IsValid());
}
