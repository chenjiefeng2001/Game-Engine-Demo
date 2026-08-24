/**
 * @file GP01Harness.h
 * @brief GP01 生产契约测试共享 harness（test_gp01 各 TU 共用）
 *
 * 模式与 VS01Test 一致：可注入输入后端 + 干净注册表/场景/实例 Ctx。
 * 快照读取走 GameStateEncode（游戏自有存档格式，宿主中介）。
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

    // 敌人名册（与 game.lua ENEMY_ROSTER 对应；测试导航用）
    inline const std::vector<const char*>& Roster() {
        static const std::vector<const char*> k = {
            "E_Grunt1", "E_Grunt2", "E_Tank1", "E_Scout1", "E_Scout2"
        };
        return k;
    }

    struct TestInput : public GameplayAPI::IScriptInputProvider {
        std::map<std::string, bool> keys;
        bool IsKeyDown(const char* k) override {
            auto it = keys.find(k);
            return it != keys.end() && it->second;
        }
        void Press(const char* k) { keys[k] = true; }
        void Clear()              { keys.clear(); }
        void MoveAxis(float dx, float dz) {   // 航点 → WASD 轴向
            keys["W"] = dz < -0.05f; keys["S"] = dz > 0.05f;
            keys["A"] = dx < -0.05f; keys["D"] = dx > 0.05f;
        }
    };

    // ── 游戏状态快照（解析 game.lua 的存档编码串）──
    struct Snap {
        int score = -1, hp = -1;
        std::string state = "?";
        std::map<std::string, std::pair<int, bool>> enemies;  // name → (hp, alive)
    };

    inline Snap ParseSnap(const std::string& s) {
        Snap out;
        std::stringstream ss(s);
        std::string tok;
        std::vector<std::string> head;
        while (std::getline(ss, tok, '|')) {
            size_t c1 = tok.find(':');
            if (c1 == std::string::npos) {           // 头部字段 score/hp/state
                head.push_back(tok);
                continue;
            }
            size_t c2 = tok.find(':', c1 + 1);
            std::string name = tok.substr(0, c1);
            int hp = std::atoi(tok.substr(c1 + 1, c2 - c1 - 1).c_str());
            bool alive = tok.substr(c2 + 1) == "1";
            out.enemies[name] = { hp, alive };
        }
        if (head.size() >= 3) {
            out.score = std::atoi(head[0].c_str());
            out.hp    = std::atoi(head[1].c_str());
            out.state = head[2];
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
        Vec3 Pos(const char* name) {
            return scene.GetObjects()[idx.at(name)]->GetTransform().GetPosition();
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
        // 当前游戏快照（经游戏自有编码）
        Snap Snapshot() {
            inst.GetEngine()->RunString("_g_snap = GameStateEncode()");
            return ParseSnap(Str("_g_snap"));
        }
        bool Alive(const char* name) {
            auto s = Snapshot();
            auto it = s.enemies.find(name);
            return it != s.enemies.end() && it->second.second;
        }
    };

    // ── 导航/驱动 helper ──

    // 把玩家传送到 name 身旁 standoff 距离处（+z 方向）
    inline void TeleportNear(Ctx& c, const char* name, float standoff) {
        Vec3 e = c.Pos(name);
        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "local h=Engine.entity.find('Player'); "
            "Engine.transform.set_position(h, %.3f, 0, %.3f)",
            e.x, e.z + standoff);
        ASSERT_TRUE(c.inst.Execute(buf));
    }

    // 原地按住 J 直到目标死亡；返回是否死成
    inline bool HoldJUntilDead(Ctx& c, const char* name, int maxFrames) {
        c.input.Press("J");
        bool dead = false;
        for (int f = 0; f < maxFrames && !dead; ++f) {
            c.inst.OnUpdate(1.0f / 60.0f);
            if (!c.inst.IsValid()) { c.input.Clear(); return false; }
            dead = !c.Alive(name);
        }
        c.input.Clear();
        return dead;
    }

    // 战斗导航（带站桩走位）：
    //   d > 1.00        → 逼近目标（进入攻击带）
    //   0.90 ≤ d ≤ 1.00 → 停在攻击带内输出（ATK_RANGE 1.10 内、避开
    //                      grunt/scout 接触半径；tank 接触 1.05 无法避免，
    //                      属可接受的交换）
    //   d < 0.90        → 后撤脱出深水区
    // 全程仅用输入注入 —— 走位即 Lua/输入可表达的"距离管理"生产观察。
    inline std::function<void(int)> CombatNav(Ctx& c) {
        return [&c](int) {
            Snap s = c.Snapshot();
            if (s.state != "playing") { c.input.MoveAxis(0, 0); return; }
            Vec3 p = c.Pos("Player");
            const char* tgt = nullptr;
            double best = 1e9;
            for (const char* n : Roster()) {
                auto it = s.enemies.find(n);
                if (it == s.enemies.end() || !it->second.second) continue;
                Vec3 e = c.Pos(n);
                double d = std::hypot(e.x - p.x, e.z - p.z);
                if (d < best) { best = d; tgt = n; }
            }
            if (!tgt) { c.input.MoveAxis(0, 0); return; }
            Vec3 t = c.Pos(tgt);
            double dx = t.x - p.x, dz = t.z - p.z;
            double d = std::hypot(dx, dz);
            // 注意：只能动 WASD（MoveAxis），不得 Clear —— 会清掉调用方的 J
            if (d > 1.00)      c.input.MoveAxis(float(dx / d), float(dz / d));
            else if (d < 0.90) c.input.MoveAxis(float(-dx / d), float(-dz / d));
            else               c.input.MoveAxis(0, 0);
        };
    }

} // namespace gp01
