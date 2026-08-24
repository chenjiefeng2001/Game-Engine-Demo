/**
 * @file Dogfood06Test.cpp
 * @brief Dogfood-06 Production Efficiency smoke test
 *
 * 15 实体多类型战斗场景：加载 → handle 绑定 → 600 帧循环 → _PERSIST 校验
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

TEST(Dogfood06, LoadAndRun) {
    ContentRegistry reg;
    ASSERT_TRUE(reg.LoadManifest("assets/scenes/dogfood06.manifest.json"));

    SceneSnapshot snap; std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile("assets/scenes/dogfood06.scene", snap, err)) << err;

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
    EXPECT_EQ(GameplayAPI::HandleCount(), 15u);

    std::string scriptPath;
    for (size_t i = 0; i < snap.entities.size(); ++i)
        if (snap.entities[i].name == "Player")
            scriptPath = reg.ResolvePath(r.bindings[i].scriptGuid);
    ASSERT_FALSE(scriptPath.empty()) << "Player script GUID not bound";

    ScriptInstance inst;
    ScriptInstance::Config cfg; cfg.instructionBudget = 5000000;
    ASSERT_TRUE(inst.Initialize(scriptPath, cfg));
    inst.OnCreate();

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
        "assert(type(_PERSIST.playerHP) == 'number')\n"))
        << "_PERSIST structure broken after 600 frames";

    veng->RunString("g_state = _PERSIST.state");
    std::string stateVal = veng->GetGlobalString("g_state", "<nil>");
    std::printf("    [DF06] state='%s' validPos=%d\n", stateVal.c_str(), validPos);
    EXPECT_TRUE(stateVal == "playing" || stateVal == "gameover" || stateVal == "victory");
}
