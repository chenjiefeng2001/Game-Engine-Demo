/**
 * @file GP01ContentScaleTest.cpp
 * @brief GP1-C 核心实验：动态生成规模 / 混合类型 / 波次推进 / 波间存档
 *
 * GP-006 实验场：Engine.entity.spawn 之后，"所有敌人"的账簿完全由
 * game.lua 自维护（enemies 表 + E_NNN 计数器）—— 本文件同时验证其
 * 正确性并度量其成本（STATS 遥测 + 账簿一致性断言）。
 */

#include "GP01Harness.h"

using namespace gp01;

// ════════════════════════════════════════════════════════════
// C2 动态生成：20+ 体全部有效、落点在 Pad 邻域、账簿/场景一致
// ════════════════════════════════════════════════════════════
TEST(GP01, ContentScale_DynamicSpawn) {
    Ctx c;
    StartRun(c);
    FreezeWaves(c);
    const int baseObjs = static_cast<int>(c.scene.GetObjectCount());

    const char* types[] = { "grunt", "tank", "scout", "grunt", "scout" };
    std::vector<std::string> names;
    for (int i = 0; i < 20; ++i) {
        std::string n = SpawnOne(c, types[i % 5]);
        ASSERT_FALSE(n.empty()) << "spawn #" << i << " failed";
        names.push_back(n);
    }

    // 遥测与账簿
    ASSERT_TRUE(c.inst.Execute("_g_sp = STATS.spawned"));
    EXPECT_EQ((int)c.Num("_g_sp"), 20);
    Snap s = c.Snapshot();
    EXPECT_EQ(s.AliveCount(), 20);

    // 场景对象数 = 布局 10 + 动态 20（生成体真实进入场景）
    EXPECT_EQ(static_cast<int>(c.scene.GetObjectCount()),
              baseObjs + 20);

    // 句柄有效性：20 个名字 find 全部命中
    std::string probe =
        "_g_all_found = 'yes' "
        "for i = 1, 20 do "
        "  if not Engine.entity.find(string.format('E_%03d', i)) then "
        "    _g_all_found = 'no' end end";
    ASSERT_TRUE(c.inst.Execute(probe));
    EXPECT_EQ(c.Str("_g_all_found"), "yes");

    // 落点邻域：每个生成体距某 Pad < 1.5（轮转 + 散布的契约）
    for (auto& kv : s.enemies) {
        float bd = 1e9f;
        for (const char* p : {"Pad_N", "Pad_S", "Pad_W", "Pad_E"}) {
            Vec3 pp = c.Pos(p);
            bd = std::min(bd, static_cast<float>(
                     std::hypot(kv.second.x - pp.x, kv.second.z - pp.z)));
        }
        ASSERT_LT(bd, 1.5f) << kv.first << " spawned off-pad";
    }

    // 全体有效运行 120 帧
    c.Run(120);
    EXPECT_EQ(c.Snapshot().AliveCount(), 20);
}

// ════════════════════════════════════════════════════════════
// C4.3 混合类型同场：四型行为分异 + 分值阶梯
// ════════════════════════════════════════════════════════════
TEST(GP01, ContentScale_MixedTypes) {
    Ctx c;
    StartRun(c);
    FreezeWaves(c);

    std::string g = SpawnOne(c, "grunt");
    std::string t = SpawnOne(c, "tank");
    std::string sc = SpawnOne(c, "scout");
    std::string bo = SpawnOne(c, "boss");
    ASSERT_FALSE(g.empty()); ASSERT_FALSE(t.empty());
    ASSERT_FALSE(sc.empty()); ASSERT_FALSE(bo.empty());

    Snap a = c.Snapshot();
    c.Run(150);                                          // 2.5s
    Snap b = c.Snapshot();
    auto disp = [&](const std::string& n) {
        return std::hypot(b.enemies[n].x - a.enemies[n].x,
                          b.enemies[n].z - a.enemies[n].z);
    };
    EXPECT_GT(disp(sc), disp(g));
    EXPECT_GT(disp(g), disp(bo));                        // boss 0.5×
    EXPECT_GT(disp(bo), disp(t));                        // tank 0.45× 最慢

    EXPECT_EQ(b.enemies[bo].hp, 16);                     // 未受击，满血
    EXPECT_EQ(b.enemies[g].hp, 3);
    EXPECT_EQ(b.enemies[t].hp, 8);
    EXPECT_EQ(b.enemies[sc].hp, 2);
}

