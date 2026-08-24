/**
 * @file Dogfood07Test.cpp
 * @brief Dogfood-07 Tower Defense Lite smoke test
 *
 * 验证维度：可维护性压力下的完整 Content→Gameplay→Restart 循环
 *   1. 10 实体场景加载 + handle 绑定
 *   2. 600 帧游戏循环（gem 收集 / 计时器 / 状态机）
 *   3. Save → Load round-trip（Restart 语义）
 */

#include <gtest/gtest.h>
#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Resources/ResourceGUID.h"
#include "Engine/Core/RenderResources/TextureManager.h"

#include "Engine/Scripting/LuaEngine.h"
#include "Engine/Scripting/GameplayAPI.h"
#include "Engine/Scripting/ScriptAPI.h"
#include "Engine/Scripting/ScriptInstance.h"
#include "Engine/OpenGL/OpenGLGraphicsFactory.h"

#include <cmath>
#include <cstdio>

using namespace Engine;
using namespace Engine::Content;
using namespace Engine::Scripting;

TEST(Dogfood07, LoadRunRestart) {
    // ── 1. 加载场景 + 脚本 ──
    ContentRegistry reg;
    ASSERT_TRUE(reg.LoadManifest("assets/scenes/dogfood07.manifest.json"));

    SceneSnapshot snap; std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile("assets/scenes/dogfood07.scene", snap, err)) << err;

    Engine::Scene scene;
    OpenGLGraphicsFactory gfxF;
    TextureManager tm(gfxF);
    GameplayAPI::Reset();
    GameplayAPI::SetScene(&scene);
    auto r = InstantiateScene(snap, scene, tm, reg);
    ASSERT_TRUE(r.ok);

    uint32_t eh = 1;
    for (auto& o : scene.GetObjects()) {
        auto h = GameplayAPI::HandleAdopt(o);
        ASSERT_EQ(h, eh); ++eh;
    }
    EXPECT_EQ(GameplayAPI::HandleCount(), 10u);

    std::string scriptPath;
    for (size_t i = 0; i < snap.entities.size(); ++i)
        if (snap.entities[i].name == "Player")
            scriptPath = reg.ResolvePath(r.bindings[i].scriptGuid);
    ASSERT_FALSE(scriptPath.empty()) << "Player script GUID not bound";

    ScriptInstance inst;
    ScriptInstance::Config cfg; cfg.instructionBudget = 5000000;
    ASSERT_TRUE(inst.Initialize(scriptPath, cfg));
    inst.OnCreate();

    // ── 2. 600 帧游戏循环（无输入 → 纯计时器推进）──
    for (int f = 0; f < 600; ++f) {
        inst.OnUpdate(1.0f / 60.0f);
        ASSERT_TRUE(inst.IsValid()) << "instance died at frame " << f;
    }

    int validPos = 0;
    for (auto& o : scene.GetObjects()) {
        const auto& p = o->GetTransform().GetPosition();
        if (std::isfinite(p.x) && std::isfinite(p.z)) ++validPos;
    }
    EXPECT_GT(validPos, 0);

    auto* veng = inst.GetEngine();
    EXPECT_TRUE(veng->RunString(
        "assert(_PERSIST ~= nil)\n"
        "assert(type(_PERSIST.state) == 'string')\n"
        "assert(_PERSIST.timer >= 0)\n"
        "assert(_PERSIST.timer < 60.0)\n"
        "assert(type(_PERSIST.gemsCollected) == 'number')\n"
        "g_state = _PERSIST.state\n"
        "g_timer = _PERSIST.timer\n"))
        << "_PERSIST structure broken after 600 frames";

    std::string stateVal = veng->GetGlobalString("g_state", "<nil>");
    double timerVal = veng->GetGlobalDouble("g_timer", -1.0);
    std::printf("    [DF07] state='%s' timer=%.1f validPos=%d\n",
                stateVal.c_str(), timerVal, validPos);
    EXPECT_TRUE(stateVal == "playing" || stateVal == "victory");

    // ── 3. Restart 语义：Save → 新 Scene → Load ──
    ASSERT_TRUE(SaveSnapshotToFile(snap, "assets/scenes/_df07_roundtrip.scene"));
    SceneSnapshot snap2;
    ASSERT_TRUE(LoadSnapshotFromFile("assets/scenes/_df07_roundtrip.scene", snap2, err)) << err;
    EXPECT_EQ(snap2.entities.size(), snap.entities.size());

    Engine::Scene scene2;
    GameplayAPI::SetScene(&scene2);
    auto r2 = InstantiateScene(snap2, scene2, tm, reg);
    ASSERT_TRUE(r2.ok);
    EXPECT_EQ(scene2.GetObjectCount(), 10u);

    std::remove("assets/scenes/_df07_roundtrip.scene");
}
