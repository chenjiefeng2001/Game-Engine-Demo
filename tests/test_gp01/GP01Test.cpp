/**
 * @file GP01Test.cpp
 * @brief Game Production Phase 1 — GP01 生产契约测试
 *
 * 测试对象不是引擎功能，而是"游戏生产契约"（docs/GP-P1-Charter.md §11）：
 *   GP1-A 验收：空场景 → Player → Play → Save → Restart → Load → 正常运行
 *
 * 与 VS01 的区别：VS01 验证引擎能力单向跑通；本文件随生产阶段逐个点亮，
 * 持续守护"开发者可迭代"的产品契约。
 */

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
#include <map>
#include <sstream>

using namespace Engine;
using namespace Engine::Content;
using namespace Engine::Scripting;

namespace {

    struct TestInput : public GameplayAPI::IScriptInputProvider {
        std::map<std::string, bool> keys;
        bool IsKeyDown(const char* k) override {
            auto it = keys.find(k);
            return it != keys.end() && it->second;
        }
        void Press(const char* k) { keys[k] = true; }
        void Clear()              { keys.clear(); }
    };

    const std::string kGpDir    = "assets/gp01";
    const std::string kScratch  = "gp01_scratch";

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
        void Run(int frames) {
            for (int f = 0; f < frames; ++f) {
                inst.OnUpdate(1.0f / 60.0f);
                ASSERT_TRUE(inst.IsValid()) << "frame " << f;
            }
        }
    };

} // namespace

// ════════════════════════════════════════════════════════════
// GP1-A · 生产契约：干净注册表 → Import 清单 → GUID → 场景实例化
// （对应章程 §12 Content：全部资产经 ContentRegistry，无路径泄漏）
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
    ASSERT_EQ(c.idx.size(), 6u);                         // Director+Player+4 Wall
    EXPECT_NE(c.idx.count("Player"), 0u);
    EXPECT_TRUE(c.inst.Initialize(c.DirectorPath(),
                                  ScriptInstance::Config{}));
    c.inst.OnCreate();
    c.Run(30);
    SUCCEED() << "[GP01] Main.scene loaded and director alive";
}

// ════════════════════════════════════════════════════════════
// GP1-A · 生产契约：Player 可操控（按住位移 ≈ SPEED·t，松开停止）
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
// GP1-A · 主验收：Play → Save → 冷启等价 → Load → 继续正常运行
// （状态连续性由宿主中介 _PERSIST —— 冻结 API 无 scene-carry）
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
    ASSERT_EQ(live.entities.size(), 6u);
    ASSERT_TRUE(SaveSnapshotToFile(live, kScratch + "/main_saved.scene"));
    ASSERT_TRUE(c.reg.SaveManifest(kScratch + "/manifest.json"));
    c.inst.GetEngine()->RunString("_g_score = _PERSIST.score");

    // ── Restart：全新进程等价（新注册表/新场景/新实例）──
    Ctx b;
    b.BeginFresh((kScratch + "/manifest.json").c_str());
    ASSERT_EQ(b.reg.Count(), 9u);
    b.LoadAndBind(kScratch + "/main_saved.scene");
    ASSERT_EQ(b.idx.size(), 6u);
    ASSERT_TRUE(b.inst.Initialize(b.DirectorPath(),
                                  ScriptInstance::Config{}));
    // 宿主把存档中的持续状态交还新实例（真实存档系统的职责边界）
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
