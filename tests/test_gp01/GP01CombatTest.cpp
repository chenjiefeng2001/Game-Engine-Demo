/**
 * @file GP01CombatTest.cpp
 * @brief GP1-C 战斗契约：动态生成体上的 AI 差异/伤害/死亡/胜负
 *
 * 敌人不再静态摆场 —— 几何类测试用 SpawnAt 定点生成（波次冻结），
 * 流程类测试走真实波次推进。
 */

#include "GP01Harness.h"

using namespace gp01;

// ════════════════════════════════════════════════════════════
// 追逐 AI：行为差异仅来自数据表 scout(3.52) > grunt(2.2) > tank(0.99)
// ════════════════════════════════════════════════════════════
TEST(GP01, EnemyMovement) {
    Ctx c;
    StartRun(c);
    FreezeWaves(c);

    std::string g = SpawnOne(c, "grunt");
    std::string t = SpawnOne(c, "tank");
    std::string s = SpawnOne(c, "scout");
    ASSERT_FALSE(g.empty()); ASSERT_FALSE(t.empty()); ASSERT_FALSE(s.empty());

    Snap a = c.Snapshot();
    c.Run(120);                                          // 2s，无玩家输入

    Snap b = c.Snapshot();
    auto disp = [&](const std::string& n) {
        return std::hypot(b.enemies[n].x - a.enemies[n].x,
                          b.enemies[n].z - a.enemies[n].z);
    };
    const double dGrunt = disp(g), dTank = disp(t), dScout = disp(s);

    EXPECT_NEAR(dGrunt, 4.40, 0.30) << "grunt chase speed wrong";
    EXPECT_NEAR(dTank, 1.98, 0.25) << "tank chase speed wrong";
    EXPECT_NEAR(dScout, 7.04, 0.35) << "scout chase speed wrong";
    EXPECT_GT(dScout, dGrunt);
    EXPECT_GT(dGrunt, dTank);
}

// ════════════════════════════════════════════════════════════
// 攻击：范围门控 + 单击一刀 + 无输入不补刀 + 按住连击（M004 观察位）
// ════════════════════════════════════════════════════════════
TEST(GP01, CombatDamage) {
    Ctx c;
    StartRun(c);
    FreezeWaves(c);

    std::string g = SpawnOne(c, "grunt");
    ASSERT_FALSE(g.empty());

    TeleportNear(c, g, 2.5f);                            // 范围外
    c.input.Press("J");
    c.Run(20);
    c.input.Clear();
    EXPECT_EQ(c.Snapshot().enemies[g].hp, 3) << "hit landed out of range";

    TeleportNear(c, g, 0.95f);                           // ∈ (接触0.85, 攻击1.10)
    c.input.Press("J");
    c.Run(1);
    c.input.Clear();
    EXPECT_EQ(c.Snapshot().enemies[g].hp, 2) << "single tap != one swing";

    c.Run(6);
    EXPECT_EQ(c.Snapshot().enemies[g].hp, 2) << "swing without input";

    // 按住连击：cd 0.35s 节拍续刀，直到斩杀（3 刀全中）
    c.input.Press("J");
    bool dead = false;
    for (int f = 0; f < 180 && !dead; ++f) {
        c.inst.OnUpdate(1.0f / 60.0f);
        ASSERT_TRUE(c.inst.IsValid());
        dead = !c.Snapshot().enemies[g].alive;
    }
    c.input.Clear();
    ASSERT_TRUE(dead) << "hold combo failed to kill";

    Snap s = c.Snapshot();
    EXPECT_EQ(s.enemies[g].hp, 0);
    EXPECT_FALSE(s.enemies[g].alive);
    EXPECT_EQ(s.destroyed, 1);
    EXPECT_EQ(s.score, 20);
}

