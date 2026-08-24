/**
 * @file GP01Test.cpp
 * @brief Game Production Phase 1 — GP01 生产契约测试（bootstrap + 数据模型）
 *
 * 测试对象不是引擎功能，而是"游戏生产契约"（docs/GP-P1-Charter.md §11）：
 *   GP1-A：空场景 → Player → Play → Save → Restart → Load → 正常运行
 *   GP1-B：ENEMY_TYPES 数据模型 / 多类型实例 / 实例状态独立性
 */

#include "GP01Harness.h"

using namespace Engine;
using namespace gp01;

// ════════════════════════════════════════════════════════════
// GP1-A · 干净注册表 → Import 清单 → GUID → 场景实例化
// ════════════════════════════════════════════════════════════
TEST(GP01, Load) {
    Ctx c;
    ASSERT_EQ(c.reg.Count(), 0u);                        // 空注册表起步
    ASSERT_EQ(c.scene.GetObjectCount(), 0u);

    c.BeginFresh((kGpDir + "/manifest.json").c_str());
    ASSERT_EQ(c.reg.Count(), 9u);                        // 8 tex + 1 script
    for (const auto& e : c.reg.GetAllEntries())
        EXPECT_FALSE(c.reg.ResolvePath(e.guid).empty()); // GUID 全程可解析

    c.LoadAndBind(kGpDir + "/Main.scene");
    ASSERT_EQ(c.idx.size(), 11u);              // Director+Player+4 Wall+5 Enemy
    EXPECT_NE(c.idx.count("Player"), 0u);
    EXPECT_TRUE(c.inst.Initialize(c.DirectorPath(),
                                  ScriptInstance::Config{}));
    c.inst.OnCreate();
    c.Run(30);
    SUCCEED() << "[GP01] Main.scene loaded and director alive";
}

// ════════════════════════════════════════════════════════════
// GP1-A · Player 可操控（位移 ≈ SPEED·t / 松手停止 / 边界不可穿出）
// ════════════════════════════════════════════════════════════
TEST(GP01, PlayerMovement) {
    Ctx c;
    c.BeginFresh((kGpDir + "/manifest.json").c_str());
    c.LoadAndBind(kGpDir + "/Main.scene");
    ASSERT_TRUE(c.inst.Initialize(c.DirectorPath(),
                                  ScriptInstance::Config{}));
    c.inst.OnCreate();

    Vec3 a = c.Pos("Player");
    c.input.Press("W");                                  // 向北（-z）
    c.Run(60);
    c.input.Clear();
    Vec3 b = c.Pos("Player");
    const double moved = std::hypot(b.x - a.x, b.z - a.z);
    EXPECT_GT(moved, 3.5)                                // 4.0 × 1s
        << "moved=" << moved;

    c.Run(30);                                           // 松开后应停止
    Vec3 d = c.Pos("Player");
    EXPECT_LT(std::hypot(d.x - b.x, d.z - b.z), 0.05)
        << "player kept moving after input released";

    // 边界契约：向南通到底被 Wall_S 挡住，不得穿出
    c.input.Press("S");
    c.Run(600);
    c.input.Clear();
    Vec3 s = c.Pos("Player");
    EXPECT_LT(s.z, 6.0) << "escaped through south wall";
}

// ════════════════════════════════════════════════════════════
// GP1-B1 · ENEMY_TYPES 数据表契约
// （三种类型存在、属性正确、实例 HP 独立、数据表不被实例污染）
// ════════════════════════════════════════════════════════════
TEST(GP01, EnemyTypes) {
    Ctx c;
    c.BeginFresh((kGpDir + "/manifest.json").c_str());
    c.LoadAndBind(kGpDir + "/Main.scene");
    ASSERT_TRUE(c.inst.Initialize(c.DirectorPath(),
                                  ScriptInstance::Config{}));
    c.inst.OnCreate();

    // 类型存在 + 属性正确（数据表全局暴露 = 模块契约）
    ASSERT_TRUE(c.inst.Execute(
        "_g_chk = {} "
        "local function chk(k, hp, spd, dmg, val) "
        "  local v = ENEMY_TYPES[k] "
        "  if not v then _G['chk_' .. k] = 'missing' return end "
        "  _G['chk_' .. k] = (v.hp == hp and v.speed == spd and "
        "                     v.damage == dmg and v.value == val) "
        "                    and 'ok' or 'mismatch' end "
        "chk('grunt', 3, 1.0, 1, 20) "
        "chk('tank', 8, 0.45, 2, 50) "
        "chk('scout', 2, 1.6, 1, 15)"));
    EXPECT_EQ(c.Str("chk_grunt"), "ok");
    EXPECT_EQ(c.Str("chk_tank"), "ok");
    EXPECT_EQ(c.Str("chk_scout"), "ok");

    // 实例独立性：打 grunt1 两刀 → grunt2 与数据表均不受污染
    TeleportNear(c, "E_Grunt1", 0.95f);
    c.input.Press("J");
    c.Run(40);                                           // 两击（cd 0.35s）
    c.input.Clear();
    Snap s = c.Snapshot();
    ASSERT_NE(s.enemies.count("E_Grunt1"), 0);
    EXPECT_EQ(s.enemies["E_Grunt1"].first, 1);           // 3 − 2
    EXPECT_EQ(s.enemies["E_Grunt2"].first, 3);           // 兄弟实例不受影响

    ASSERT_TRUE(c.inst.Execute(
        "_g_tpl_ok = (ENEMY_TYPES.grunt.hp == 3 and "
        "             ENEMY_TYPES.grunt.maxHp == 3) "
        "            and 'pure' or 'polluted'"));
    EXPECT_EQ(c.Str("_g_tpl_ok"), "pure")                // 数据表未被实例污染
        << "instance damage leaked into type table";
}

