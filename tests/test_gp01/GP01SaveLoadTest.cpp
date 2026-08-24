/**
 * @file GP01SaveLoadTest.cpp
 * @brief GP1-C — 战斗状态穿越 Save→Restart→Load + 完整生产链 Golden
 *
 * 存档 = 静态布局场景文件 ＋ 游戏自有 blob（敌人账簿/波次/计数）。
 * 读档后幸存者按存档坐标重铸，亡者保持墓碑。
 */

#include "GP01Harness.h"

using namespace gp01;

namespace {

    // 用动态构造的条件探测（死者 find 全 nil）
    inline void c_inst_probe_dead(Ctx& b, const std::string& cond) {
        ASSERT_TRUE(b.inst.Execute("_g_dead_ok = (" + cond +
                                   ") and 'ok' or 'bad'"));
        EXPECT_EQ(b.Str("_g_dead_ok"), "ok");
    }

    // 打出"中间战局"：wave1(5G) 杀 2、软化 1、补给 hp=13（blob 输出参）
    inline void SetupMidFight(Ctx& c, const std::string& tag,
                              std::string& blobOut) {
        StartRun(c);
        PristineSave(c, tag);                            // 布局先行（无敌人态）
        EXPECT_TRUE(WaitForWave(c, 1, 5));
        KillUntilDestroyed(c, 2);
        c.Run(30);                                       // 攻击冷却归零

        Snap s = c.Snapshot();
        std::string target;
        for (auto& kv : s.enemies)
            if (kv.second.alive) { target = kv.first; break; }
        EXPECT_FALSE(target.empty());
        TeleportNear(c, target, 0.95f);
        c.input.Press("J");
        c.Run(1);
        c.input.Clear();

        // 剧本化补给（Console 注入 = 合法调试动作，VS01 先例）
        ASSERT_TRUE(c.inst.Execute("_PERSIST.hp = 13"));

        Snap s2 = c.Snapshot();
        EXPECT_EQ(s2.score, 40);
        EXPECT_EQ(s2.destroyed, 2);
        EXPECT_GT(s2.hp, 6);
        EXPECT_EQ(s2.enemies[target].hp, 2);
        blobOut = c.Blob();
    }
}

// ════════════════════════════════════════════════════════════
// 战斗中存档：内容契约（账簿/波次/计数/掉血 全部落盘）
// ════════════════════════════════════════════════════════════
TEST(GP01, SaveCombatState) {
    std::filesystem::remove_all(kScratch);
    Ctx c;
    std::string blob;
    SetupMidFight(c, "midsave", blob);

    Snap s = c.Snapshot();
    EXPECT_EQ(s.score, 40);
    EXPECT_EQ(s.wave, 1);
    EXPECT_EQ(s.spawned, 5);
    EXPECT_EQ(s.destroyed, 2);
    int alive = s.AliveCount();
    EXPECT_EQ(alive, 3);

    PristineSave(c, "midsave");                          // 幂等重写布局
    EXPECT_FALSE(c.Blob().empty());
}

// ════════════════════════════════════════════════════════════
// 冷启动恢复：blob 全等、死者仍死、幸存者原坐标、续战至 victory
// ════════════════════════════════════════════════════════════
TEST(GP01, RestartCombatState) {
    std::filesystem::remove_all(kScratch);
    Ctx a;
    std::string blobA;
    SetupMidFight(a, "restart", blobA);

    Ctx b;
    ColdStart(b, "restart", blobA);

    EXPECT_EQ(b.Blob(), blobA) << "state drifted across restart";

    // 死者仍死（已销毁，find 失效）—— 死者集合从存档账簿推导
    Snap pa = ParseSnap(blobA);
    {
        std::string cond;
        for (auto& kv : pa.enemies) {
            if (kv.second.alive) continue;
            if (!cond.empty()) cond += " and ";
            cond += "Engine.entity.find('" + kv.first + "') == nil";
        }
        c_inst_probe_dead(b, cond);
    }
    Snap rb = b.Snapshot();
    EXPECT_EQ(rb.AliveCount(), 3);

    // 幸存者恢复追击
    double moved = 0.0;
    {
        Snap x0 = b.Snapshot();
        for (int f = 0; f < 60; ++f) b.inst.OnUpdate(1.0f / 60.0f);
        Snap x1 = b.Snapshot();
        for (auto& kv : x1.enemies) {
            if (!kv.second.alive) continue;
            moved = std::max(moved, static_cast<double>(std::hypot(
                kv.second.x - x0.enemies[kv.first].x,
                kv.second.z - x0.enemies[kv.first].z)));
        }
    }
    EXPECT_GT(moved, 0.2) << "survivors frozen after load";

    // 截断后续波 → 清场 → victory；分数在恢复基础上精确累计
    ASSERT_TRUE(b.inst.Execute("WAVES = {}"));
    b.input.Press("J");
    std::string endState = DriveBattle(b, 7200);
    ASSERT_EQ(endState, "victory") << "ended in " << endState;

    Snap fin = b.Snapshot();
    EXPECT_EQ(fin.score, 100);                           // 40(pre) + 60(post)
    EXPECT_EQ(fin.destroyed, 5);
}

// ════════════════════════════════════════════════════════════
// B6/C10 Game Production Evidence Test：
// 冷启 → 瘦场景载入 → 宽限内移动 → wave1 战斗 → 中场存档 →
// 冷启全等 → 续战 → victory
// ════════════════════════════════════════════════════════════
TEST(GP01, CoreGameplay) {
    std::filesystem::remove_all(kScratch);

    Ctx c;
    StartRun(c);
    PristineSave(c, "golden");                           // 布局先行

    // 宽限期内移动（前 2s 无敌情）
    Vec3 p0 = c.Pos("Player");
    c.input.Press("W");
    c.Run(45);
    c.input.Clear();
    EXPECT_GT(std::hypot(c.Pos("Player").x - p0.x,
                         c.Pos("Player").z - p0.z), 2.5);

    // wave1 到达并击杀首只
    ASSERT_TRUE(WaitForWave(c, 1, 5));
    KillUntilDestroyed(c, 1);
    Snap mid = c.Snapshot();
    EXPECT_EQ(mid.score, 20);
    EXPECT_EQ(mid.destroyed, 1);

    const std::string blob = c.Blob();                   // Save（状态半场）

    Ctx b;
    ColdStart(b, "golden", blob);
    EXPECT_EQ(b.Blob(), blob);

    // 截断后续波 → 清场 → victory → 总分恒等 Σ已生成分值
    ASSERT_TRUE(b.inst.Execute("WAVES = {}"));
    b.input.Press("J");
    std::string endState = DriveBattle(b, 7200);
    ASSERT_EQ(endState, "victory") << "ended in " << endState;

    Snap fin = b.Snapshot();
    EXPECT_EQ(fin.score, 100);                           // 20 + 4×20
    EXPECT_GT(fin.hp, 0);

    std::printf("    [GP01] golden: cold->lean load->move->wave combat"
                "->save->restart->equal->continue->victory OK\n");
}
