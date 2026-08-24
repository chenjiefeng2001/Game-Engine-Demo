/**
 * @file Dogfood08Test.cpp
 * @brief DF08 Long-session DX — 连续开发周期压力测试
 *
 * Golden Scenario（headless 模拟编辑器操作序列）：
 *   Clean Start → Import → Create Entities → Assign → Play
 *   → [Edit Lua + Reload] → Play → [Mutate Scene mid-session]
 *   → Capture/Save → Restart(冷启动) → Load → Verify → Edit again → Play → Final Round-trip
 *
 * 四类探针：
 *   状态可靠性   R1-R3 / X1-X4
 *   数据丢失风险 S1-S2（活场景编辑是否被 Save 捕获）
 *   工作流摩擦   Console 诊断质量、Reload 新表语义（独立 TEST）
 */

#include <gtest/gtest.h>
#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Resources/ResourceGUID.h"
#include "Engine/Core/RenderResources/TextureManager.h"

#include "Engine/Scripting/LuaEngine.h"
#include "Engine/Scripting/GameplayAPI.h"
#include "Engine/Scripting/ScriptAPI.h"
#include "Engine/Scripting/ScriptInstance.h"
#include "Engine/OpenGL/OpenGLGraphicsFactory.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace Engine;
using namespace Engine::Content;
using namespace Engine::Scripting;

namespace {
    const std::string kDir = "df08_scratch";

    void W8(const std::string& name, const std::string& body) {
        std::filesystem::create_directories(kDir);
        std::ofstream f(kDir + "/" + name, std::ios::binary | std::ios::trunc);
        f << body;
    }
    std::string P8(const std::string& n) { return kDir + "/" + n; }

    const char* LUA_V1 =
        "_PERSIST = _PERSIST or {}\n"
        "_PERSIST.v = 1\n"
        "function OnCreate() end\n"
        "function OnUpdate(dt)\n"
        "    local x,y,z = Engine.transform.get_position(1)\n"   // handle 1 = Player
        "    if x then Engine.transform.set_position(1, x + 1.0*dt, y, z) end\n"
        "end\n";

    const char* LUA_V2 =
        "_PERSIST = _PERSIST or {}\n"          // branch-1: keep old table
        "_PERSIST.v = 2\n"
        "_PERSIST.edited_v2 = true\n"
        "function OnCreate() end\n"
        "function OnUpdate(dt)\n"
        "    local x,y,z = Engine.transform.get_position(1)\n"
        "    if x then Engine.transform.set_position(1, x + 3.0*dt, y, z) end\n"
        "end\n";

    const char* LUA_V3 =
        "_PERSIST = _PERSIST or {}\n"
        "_PERSIST.session2 = true\n"
        "function OnCreate() end\n"
        "function OnUpdate(dt) end\n";
}