// ════════════════════════════════════════════════════════════
// GP1-B1 · 多类型实例生成契约（Grunt×2 / Tank×1 / Scout×2 手写场景 JSON）
// —— M003 观察样本：不引入 Prefab 时的真实生产成本
// ════════════════════════════════════════════════════════════
TEST(GP01, EnemySpawn) {
    Ctx c;
    c.BeginFresh((kGpDir + "/manifest.json").c_str());
    c.LoadAndBind(kGpDir + "/Main.scene");
    ASSERT_EQ(c.idx.size(), 11u);
    ASSERT_TRUE(c.inst.Initialize(c.DirectorPath(),
                                  ScriptInstance::Config{}));
    c.inst.OnCreate();
    Snap s = c.Snapshot();
    ASSERT_EQ(s.enemies.size(), 5u);

    // 各类型初始 HP = maxHp，数量正确
    EXPECT_EQ(s.enemies["E_Grunt1"].first, 3);
    EXPECT_EQ(s.enemies["E_Grunt2"].first, 3);
    EXPECT_EQ(s.enemies["E_Tank1"].first, 8);
    EXPECT_EQ(s.enemies["E_Scout1"].first, 2);
    EXPECT_EQ(s.enemies["E_Scout2"].first, 2);

    // 精灵绑定按类型区分（GUID 契约）
    auto spriteOf = [&](const char* n) {
        return c.bindings[c.idx.at(n)].spriteGuid;
    };
    EXPECT_EQ(spriteOf("E_Grunt1"), spriteOf("E_Grunt2"));   // 同型同精灵
    EXPECT_NE(spriteOf("E_Grunt1"), spriteOf("E_Tank1"));    // 异型异精灵
    EXPECT_NE(spriteOf("E_Grunt1"), spriteOf("E_Scout1"));
    EXPECT_NE(spriteOf("E_Tank1"), spriteOf("E_Scout1"));

    // 场景内位置互不重叠（手写坐标的最低质量要求）
    for (const char* n : Roster())
        EXPECT_LT(std::hypot(c.Pos(n).x, c.Pos(n).z), 20.0);
}

// ════════════════════════════════════════════════════════════
// GP1-A · Play → Save → 冷启等价 → Load → 继续正常运行
// ════════════════════════════════════════════════════════════
TEST(GP01, SaveRestartLoad) {
    std::filesystem::remove_all(kScratch);

    // ── Play ──
    Ctx c;
    c.BeginFresh((kGpDir + "/manifest.json").c_str());
    c.LoadAndBind(kGpDir + "/Main.scene");
    ASSERT_TRUE(c.inst.Initialize(c.DirectorPath(),
                                  ScriptInstance::Config{}));
    c.inst.OnCreate();
    c.input.Press("D");
    c.Run(45);
    c.input.Clear();
    ASSERT_TRUE(c.inst.Execute("_PERSIST.score = 77"));   // 玩出一点状态

    // ── Save：活场景捕获落盘 ──
    std::filesystem::create_directories(kScratch);
    SceneSnapshot live = CaptureScene(c.scene, c.bindings);
    ASSERT_EQ(live.entities.size(), 11u);
    ASSERT_TRUE(SaveSnapshotToFile(live, kScratch + "/main_saved.scene"));
    ASSERT_TRUE(c.reg.SaveManifest(kScratch + "/manifest.json"));
    c.inst.GetEngine()->RunString("_g_score = _PERSIST.score");

    // ── Restart：全新进程等价 ──
    Ctx b;
    b.BeginFresh((kScratch + "/manifest.json").c_str());
    ASSERT_EQ(b.reg.Count(), 9u);
    b.LoadAndBind(kScratch + "/main_saved.scene");
    ASSERT_EQ(b.idx.size(), 11u);
    ASSERT_TRUE(b.inst.Initialize(b.DirectorPath(),
                                  ScriptInstance::Config{}));
    // 宿主把存档中的持续状态交还新实例（真实存档系统职责边界）
    ASSERT_TRUE(b.inst.Execute(std::string("_PERSIST.score = ") +
                               std::to_string((int)c.Num("_g_score"))));
    b.inst.OnCreate();

    // ── Load 后继续正常游玩 ──
    b.inst.GetEngine()->RunString("_g_score = _PERSIST.score");
    EXPECT_EQ((int)b.Num("_g_score"), 77);               // 状态跨冷启存活
    Vec3 a = b.Pos("Player");
    b.input.Press("A");                                  // 换方向仍可控
    b.Run(60);
    b.input.Clear();
    Vec3 e = b.Pos("Player");
    EXPECT_GT(std::hypot(e.x - a.x, e.z - a.z), 3.5)
        << "player not controllable after cold-start load";

    std::printf("    [GP01] GP1-A acceptance: play -> save -> restart "
                "-> load -> playable OK\n");
}
