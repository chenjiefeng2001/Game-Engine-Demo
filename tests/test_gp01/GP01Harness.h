/**
 * @file GP01Harness.h
 * @brief GP01 生产契约测试共享 harness（test_gp01 各 TU 共用）
 *
 * GP1-C 版本：敌人动态生成（E_NNN 命名），快照含波次/计数/坐标；
 * 存档 = 静态布局场景文件 ＋ 游戏自有 blob（GameStateEncode/Restore）。
 */

#pragma once

#include <gtest/gtest.h>
#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Resources/ResourceGUID.h"
#include "Engine/Core/RenderResources/TextureManager.h"

#include "Engine/Scripting/GameplayAPI.h"
#include "Engine/Scripting/LuaEngine.h"
#include "Engine/Scripting/ScriptInstance.h"
#include "Engine/OpenGL/OpenGLGraphicsFactory.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <sstream>
#include <vector>

using namespace Engine;
using namespace Engine::Content;
using namespace Engine::Scripting;

namespace gp01 {

    const std::string kGpDir = "assets/gp01";
    const std::string kScratch = "gp01_scratch";

    struct TestInput : public GameplayAPI::IScriptInputProvider {
        std::map<std::string, bool> keys;
        bool IsKeyDown(const char* k) override {
            auto it = keys.find(k);
            return it != keys.end() && it->second;
        }
        void Press(const char* k) { keys[k] = true; }
        void Clear()              { keys.clear(); }
        void MoveAxis(float dx, float dz) {
            keys["W"] = dz < -0.05f; keys["S"] = dz > 0.05f;
            keys["A"] = dx < -0.05f; keys["D"] = dx > 0.05f;
        }
    };

    // ── 游戏状态快照（解析 game.lua 存档编码 v3）──
    // 头部：score|hp|state|waveIdx|startGrace|spawned|destroyed
    // 条目：name:type:hp:alive:x:z
    struct EnemyState {
        std::string type;
        int hp = 0;
        bool alive = false;
        float x = 0.f, z = 0.f;
    };
    struct Snap {
        int score = -1, hp = -1;
        int wave = -1, spawned = -1, destroyed = -1;
        double grace = -1;
        std::string state = "?";
        std::map<std::string, EnemyState> enemies;

        std::vector<std::string> AliveNames() const {
            std::vector<std::string> out;
            for (auto& kv : enemies)
                if (kv.second.alive) out.push_back(kv.first);
            return out;
        }
        int AliveCount() const {
            return static_cast<int>(AliveNames().size());
        }
    };

    inline Snap ParseSnap(const std::string& s) {
        Snap out;
        std::stringstream ss(s);
        std::string tok;
        std::vector<std::string> head;
        while (std::getline(ss, tok, '|')) {
            size_t c1 = tok.find(':');
            if (c1 == std::string::npos) { head.push_back(tok); continue; }
            EnemyState es;
            size_t c2 = tok.find(':', c1 + 1);
            size_t c3 = tok.find(':', c2 + 1);
            size_t c4 = tok.find(':', c3 + 1);
            size_t c5 = tok.find(':', c4 + 1);
            std::string name = tok.substr(0, c1);
            es.type  = tok.substr(c1 + 1, c2 - c1 - 1);
            es.hp    = std::atoi(tok.substr(c2 + 1, c3 - c2 - 1).c_str());
            es.alive = tok.substr(c3 + 1, c4 - c3 - 1) == "1";
            es.x     = static_cast<float>(std::atof(tok.substr(c4 + 1, c5 - c4 - 1).c_str()));
            es.z     = static_cast<float>(std::atof(tok.substr(c5 + 1).c_str()));
            out.enemies[name] = es;
        }
        if (head.size() >= 7) {
            out.score     = std::atoi(head[0].c_str());
            out.hp        = std::atoi(head[1].c_str());
            out.state     = head[2];
            out.wave      = std::atoi(head[3].c_str());
            out.grace     = std::atof(head[4].c_str());
            out.spawned   = std::atoi(head[5].c_str());
            out.destroyed = std::atoi(head[6].c_str());
        }
        return out;
    }

    struct Ctx {
        ContentRegistry       reg;
        OpenGLGraphicsFactory gfxF;
        TextureManager        tm{gfxF};
        Scene                 scene;
        ScriptInstance        inst;
        TestInput             input;
        std::vector<EntityContentBinding> bindings;
        std::unordered_map<std::string, size_t> idx;