// ════════════════════════════════════════════════════════════
// 主序列：完整开发周期
// ════════════════════════════════════════════════════════════
TEST(Dogfood08, LongSession_EditorWorkflow) {
    std::filesystem::remove_all(kDir);

    // ── Phase A: Clean Start（空场景真的空）──
    ContentRegistry reg;
    ASSERT_EQ(reg.Count(), 0u);
    Engine::Scene scene;
    OpenGLGraphicsFactory gfxF;
    TextureManager tm(gfxF);
    GameplayAPI::Reset();
    GameplayAPI::SetScene(&scene);
    ASSERT_EQ(scene.GetObjectCount(), 0u);

    // ── Phase B: Import + Create + Assign ──
    auto texA = reg.Import(P8("tex_a.png"), AssetType::Texture);
    auto texB = reg.Import(P8("tex_b.png"), AssetType::Texture);
    W8("player.lua", LUA_V1);
    auto scrG = reg.Import(P8("player.lua"), AssetType::Script);
    ASSERT_FALSE(scrG.IsNull());

    ASSERT_EQ(GameplayAPI::HandleSpawn("Player"), 1u);   // handle 1 = Player
    ASSERT_EQ(GameplayAPI::HandleSpawn("Gem_1"),  2u);
    ASSERT_EQ(GameplayAPI::HandleSpawn("Wall_1"), 3u);
    ASSERT_EQ(scene.GetObjectCount(), 3u);

    // Asset Browser 分配等价物：binding 表按场景顺序键控
    // （Player←player.lua 即 Inspector/双击分配动作；跳过此步=未分配）
    std::vector<EntityContentBinding> bindings;
    bindings.push_back({ResourceGUID::Null, scrG});
    bindings.push_back({texB, ResourceGUID::Null});
    bindings.push_back({texA, ResourceGUID::Null});

    // ── Phase C: Play session 1（v1：1 u/s）──
    ScriptInstance inst;
    ScriptInstance::Config cfg; cfg.instructionBudget = 2000000;
    ASSERT_TRUE(inst.Initialize(P8("player.lua"), cfg));
    inst.OnCreate();
    for (int f = 0; f < 60; ++f) {
        inst.OnUpdate(1.0f / 60.0f);
        ASSERT_TRUE(inst.IsValid());
    }
    const float pxAfterV1 = scene.GetObjects()[0]->GetTransform().GetPosition().x;
    EXPECT_NEAR(pxAfterV1, 1.0f, 0.05f) << "v1 speed contract broken";

    // ── Phase D: Edit cycle 1 —— 仅改脚本，Reload ──
    const size_t  cntPre  = scene.GetObjectCount();
    const Vec3    wallPre = scene.GetObjects()[2]->GetTransform().GetPosition();

    W8("player.lua", LUA_V2);
    ASSERT_TRUE(inst.Reload()) << "Reload failed";

    // R1: Reload 不增删场景对象
    EXPECT_EQ(scene.GetObjectCount(), cntPre);
    // R2: 脚本未触碰的实体位置不被 Reload 破坏
    const Vec3 wallPost = scene.GetObjects()[2]->GetTransform().GetPosition();
    EXPECT_FLOAT_EQ(wallPost.x, wallPre.x);
    EXPECT_FLOAT_EQ(wallPost.z, wallPre.z);
    // R3: _PERSIST 经 Reload 保留（S4 branch-1）
    EXPECT_TRUE(inst.GetEngine()->RunString(
        "assert(_PERSIST ~= nil)\n"
        "assert(_PERSIST.v == 2)\n"           // 新代码写入的新值
        "assert(_PERSIST.edited_v2 == true)\n"));

    // 继续 Play：v2 生效（3 u/s），总位移 = 1.0 + 3.0
    for (int f = 0; f < 60; ++f) {
        inst.OnUpdate(1.0f / 60.0f);
        ASSERT_TRUE(inst.IsValid());
    }
    const float pxAfterV2 = scene.GetObjects()[0]->GetTransform().GetPosition().x;
    EXPECT_NEAR(pxAfterV2, 4.0f, 0.05f) << "v2 behavior did not activate";

    // ── Phase E: 会话中途修改 Scene（运行位移 + 编辑器加对象）──
    ASSERT_EQ(GameplayAPI::HandleSpawn("Gem_2"), 4u);    // mid-session add
    bindings.push_back({});                              // 尚未分配 sprite

    SceneSnapshot snapLive = CaptureScene(scene, bindings);

    // S1: 活场景新增实体被捕获
    ASSERT_EQ(snapLive.entities.size(), 4u);
    EXPECT_EQ(snapLive.entities[3].name, "Gem_2");
    // S2【核心数据丢失探针】：Save 必须捕获 LIVE 位置而非 authored 位置
    EXPECT_NEAR(snapLive.entities[0].px, pxAfterV2, 0.001f)
        << "DATA LOSS RISK: save wrote stale/authored position instead of live edit";

    ASSERT_TRUE(SaveSnapshotToFile(snapLive, P8("session.scene")));
    ASSERT_TRUE(reg.SaveManifest(P8("manifest.json")));

    // ── Phase F: 关闭 Editor → 冷启动重启 → Load ──
    inst.Shutdown();
    GameplayAPI::Reset();                     // 进程级状态清零
    ContentRegistry reg2;                     // 全新注册表（跨进程）
    Engine::Scene scene2;
    GameplayAPI::SetScene(&scene2);

    ASSERT_TRUE(reg2.LoadManifest(P8("manifest.json")));
    SceneSnapshot snapIn; std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile(P8("session.scene"), snapIn, err)) << err;

    auto r2 = InstantiateScene(snapIn, scene2, tm, reg2);
    ASSERT_TRUE(r2.ok);

    uint32_t eh = 1;
    for (auto& o : scene2.GetObjects()) { ASSERT_EQ(GameplayAPI::HandleAdopt(o), eh); ++eh; }

    // X1: 实体数在重启后存活
    EXPECT_EQ(scene2.GetObjectCount(), 4u);
    // X2: 所有名字可解析
    for (const char* n : {"Player", "Gem_1", "Wall_1", "Gem_2"})
        EXPECT_NE(GameplayAPI::HandleFindByName(n), 0u) << "name lost across restart: " << n;
    // X3: 运行期位移精确穿越重启边界
    const float pxRestart = scene2.GetObjects()[0]->GetTransform().GetPosition().x;
    EXPECT_NEAR(pxRestart, pxAfterV2, 0.001f) << "live position lost across restart";
    // X4: 脚本绑定穿越重启
    std::string scriptPath2;
    for (size_t i = 0; i < snapIn.entities.size(); ++i)
        if (snapIn.entities[i].name == "Player")
            scriptPath2 = reg2.ResolvePath(r2.bindings[i].scriptGuid);
    EXPECT_EQ(scriptPath2, P8("player.lua"));
    // 注：Gem_2 的 sprite GUID 在保存时为 Null——mid-session 新增实体的
    // 内容绑定依赖编辑器维护的 binding 表（见 Ledger DL-02）。

    // ── Phase G: 第二会话——再改脚本 + Play + Final Round-trip ──
    // 运行时状态契约：_PERSIST 属进程内状态，重启后重置（非数据丢失）
    W8("player.lua", LUA_V3);
    ScriptInstance inst3;
    ASSERT_TRUE(inst3.Initialize(P8("player.lua"), cfg));
    inst3.OnCreate();
    for (int f = 0; f < 30; ++f) { inst3.OnUpdate(1.0f / 60.0f); }
    // G1: v3 行为激活
    EXPECT_TRUE(inst3.GetEngine()->RunString(
        "assert(_PERSIST.session2 == true)\n"
        "assert(_PERSIST.v == nil)\n"));      // 旧会话字段不残留

    // Final: 二次 Capture/Save/Load 幂等
    SceneSnapshot snapFinal = CaptureScene(scene2, r2.bindings);
    ASSERT_EQ(snapFinal.entities.size(), 4u);
    ASSERT_TRUE(SaveSnapshotToFile(snapFinal, P8("final.scene")));
    SceneSnapshot snapBack;
    ASSERT_TRUE(LoadSnapshotFromFile(P8("final.scene"), snapBack, err)) << err;
    ASSERT_EQ(snapBack.entities.size(), 4u);
}