// ════════════════════════════════════════════════════════════
// C4.2 / C1 波次推进（全战役）：五波逐波点亮、每波编成精确 →
//      全 32 体真实战斗清场 → victory。
//      C1 运行时证据：STATS.spawned=32（≥30）全部经 entity.spawn 实体化；
//      总数精确：destroyed=32、score=785 = 19×20 + 6×50 + 7×15。
// ════════════════════════════════════════════════════════════
TEST(GP01, ContentScale_WavesProgression) {
    Ctx c;
    StartRun(c);
    PristineSave(c, "waves");

    struct WaveExp { int g, t, s; };
    const WaveExp exp[5] = { { 5,0,0 },{ 3,0,2 },{ 5,3,0 },
                             { 0,2,4 },{ 6,1,1 } };   // ↔ game.lua WAVES

    int cumKilled = 0;
    for (int w = 1; w <= 5; ++w) {
        const int n = exp[w - 1].g + exp[w - 1].t + exp[w - 1].s;
        ASSERT_TRUE(WaitForWave(c, w, n)) << "wave " << w << " never lit";
        Snap s = c.Snapshot();
        EXPECT_EQ(s.wave, w);
        EXPECT_EQ(s.AliveCount(), n);            // 前波已清 —— 全场仅本波
        int g = 0, t = 0, sc = 0;
        for (auto& kv : s.enemies) {
            if (!kv.second.alive) continue;
            if (kv.second.type == "grunt") ++g;
            else if (kv.second.type == "tank") ++t;
            else if (kv.second.type == "scout") ++sc;
        }
        EXPECT_EQ(g, exp[w - 1].g) << "wave " << w << " grunt comp";
        EXPECT_EQ(t, exp[w - 1].t) << "wave " << w << " tank comp";
        EXPECT_EQ(sc, exp[w - 1].s) << "wave " << w << " scout comp";
        EXPECT_EQ(s.spawned, cumKilled + n);

        cumKilled += n;
        if (w < 5) KillUntilDestroyed(c, cumKilled);
    }

    c.input.Press("J");
    std::string endState = DriveBattle(c, 7200);
    ASSERT_EQ(endState, "victory") << "ended in " << endState;

    Snap fin = c.Snapshot();
    EXPECT_EQ(fin.spawned, 32);                  // C1：≥30 体全部实体化
    EXPECT_EQ(fin.destroyed, 32);               // 总数精确
    EXPECT_EQ(fin.score, 785);                  // 19×20 + 6×50 + 7×15
    EXPECT_GT(fin.hp, 0) << "campaign survived on skill alone";
}

// ════════════════════════════════════════════════════════════
// C4.4 波间存档：Spawn→Kill→Spawn→Kill→Save → 冷启全等 → 续波可用
// （含"波中增援"的非规则序列 —— 存档必须忠实记录任意中间态）
// ════════════════════════════════════════════════════════════
TEST(GP01, ContentScale_SaveMidWaves) {
    std::filesystem::remove_all(kScratch);
    Ctx c;
    StartRun(c);
    PristineSave(c, "midwaves");                         // 先存布局（无敌人态）

    ASSERT_TRUE(WaitForWave(c, 1, 5));                   // Spawn ×5
    KillUntilDestroyed(c, 2);                            // Kill ×2
    std::string extra = SpawnOne(c, "scout");            // 波中增援 Spawn
    ASSERT_FALSE(extra.empty());
    KillUntilDestroyed(c, 3);                            // Kill 增援
    Snap mid = c.Snapshot();
    ASSERT_EQ(mid.spawned, 6);
    ASSERT_EQ(mid.destroyed, 3);
    ASSERT_EQ(mid.wave, 1);

    const std::string blobA = c.Blob();                  // Save（状态半场）
    std::vector<std::string> deadNames;
    for (auto& kv : mid.enemies)
        if (!kv.second.alive) deadNames.push_back(kv.first);

    Ctx b;
    ColdStart(b, "midwaves", blobA);                     // Load（布局+blob）

    EXPECT_EQ(b.Blob(), blobA) << "state drifted across restart";

    // 死者仍死（名单来自存档前真实战局）；幸存者在原坐标重铸
    std::string probe = "_g_dead_ok = 'ok' ";
    for (auto& n : deadNames)
        probe += "if Engine.entity.find('" + n + "') then "
                 "_g_dead_ok = 'bad' end ";
    ASSERT_TRUE(b.inst.Execute(probe));
    EXPECT_EQ(b.Str("_g_dead_ok"), "ok");

    // 续战能力：剩余 wave1 清空后 victory（截断后续波以聚焦本契约）
    ASSERT_TRUE(b.inst.Execute("WAVES = {}"));
    b.input.Press("J");
    std::string endState = DriveBattle(b, 7200);
    ASSERT_EQ(endState, "victory") << "ended in " << endState;

    Snap fin = b.Snapshot();
    EXPECT_EQ(fin.score, 115);                           // 55(pre) + 60(post)
    EXPECT_EQ(fin.destroyed, 6);
}
