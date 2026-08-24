/**
 * @file GP01SaveLoadTest.cpp
 * @brief GP1-B5/B6 — 战斗状态穿越 Save→Kill→Restart→Load 链路 + 完整生产链 Golden
 *
 * 验证对象：SceneSerializerV1(位置/实体) ＋ _PERSIST 宿主中介(游戏状态，
 * 编码格式归 game.lua 所有) 能否完整恢复真实 gameplay state。
 */

#include "GP01Harness.h"

using namespace gp01;

namespace {

    void NewFight(Ctx& c) {
        c.BeginFresh((kGpDir + "/manifest.json").c_str());
        c.LoadAndBind(kGpDir + "/Main.scene");
        ASSERT_TRUE(c.inst.Initialize(c.DirectorPath(),
                                      ScriptInstance::Config{}));
        c.inst.OnCreate();
    }

    // 打出"中间战局"：双 scout 已死、grunt1 掉 1 血、score=30
    // （确定性脚本：scout 主动扑向静止玩家）
    void SetupMidFight(Ctx& c) {
        NewFight(c);
        bool s1 = false, s2 = false;
        c.input.Press("J");
        for (int f = 0; f < 900 && !(s1 && s2); ++f) {
            c.inst.OnUpdate(1.0f / 60.0f);
            EXPECT_TRUE(c.inst.IsValid());
            Snap s = c.Snapshot();
            s1 = s1 || !s.enemies["E_Scout1"].second;
            s2 = s2 || !s.enemies["E_Scout2"].second;
        }
        c.input.Clear();
        EXPECT_TRUE(s1 && s2);
        c.Run(30);                                       // 攻击冷却归零
        // 剧本化补给：站桩杀敌必被咬（真实战斗代价）；为后续"继续打到
        // 胜利"留出确定性余量。Console 注入 = 合法调试动作（VS01 先例）
        EXPECT_TRUE(c.inst.Execute("_PERSIST.hp = 13"));
        TeleportNear(c, "E_Grunt1", 0.95f);
        c.input.Press("J");
        c.Run(1);
        c.input.Clear();
        Snap s = c.Snapshot();
        EXPECT_EQ(s.score, 30);
        EXPECT_GT(s.hp, 6);                              // 站桩输出允许被咬
        EXPECT_EQ(s.enemies["E_Grunt1"].first, 2);
    }

    // 经 RunString 取编码串（与 Ctx::Snapshot 一致，这里需要原始串）
    std::string EncodeOf(Ctx& c) {
        c.inst.GetEngine()->RunString("_g_snap = GameStateEncode()");
        return c.Str("_g_snap");
    }

    void SaveTo(Ctx& c, const std::string& tag) {
        std::filesystem::create_directories(kScratch);
        SceneSnapshot live = CaptureScene(c.scene, c.bindings);
        ASSERT_TRUE(SaveSnapshotToFile(
            live, kScratch + "/" + tag + ".scene"));
        ASSERT_TRUE(c.reg.SaveManifest(kScratch + "/" + tag + "_manifest.json"));
    }

    // 冷启等价上下文：新注册表 + 存档场景 + 宿主回灌状态
    void ColdLoad(Ctx& b, const std::string& sceneFile,
                  const std::string& manifestFile,
                  const std::string& persistBlob) {
        b.BeginFresh(manifestFile.c_str());
        b.LoadAndBind(sceneFile);
        ASSERT_EQ(b.idx.size(), 11u);
        ASSERT_TRUE(b.inst.Initialize(b.DirectorPath(),
                                      ScriptInstance::Config{}));
        if (!persistBlob.empty())
            ASSERT_TRUE(b.inst.Execute("GameStateRestore('" +
                                       persistBlob + "')"));
        b.inst.OnCreate();
    }

    // 持续作战至胜利（调用方持有 J）；DBG 版本打印轨迹
    bool DriveVictory(Ctx& c, int maxFrames, bool dbg = false) {
        auto nav = CombatNav(c);
        for (int f = 0; f < maxFrames; ++f) {
            nav(f);
            c.inst.OnUpdate(1.0f / 60.0f);
            EXPECT_TRUE(c.inst.IsValid());
            if (!c.inst.IsValid()) return false;
            Snap s = c.Snapshot();
            if (dbg && f % 120 == 0)
                std::printf("    [dbg] f=%d hp=%d score=%d state=%s\n",
                            f, s.hp, s.score, s.state.c_str());
            if (s.state == "victory") return true;
            if (s.state == "lost") return false;
        }
        return false;
    }
}

