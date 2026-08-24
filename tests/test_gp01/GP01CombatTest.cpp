/**
 * @file GP01CombatTest.cpp
 * @brief GP1-B 核心战斗契约：AI 差异 / 伤害 / 死亡 / Score / 胜负状态机
 *
 * 全部经由冻结 API 驱动（输入注入 + Console 合法注入），
 * 不引入 Query/Collision Event/Component —— 摩擦进 GP01-Ledger。
 */

#include "GP01Harness.h"

using namespace gp01;

namespace {
    // 开一局新战斗
    void NewFight(Ctx& c) {
        c.BeginFresh((kGpDir + "/manifest.json").c_str());
        c.LoadAndBind(kGpDir + "/Main.scene");
        ASSERT_TRUE(c.inst.Initialize(c.DirectorPath(),
                                      ScriptInstance::Config{}));
        c.inst.OnCreate();
    }
}

// ════════════════════════════════════════════════════════════
// GP1-B2 · 追逐 AI：行为差异仅来自数据表（scout > grunt > tank）
// ════════════════════════════════════════════════════════════
TEST(GP01, EnemyMovement) {
    Ctx c;
    NewFight(c);

    Vec3 g1 = c.Pos("E_Grunt1"), t = c.Pos("E_Tank1"),
         s1 = c.Pos("E_Scout1");
    c.Run(120);                                          // 2s，无玩家输入

    auto disp = [](Vec3 a, Vec3 b) {
        return std::hypot(b.x - a.x, b.z - a.z);
    };
    const double dGrunt = disp(g1, c.Pos("E_Grunt1"));   // 期望 ≈2.2×2=4.4
    const double dTank  = disp(t,  c.Pos("E_Tank1"));    // ≈0.99×2=1.98
    const double dScout = disp(s1, c.Pos("E_Scout1"));   // ≈3.52×2=7.04

    EXPECT_NEAR(dGrunt, 4.4, 0.30) << "grunt chase speed wrong";
    EXPECT_NEAR(dTank, 1.98, 0.25) << "tank chase speed wrong";
    EXPECT_NEAR(dScout, 7.04, 0.35) << "scout chase speed wrong";
    EXPECT_GT(dScout, dGrunt);
    EXPECT_GT(dGrunt, dTank);

    // 敌人朝玩家逼近（直线追击的方向性）
    Vec3 p = c.Pos("Player"), g2 = c.Pos("E_Grunt2");
    EXPECT_LT(std::hypot(g2.x - p.x, g2.z - p.z),
              std::hypot(4.0 - p.x, -6.0 - p.z) - 3.0)
        << "grunt not approaching player";
}

// ════════════════════════════════════════════════════════════
// GP1-B3 · 攻击：范围门控 + 冷却 + is_down 连击（M004 观察位）
// ════════════════════════════════════════════════════════════
TEST(GP01, CombatDamage) {
    Ctx c;
    NewFight(c);

    // 时序纪律：scout 抵达需 ~3s，本测试全部几何操作在 ~1.1s 内完成，
    // 目标选择不会被逼近中的其他敌人抢占。
    // （1）范围外（tank 慢速且远）：按键不造成伤害
    TeleportNear(c, "E_Tank1", 2.5f);
    c.input.Press("J");
    c.Run(20);
    c.input.Clear();
    EXPECT_EQ(c.Snapshot().enemies["E_Tank1"].first, 8)
        << "hit landed out of range";

    // （2）范围内单击：恰好一刀（is_down 单击语义 —— M004 证据）
    TeleportNear(c, "E_Grunt1", 0.95f);                  // ∈ (接触0.85, 攻击1.10)
    c.input.Press("J");
    c.Run(1);
    c.input.Clear();
    EXPECT_EQ(c.Snapshot().enemies["E_Grunt1"].first, 2)
        << "single tap != one swing";

    // （3）无输入不补刀
    c.Run(6);
    EXPECT_EQ(c.Snapshot().enemies["E_Grunt1"].first, 2);

    // （4）按住连击：36 帧内按 cd 0.35s 出两刀
    c.input.Press("J");
    c.Run(36);
    c.input.Clear();
    Snap s = c.Snapshot();
    EXPECT_EQ(s.enemies["E_Grunt1"].first, 1) << "hold combo cadence wrong";
}

// ════════════════════════════════════════════════════════════
// GP1-B3/B4 · 死亡与得分：alive=false、尸体停止行动、score += value
// ════════════════════════════════════════════════════════════
TEST(GP01, EnemyDeathAndScore) {
    Ctx c;
    NewFight(c);

    // 两只 scout 会主动扑向玩家：按住 J 待其进入范围逐个击杀
    bool s1 = false, s2 = false;
    c.input.Press("J");
    for (int f = 0; f < 900 && !(s1 && s2); ++f) {
        c.inst.OnUpdate(1.0f / 60.0f);
        ASSERT_TRUE(c.inst.IsValid());
        Snap s = c.Snapshot();
        s1 = s1 || !s.enemies["E_Scout1"].second;
        s2 = s2 || !s.enemies["E_Scout2"].second;
    }
    c.input.Clear();
    ASSERT_TRUE(s1 && s2) << "scouts not cleared";

    Snap s = c.Snapshot();
    EXPECT_FALSE(s.enemies["E_Scout1"].second);
    EXPECT_EQ(s.enemies["E_Scout1"].first, 0);
    EXPECT_EQ(s.score, 30);                              // 15 × 2
    EXPECT_EQ(s.state, "playing");                       // 清场未完成

    // 尸体契约：位置冻结、不再行动
    Vec3 corpse = c.Pos("E_Scout1");
    c.Run(90);
    Vec3 after = c.Pos("E_Scout1");
    EXPECT_LT(std::hypot(after.x - corpse.x, after.z - corpse.z), 0.01)
        << "corpse moved";
}

// ════════════════════════════════════════════════════════════
// GP1-B4 · 失败路径：hp ≤ 0 → lost（Console 注入濒死属合法调试动作）
// ════════════════════════════════════════════════════════════
TEST(GP01, GameOver) {
    Ctx c;
    NewFight(c);

    ASSERT_TRUE(c.inst.Execute("_PERSIST.hp = 1"));
    TeleportNear(c, "E_Tank1", 1.02f);                   // tank 接触半径 1.05 内

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
    EXPECT_EQ(s.score, 0);                               // 未击杀任何敌人
    EXPECT_TRUE(s.enemies["E_Tank1"].second);
}

// ════════════════════════════════════════════════════════════
// GP1-B4 · 胜利路径：清场 → victory；总得分 = Σ type.value = 120
// ════════════════════════════════════════════════════════════
TEST(GP01, Victory) {
    Ctx c;
    NewFight(c);

    auto nav = CombatNav(c);
    c.input.Press("J");                                  // 全程按住攻击
    bool won = false;
    for (int f = 0; f < 5400 && !won; ++f) {             // ≤90s 游戏时间
        nav(f);
        c.inst.OnUpdate(1.0f / 60.0f);
        ASSERT_TRUE(c.inst.IsValid()) << "frame " << f;
        if (c.Snapshot().state == "victory") won = true;
    }
    c.input.Clear();
    ASSERT_TRUE(won) << "victory unreachable; state=" << c.Snapshot().state;

    Snap s = c.Snapshot();
    EXPECT_EQ(s.score, 120);                             // 20×2+50+15×2
    EXPECT_GT(s.hp, 0);
    for (auto& kv : s.enemies)
        EXPECT_FALSE(kv.second.second) << kv.first << " survived";
}