        void BeginFresh(const char* manifestPath) {
            inst.Shutdown();
            GameplayAPI::Reset();
            scene = Scene{};
            GameplayAPI::SetScene(&scene);
            GameplayAPI::SetInputProvider(&input);
            if (manifestPath)
                ASSERT_TRUE(reg.LoadManifest(manifestPath));
        }
        void LoadAndBind(const std::string& scenePath) {
            SceneSnapshot snap; std::string err;
            EXPECT_TRUE(LoadSnapshotFromFile(scenePath, snap, err)) << err;
            auto r = InstantiateScene(snap, scene, tm, reg);
            EXPECT_TRUE(r.ok);
            bindings = r.bindings;
            idx.clear();
            uint32_t h = 1;
            for (auto& o : scene.GetObjects()) {
                EXPECT_EQ(GameplayAPI::HandleAdopt(o), h); ++h;
                idx[o->GetName()] = idx.size();
            }
        }
        std::string DirectorPath() const {
            return reg.ResolvePath(bindings[0].scriptGuid);
        }
        Vec3 Pos(const std::string& name) {
            return scene.GetObjects()[idx.at(name)]->GetTransform().GetPosition();
        }
        bool InScene(const std::string& name) const {
            return idx.count(name) != 0;
        }
        double Num(const char* global) {
            return inst.GetEngine()->GetGlobalDouble(global, -1e9);
        }
        std::string Str(const char* global) {
            return inst.GetEngine()->GetGlobalString(global, "");
        }
        void Run(int frames) {
            for (int f = 0; f < frames; ++f) {
                inst.OnUpdate(1.0f / 60.0f);
                ASSERT_TRUE(inst.IsValid()) << "frame " << f;
            }
        }
        Snap Snapshot() {
            inst.GetEngine()->RunString("_g_snap = GameStateEncode()");
            return ParseSnap(Str("_g_snap"));
        }
        std::string Blob() {
            inst.GetEngine()->RunString("_g_snap = GameStateEncode()");
            return Str("_g_snap");
        }
    };

    // ── 流程助手 ──

    // 开一局：干净注册表 + Main.scene + 导演就绪（未开始生成，宽限期内）
    inline void StartRun(Ctx& c,
                         const std::string& manifest = std::string()) {
        c.BeginFresh((manifest.empty() ? kGpDir + "/manifest.json"
                                       : manifest).c_str());
        c.LoadAndBind(kGpDir + "/Main.scene");
        ASSERT_TRUE(c.inst.Initialize(c.DirectorPath(),
                                      ScriptInstance::Config{}));
        c.inst.OnCreate();
    }

    // 冻结波次机器（几何类测试用）：宽限拉满即不生成
    inline void FreezeWaves(Ctx& c) {
        ASSERT_TRUE(c.inst.Execute("_PERSIST.startGrace = 999999"));
    }

    inline std::string SpawnOne(Ctx& c, const std::string& type) {
        c.inst.GetEngine()->RunString(
            "_g_sn = SpawnAt('" + type + "')");
        return c.Str("_g_sn");
    }