// ════════════════════════════════════════════════════════════
// GP1-B5 · 战斗中存档：内容契约（谁死了、掉血多少、多少分，全部落盘）
// ════════════════════════════════════════════════════════════
TEST(GP01, SaveCombatState) {
    std::filesystem::remove_all(kScratch);
    Ctx c;
    SetupMidFight(c);

    Snap s = c.Snapshot();
    EXPECT_EQ(s.score, 30);
    EXPECT_GT(s.hp, 6);                                   // 允许战斗中被咬
    EXPECT_FALSE(s.enemies["E_Scout1"].second);
    EXPECT_FALSE(s.enemies["E_Scout2"].second);
    EXPECT_EQ(s.enemies["E_Grunt1"].first, 2);
    EXPECT_TRUE(s.enemies["E_Tank1"].second);

    SaveTo(c, "midfight");                                // 场景+清单落盘
    EXPECT_FALSE(EncodeOf(c).empty());
}

// ════════════════════════════════════════════════════════════
// GP1-B5 · 冷启动恢复：Score/HP/敌我状态全等，死者仍死，可继续打到胜利
// ════════════════════════════════════════════════════════════
TEST(GP01, RestartCombatState) {
    std::filesystem::remove_all(kScratch);
    Ctx a;
    SetupMidFight(a);
    SaveTo(a, "restart");
    const std::string blobA = EncodeOf(a);

    // ── Kill process → Restart ──
    Ctx b;
    ColdLoad(b, kScratch + "/restart.scene",
             kScratch + "/restart_manifest.json", blobA);

    // 状态全等（含死者仍死、掉血保留）
    const std::string blobB = EncodeOf(b);
    EXPECT_EQ(blobB, blobA) << "state drifted across restart";

    // 读档后重定位（正常玩家动作）：站桩存档点周围是敌群集结区，
    // 原地恢复会立即承伤 —— 移动到远离集结区的角落再继续
    EXPECT_TRUE(b.inst.Execute(
        "local h=Engine.entity.find('Player'); "
        "Engine.transform.set_position(h, 6, 0, -9)"));

    // 尸体不会复活：静置 120 帧
    b.Run(120);
    Snap s = b.Snapshot();
    EXPECT_FALSE(s.enemies["E_Scout1"].second);
    EXPECT_FALSE(s.enemies["E_Scout2"].second);
    EXPECT_EQ(s.enemies["E_Scout1"].first, 0);
    EXPECT_EQ(s.score, 30);

    // 活敌恢复追击（位置开始变化）
    Vec3 t0 = b.Pos("E_Tank1");
    b.Run(60);
    EXPECT_GT(std::hypot(b.Pos("E_Tank1").x - t0.x,
                         b.Pos("E_Tank1").z - t0.z), 0.2)
        << "alive enemy frozen after load";

    // 继续游戏直至胜利 —— 分数在恢复基础上累计到 120
    b.input.Press("J");
    ASSERT_TRUE(DriveVictory(b, 5400, true)) << "state="
                                              << b.Snapshot().state;
    b.input.Clear();
    EXPECT_EQ(b.Snapshot().score, 120);
}

// ════════════════════════════════════════════════════════════
// GP1-B6 · Game Production Evidence Test：
//   冷启 → 载入 → 移动 → 战斗 → 中场存档 → 冷启 → 全等 → 继续 → Victory
// ════════════════════════════════════════════════════════════
TEST(GP01, CoreGameplay) {
    std::filesystem::remove_all(kScratch);

    // ── Cold Start → Load Main.scene ──
    Ctx c;
    NewFight(c);

    // ── Player movement ──
    Vec3 p0 = c.Pos("Player");
    c.input.Press("W");
    c.Run(45);
    c.input.Clear();
    EXPECT_GT(std::hypot(c.Pos("Player").x - p0.x,
                         c.Pos("Player").z - p0.z), 2.5);

    // ── Combat：击杀首只 scout ──
    bool s1 = false;
    c.input.Press("J");
    for (int f = 0; f < 900 && !s1; ++f) {
        c.inst.OnUpdate(1.0f / 60.0f);
        ASSERT_TRUE(c.inst.IsValid());
        s1 = !c.Snapshot().enemies["E_Scout1"].second;
    }
    c.input.Clear();
    ASSERT_TRUE(s1);

    // ── Mid-fight Save ──
    SaveTo(c, "golden");
    const std::string blob = EncodeOf(c);

    // ── Process restart → Load → State equality ──
    Ctx b;
    ColdLoad(b, kScratch + "/golden.scene",
             kScratch + "/golden_manifest.json", blob);
    EXPECT_EQ(EncodeOf(b), blob);

    // ── 继续游戏 → Victory ──
    b.input.Press("J");
    ASSERT_TRUE(DriveVictory(b, 7200)) << "state=" << b.Snapshot().state;
    b.input.Clear();

    Snap fin = b.Snapshot();
    EXPECT_EQ(fin.score, 120);                    // 胜利时全场清空：Σ value 恒定
    EXPECT_GT(fin.hp, 0);
    for (auto& kv : fin.enemies)
        EXPECT_FALSE(kv.second.second) << kv.first << " survived";

    std::printf("    [GP01] B6 golden: cold->load->move->combat->save"
                "->restart->equal->continue->victory OK\n");
}
