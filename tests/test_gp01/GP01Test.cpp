/**
 * @file GP01Test.cpp
 * @brief GP01 生产契约测试（bootstrap + 数据模型 + 波次编成）
 *
 * GP1-C 版本：场景瘦身为 Player/Walls/Pads/Director（10 实体），
 * 敌人全部运行时生成；注册表扩至 33 资产。
 */

#include "GP01Harness.h"

using namespace gp01;

// ════════════════════════════════════════════════════════════
// 生产契约：33 资产 100% 解析；瘦场景实例化；导演存活
// ════════════════════════════════════════════════════════════
TEST(GP01, Load) {
    Ctx c;
    ASSERT_EQ(c.reg.Count(), 0u);
    ASSERT_EQ(c.scene.GetObjectCount(), 0u);

    c.BeginFresh((kGpDir + "/manifest.json").c_str());
    ASSERT_EQ(c.reg.Count(), 33u);                       // 32 tex + 1 script
    int textures = 0, scripts = 0;
    for (const auto& e : c.reg.GetAllEntries()) {
        EXPECT_FALSE(c.reg.ResolvePath(e.guid).empty());
        if (e.type == AssetType::Texture) ++textures;
        if (e.type == AssetType::Script)  ++scripts;
    }
    EXPECT_EQ(textures, 32);
    EXPECT_EQ(scripts, 1);

    c.LoadAndBind(kGpDir + "/Main.scene");
    ASSERT_EQ(c.idx.size(), 10u);              // Director+Player+4 Pad+4 Wall
    for (const char* p : {"Pad_N", "Pad_S", "Pad_W", "Pad_E"})
        EXPECT_NE(c.idx.count(p), 0u) << p << " missing";
    EXPECT_TRUE(c.inst.Initialize(c.DirectorPath(),
                                  ScriptInstance::Config{}));
    c.inst.OnCreate();
    c.Run(30);
    SUCCEED() << "[GP01] lean scene + 33-asset registry OK";
}

// ════════════════════════════════════════════════════════════
// Player 可操控（宽限期内静场：位移/停止/边界）
// ════════════════════════════════════════════════════════════
TEST(GP01, PlayerMovement) {
    Ctx c;
    StartRun(c);
    FreezeWaves(c);

    Vec3 a = c.Pos("Player");
    c.input.Press("W");
    c.Run(60);
    c.input.Clear();
    Vec3 b = c.Pos("Player");
    EXPECT_GT(std::hypot(b.x - a.x, b.z - a.z), 3.5);

    c.Run(30);
    Vec3 d = c.Pos("Player");
    EXPECT_LT(std::hypot(d.x - b.x, d.z - b.z), 0.05);

    c.input.Press("S");
    c.Run(600);
    c.input.Clear();
    EXPECT_LT(c.Pos("Player").z, 6.0) << "escaped through south wall";
}