// ════════════════════════════════════════════════════════════
// 工作流摩擦探针：Console 能否解释脚本失败原因
// ════════════════════════════════════════════════════════════
TEST(Dogfood08, ConsoleDiagnostics_ErrorExplainsFailure) {
    std::filesystem::create_directories(kDir);
    W8("diag.lua",
        "_PERSIST = _PERSIST or {}\n"
        "function OnCreate() end\n"
        "function OnUpdate(dt) end\n");
    ContentRegistry reg;
    Engine::Scene scene;
    GameplayAPI::Reset();
    GameplayAPI::SetScene(&scene);

    ScriptInstance inst;
    ScriptInstance::Config cfg; cfg.instructionBudget = 1000000;
    ASSERT_TRUE(inst.Initialize(P8("diag.lua"), cfg));

    // 运行期错误必须被捕获且引擎存活
    EXPECT_FALSE(inst.Execute("error('df08_intentional_marker')"));
    EXPECT_TRUE(inst.IsValid());

    // 诊断质量：错误消息必须包含原始标记（Console 可解释失败原因）
    LuaError e = inst.GetEngine()->GetLastError();
    EXPECT_NE(e.message.find("df08_intentional_marker"), std::string::npos)
        << "console cannot explain failure; message='" << e.message << "'";

    // 语法错误同样有诊断
    EXPECT_FALSE(inst.Execute("this is not valid lua !!!"));
    e = inst.GetEngine()->GetLastError();
    EXPECT_FALSE(e.message.empty());
}

// ════════════════════════════════════════════════════════════
// 状态可靠性探针：Reload S4 branch-2（新代码显式定义新表 → 旧表让位）
// ════════════════════════════════════════════════════════════
TEST(Dogfood08, ReloadProtocol_NewTableYieldsOld) {
    std::filesystem::create_directories(kDir);
    Engine::Scene scene;
    GameplayAPI::Reset();
    GameplayAPI::SetScene(&scene);

    W8("proto.lua",
        "_PERSIST = { gen = 1 }\n"
        "function OnCreate() end\n"
        "function OnUpdate(dt) end\n");

    ScriptInstance inst;
    ScriptInstance::Config cfg; cfg.instructionBudget = 500000;
    ASSERT_TRUE(inst.Initialize(P8("proto.lua"), cfg));
    EXPECT_TRUE(inst.GetEngine()->RunString("assert(_PERSIST.gen == 1)"));

    // 显式新表 → 文档化语义：旧表让位（不做深合并）
    W8("proto.lua",
        "_PERSIST = { gen = 2 }\n"
        "function OnCreate() end\n"
        "function OnUpdate(dt) end\n");
    ASSERT_TRUE(inst.Reload());
    EXPECT_TRUE(inst.GetEngine()->RunString(
        "assert(_PERSIST.gen == 2)\n"
        "assert(_PERSIST.old_carryover == nil)\n"))
        << "S4 branch-2 semantics violated";
}