    // 把玩家传送到目标身旁 standoff 处（+z 方向）。
    // 目标可为静态场景实体或动态生成体（后者经 Lua find 取实时坐标）。
    inline void TeleportNear(Ctx& c, const std::string& name, float standoff) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "local t=Engine.entity.find('%s') "
            "if not t then error('teleport target missing') end "
            "local tx,ty,tz=Engine.transform.get_position(t) "
            "local h=Engine.entity.find('Player') "
            "Engine.transform.set_position(h, tx, 0, tz + %.3f)",
            name.c_str(), standoff);
        ASSERT_TRUE(c.inst.Execute(buf))
            << "lua err='" << c.inst.GetEngine()->GetLastError().message
            << "' buf=" << buf;
    }

    // 把任意实体传送到绝对坐标（动态体走句柄）
    inline void TeleportEntity(Ctx& c, const std::string& name,
                               float x, float z) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "local h=Engine.entity.find('%s') "
            "Engine.transform.set_position(h, %.3f, 0, %.3f)",
            name.c_str(), x, z);
        ASSERT_TRUE(c.inst.Execute(buf))
            << "lua err='" << c.inst.GetEngine()->GetLastError().message
            << "' buf=" << buf;
    }

    // 战斗导航（攻击带走位）：类型感知安全 standoff ——
    //   holdLo = 接触半径+0.41（接触+0.01 余量，退后线）
    //   holdHi = max(holdLo+0.05, 1.00)（逼近线，≤ ATK_RANGE 1.10 可输出；
    //            tank 1.11 → 敌自寻的进入 1.10 后命中，无死区）
    //   grunt/scout 行为与旧 [0.90,1.00] 带等价；tank 从此可站外输出
    //   （接触 1.05 > 旧带上限，否则全战役 6 tank 必打死玩家）。
    //   boss 接触 1.21 > ATK_RANGE —— 无法安全输出（boss 不出现在战斗契约波次）。
    // 只动 WASD —— 不碰调用方按下的 J。
    // 目标与坐标全部来自快照（动态生成体不在加载期 idx 内，blob 自带坐标）。
    inline std::function<void(int)> CombatNav(Ctx& c) {
        static const std::map<std::string, float> kContactR = {
            { "grunt", 0.45f }, { "tank", 0.65f },
            { "scout", 0.35f }, { "boss", 0.80f },
        };
        return [&c](int) {
            Snap s = c.Snapshot();
            if (s.state != "playing") { c.input.MoveAxis(0, 0); return; }
            Vec3 p = c.Pos("Player");
            const std::string* tgt = nullptr;
            std::string ttype;
            float tx = 0.f, tz = 0.f;
            double best = 1e9;
            for (auto& kv : s.enemies) {
                if (!kv.second.alive) continue;
                double d = std::hypot(kv.second.x - p.x, kv.second.z - p.z);
                if (d < best) {
                    best = d; tgt = &kv.first; ttype = kv.second.type;
                    tx = kv.second.x; tz = kv.second.z;
                }
            }
            if (!tgt) { c.input.MoveAxis(0, 0); return; }
            float rad = 0.45f;
            auto rit = kContactR.find(ttype);
            if (rit != kContactR.end()) rad = rit->second;
            float holdLo = std::max(rad + 0.41f, 0.85f);
            float holdHi = std::max(holdLo + 0.05f, 1.00f);
            double dx = tx - p.x, dz = tz - p.z;
            if (best > holdHi)      c.input.MoveAxis(float(dx / best), float(dz / best));
            else if (best < holdLo) c.input.MoveAxis(float(-dx / best), float(-dz / best));
            else                    c.input.MoveAxis(0, 0);
        };
    }

    // 持续作战至胜利/败北（调用方持有 J）；返回终态 state
    inline std::string DriveBattle(Ctx& c, int maxFrames, bool dbg = false) {
        auto nav = CombatNav(c);
        std::string st = "?";
        for (int f = 0; f < maxFrames; ++f) {
            nav(f);
            c.inst.OnUpdate(1.0f / 60.0f);
            EXPECT_TRUE(c.inst.IsValid());
            if (!c.inst.IsValid()) return "invalid";
            Snap s = c.Snapshot();
            st = s.state;
            if (dbg && f % 240 == 0)
                std::printf("    [dbg] f=%d hp=%d score=%d wave=%d "
                            "alive=%d state=%s\n",
                            f, s.hp, s.score, s.wave,
                            s.AliveCount(), st.c_str());
            if (st == "victory" || st == "lost") break;
        }
        c.input.Clear();
        return st;
    }

    // 精确击杀：导航+按住 J 直到 destroyed 达标
    inline void KillUntilDestroyed(Ctx& c, int n, int maxFrames = 3600) {
        auto nav = CombatNav(c);
        c.input.Press("J");
        for (int f = 0; f < maxFrames; ++f) {
            nav(f);
            c.inst.OnUpdate(1.0f / 60.0f);
            EXPECT_TRUE(c.inst.IsValid());
            if (!c.inst.IsValid()) break;
            if (c.Snapshot().destroyed >= n) break;
        }
        c.input.Clear();
    }

    // 等待某波生成完毕（alive 达到编成数）
    inline bool WaitForWave(Ctx& c, int wave, int aliveN,
                            int maxFrames = 900) {
        for (int f = 0; f < maxFrames; ++f) {
            c.inst.OnUpdate(1.0f / 60.0f);
            if (!c.inst.IsValid()) return false;
            Snap s = c.Snapshot();
            if (s.wave >= wave && s.AliveCount() >= aliveN) return true;
        }
        return false;
    }

    // 静态布局落盘（开局即捕获 —— 场景=布局资产，不含运行时实体）
    inline void PristineSave(Ctx& c, const std::string& tag) {
        std::filesystem::create_directories(kScratch);
        SceneSnapshot live = CaptureScene(c.scene, c.bindings);
        ASSERT_TRUE(SaveSnapshotToFile(live,
                    kScratch + "/" + tag + ".scene"));
        ASSERT_TRUE(c.reg.SaveManifest(
                    kScratch + "/" + tag + "_manifest.json"));
    }

    // 冷启等价：新注册表 + 布局场景 + blob 回灌 → OnCreate
    inline void ColdStart(Ctx& b, const std::string& tag,
                          const std::string& blob) {
        b.BeginFresh((kScratch + "/" + tag + "_manifest.json").c_str());
        b.LoadAndBind(kScratch + "/" + tag + ".scene");
        ASSERT_TRUE(b.inst.Initialize(b.DirectorPath(),
                                      ScriptInstance::Config{}));
        if (!blob.empty())
            ASSERT_TRUE(b.inst.Execute("GameStateRestore('" + blob + "')"));
        b.inst.OnCreate();
    }

} // namespace gp01