// ════════════════════════════════════════════════════════════
// ENEMY_TYPES 数据表契约（四型含 boss；实例独立；模板不被污染）
// ════════════════════════════════════════════════════════════
TEST(GP01, EnemyTypes) {
    Ctx c;
    StartRun(c);
    FreezeWaves(c);

    ASSERT_TRUE(c.inst.Execute(
        "local function chk(k, hp, spd, dmg, val) "
        "  local v = ENEMY_TYPES[k] "
        "  if not v then _G['chk_' .. k] = 'missing' return end "
        "  _G['chk_' .. k] = (v.hp == hp and v.speed == spd and "
        "                     v.damage == dmg and v.value == val) "
        "                    and 'ok' or 'mismatch' end "
        "chk('grunt', 3, 1.0, 1, 20) "
        "chk('tank', 8, 0.45, 2, 50) "
        "chk('scout', 2, 1.6, 1, 15) "
        "chk('boss', 16, 0.5, 3, 100)"));
    EXPECT_EQ(c.Str("chk_grunt"), "ok");
    EXPECT_EQ(c.Str("chk_tank"), "ok");
    EXPECT_EQ(c.Str("chk_scout"), "ok");
    EXPECT_EQ(c.Str("chk_boss"), "ok");

    // 实例独立性：生成两只 grunt，打其一 → 另一只与模板均不受污染
    std::string a = SpawnOne(c, "grunt");
    std::string b = SpawnOne(c, "grunt");
    ASSERT_FALSE(a.empty());
    ASSERT_FALSE(b.empty());
    TeleportNear(c, a, 0.95f);
    c.input.Press("J");
    c.Run(40);
    c.input.Clear();

    Snap s = c.Snapshot();
    ASSERT_NE(s.enemies.count(a), 0u);
    ASSERT_NE(s.enemies.count(b), 0u);
    EXPECT_EQ(s.enemies[a].hp, 1);
    EXPECT_EQ(s.enemies[b].hp, 3);

    ASSERT_TRUE(c.inst.Execute(
        "_g_tpl_ok = (ENEMY_TYPES.grunt.hp == 3 and "
        "             ENEMY_TYPES.grunt.maxHp == 3) "
        "            and 'pure' or 'polluted'"));
    EXPECT_EQ(c.Str("_g_tpl_ok"), "pure")
        << "instance damage leaked into type table";
}

// ════════════════════════════════════════════════════════════
// 波次编成数据契约：五波 32 体 = 19 grunt / 6 tank / 7 scout（GP1-C 规格书）
// ════════════════════════════════════════════════════════════
TEST(GP01, WaveTableSpec) {
    Ctx c;
    StartRun(c);

    ASSERT_TRUE(c.inst.Execute(
        "_g_tally_grunt = 0 _g_tally_tank = 0 _g_tally_scout = 0 "
        "_g_waves_n = #WAVES "
        "for _, w in ipairs(WAVES) do "
        "  for _, tn in ipairs(w) do "
        "    if tn == 'grunt' then _g_tally_grunt = _g_tally_grunt + 1 end "
        "    if tn == 'tank'  then _g_tally_tank  = _g_tally_tank + 1 end "
        "    if tn == 'scout' then _g_tally_scout = _g_tally_scout + 1 end "
        "  end end"));
    EXPECT_EQ((int)c.Num("_g_waves_n"), 5);
    EXPECT_EQ((int)c.Num("_g_tally_grunt"), 19);
    EXPECT_EQ((int)c.Num("_g_tally_tank"), 6);
    EXPECT_EQ((int)c.Num("_g_tally_scout"), 7);
    EXPECT_EQ((int)c.Num("_g_tally_grunt") + (int)c.Num("_g_tally_tank") +
              (int)c.Num("_g_tally_scout"), 32);
}

// ════════════════════════════════════════════════════════════
// Play → 布局落盘 → 冷启 → blob 回灌 → 继续正常运行
// ════════════════════════════════════════════════════════════
TEST(GP01, SaveRestartLoad) {
    std::filesystem::remove_all(kScratch);
    Ctx c;
    StartRun(c);
    FreezeWaves(c);

    c.input.Press("D");
    c.Run(45);
    c.input.Clear();
    ASSERT_TRUE(c.inst.Execute("_PERSIST.score = 77"));

    PristineSave(c, "sr");
    const std::string blob = c.Blob();

    Ctx b;
    ColdStart(b, "sr", blob);
    ASSERT_EQ(b.reg.Count(), 33u);
    b.inst.GetEngine()->RunString("_g_score = _PERSIST.score");
    EXPECT_EQ((int)b.Num("_g_score"), 77);

    Vec3 a = b.Pos("Player");
    b.input.Press("A");
    b.Run(60);
    b.input.Clear();
    EXPECT_GT(std::hypot(b.Pos("Player").x - a.x,
                         b.Pos("Player").z - a.z), 3.5)
        << "player not controllable after cold-start load";

    std::printf("    [GP01] layout-scene + state-blob cold start OK\n");
}
