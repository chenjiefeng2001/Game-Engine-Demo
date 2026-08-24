/**
 * @file Dogfood05Test.cpp
 * @brief Dogfood-05 Arena Survival smoke test
 */

#include <gtest/gtest.h>
#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Resources/ResourceGUID.h"

#include "Engine/Scripting/LuaEngine.h"
#include "Engine/Scripting/GameplayAPI.h"
#include "Engine/Scripting/ScriptAPI.h"
#include "Engine/Scripting/ScriptInstance.h"

#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace Engine;
using namespace Engine::Content;
using namespace Engine::Scripting;

TEST(Dogfood05, LoadAndRun) {
    ContentRegistry reg;
    ASSERT_TRUE(reg.LoadManifest("assets/scenes/dogfood05.manifest.json"));

    SceneSnapshot snap; std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile("assets/scenes/dogfood05.scene", snap, err)) << err;

    Engine::Scene scene;
    OpenGLGraphicsFactory gfxF;
    TextureManager tm(gfxF);
    GameplayAPI::Reset();
    GameplayAPI::SetScene(&scene);
    auto r = InstantiateScene(snap, scene, tm, reg);
    ASSERT_TRUE(r.ok);

    // Adopt all entities into handle system
    uint32_t eh = 1;
    for (auto& o : scene.GetObjects()) {
        auto h = GameplayAPI::HandleAdopt(o);
        ASSERT_EQ(h, eh); ++eh;
    }
    EXPECT_EQ(GameplayAPI::HandleCount(), 13u);

    // Find and load player script
    std::string scriptPath;
    for (size_t i = 0; i < snap.entities.size(); ++i)
        if (snap.entities[i].name == "Player")
            scriptPath = reg.ResolvePath(r.bindings[i].scriptGuid);
    ASSERT_FALSE(scriptPath.empty());

    ScriptInstance inst;
    ScriptInstance::Config cfg; cfg.instructionBudget = 5000000;
    ASSERT_TRUE(inst.Initialize(scriptPath, cfg));
    inst.OnCreate();

    // Drive 600 frames (10 seconds simulated gameplay)
    for (int f = 0; f < 600; ++f) {
        inst.OnUpdate(1.0f / 60.0f);
        ASSERT_TRUE(inst.IsValid()) << "instance died at frame " << f;
    }

    // Verify entities have valid positions (not NaN/inf from bad physics)
    int validPos = 0;
    for (auto& o : scene.GetObjects()) {
        const auto& p = o->GetTransform().GetPosition();
        if (std::isfinite(p.x) && std::isfinite(p.z)) ++validPos;
    }
    EXPECT_GT(validPos, 0) << "no entities with valid positions";

    // Verify _PERSIST is accessible via Lua
    auto* veng = inst.GetEngine();
    EXPECT_TRUE(veng->RunString(
        "assert(_PERSIST ~= nil)\n"
        "assert(type(_PERSIST.state) == 'string')\n"
        "assert(_PERSIST.timer >= 0)\n"));

    std::printf("    [DF05] 600 frames OK, %d valid positions\n", validPos);
}