// ════════════════════════════════════════════════════════════
// 死亡与得分（动态体）：destroy → 场景对象消失 + find 失效 + 墓碑账簿
// ════════════════════════════════════════════════════════════
TEST(GP01, EnemyDeathAndScore) {
    Ctx c;
    StartRun(c);                                         // 真实波次：wave1 = 5 grunt
    ASSERT_TRUE(WaitForWave(c, 1, 5));
    const int baseObjs = static_cast<int>(c.scene.GetObjectCount());

    // 击杀顺序按空间最近而非生成序 —— 死者集合从快照差集推导
    Snap before = c.Snapshot();
    KillUntilDestroyed(c, 2);
    Snap s = c.Snapshot();

    std::vector<std::string> dead;
    for (auto& kv : before.enemies)
        if (kv.second.alive && !s.enemies[kv.first].alive)
            dead.push_back(kv.first);
    ASSERT_EQ(dead.size(), 2u);
    EXPECT_EQ(s.destroyed, 2);
    EXPECT_EQ(s.score, 40);                              // 20 × 2

    // 尸体契约 v3：实体已销毁 —— find 返回 nil、场景对象数下降
    std::string probe = "_g_dead_gone = ";
    for (size_t i = 0; i < dead.size(); ++i) {
        probe += (i ? " and " : "");
        probe += "Engine.entity.find('" + dead[i] + "') == nil";
    }
    probe += " and 'gone' or 'stale'";
    ASSERT_TRUE(c.inst.Execute(probe));
    EXPECT_EQ(c.Str("_g_dead_gone"), "gone");
    EXPECT_EQ(static_cast<int>(c.scene.GetObjectCount()), baseObjs - 2);

    // 账簿墓碑：快照仍记录其生死状态（存档连续性依据）
    for (auto& dname : dead)
        EXPECT_FALSE(s.enemies[dname].alive);
    EXPECT_EQ(s.state, "playing");
}

// ════════════════════════════════════════════════════════════
// 失败路径：hp ≤ 0 → lost（Console 注入濒死属合法调试动作）
// ════════════════════════════════════════════════════════════
TEST(GP01, GameOver) {
    Ctx c;
    StartRun(c);
    FreezeWaves(c);

    std::string t = SpawnOne(c, "tank");
    ASSERT_FALSE(t.empty());
    ASSERT_TRUE(c.inst.Execute("_PERSIST.hp = 1"));
    Vec3 p = c.Pos("Player");
    TeleportEntity(c, t, p.x, p.z + 0.5f);               // 贴脸（接触 <1.05）

    bool lost = false;
    for (int f = 0; f < 600 && !lost; ++f) {
        c.inst.OnUpdate(1.0f / 60.0f);
        ASSERT_TRUE(c.inst.IsValid());
        if (c.Snapshot().state == "lost") lost = true;
    }
    c.input.Clear();
    ASSERT_TRUE(lost) << "game over unreachable; state="
                      << c.Snapshot().state;
    Snap s = c.Snapshot();
    EXPECT_EQ(s.score, 0);
    EXPECT_TRUE(s.enemies[t].alive);
}

// ════════════════════════════════════════════════════════════
// 胜利路径（迷你波次配置 —— 全规模清场由 WavesProgression 覆盖）：
// 三波 {1G},{1S},{1T} → victory；score = 20+15+50 = 85
// ════════════════════════════════════════════════════════════
TEST(GP01, Victory) {
    Ctx c;
    StartRun(c);
    ASSERT_TRUE(c.inst.Execute(
        "WAVES = { {'grunt'}, {'scout'}, {'tank'} }"));

    Snap fin = c.Snapshot();                             // 占位避免未用警告
    (void)fin;
    c.input.Press("J");
    std::string endState = DriveBattle(c, 5400);
    ASSERT_EQ(endState, "victory") << "ended in " << endState;

    Snap s = c.Snapshot();
    EXPECT_EQ(s.score, 85);
    EXPECT_GT(s.hp, 0);
    EXPECT_EQ(s.spawned, 3);
    EXPECT_EQ(s.destroyed, 3);
}
