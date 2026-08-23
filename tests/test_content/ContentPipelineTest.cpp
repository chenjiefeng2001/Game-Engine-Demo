/**
 * @file ContentPipelineTest.cpp
 * @brief Content Pipeline acceptance tests (Ring8-Ring12 + Gameplay API v2.1)
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

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <set>

using namespace Engine;
using namespace Engine::Content;
using namespace Engine::Scripting;

namespace {
    const std::string kDir = "content_scratch";
    void WriteFile(const std::string& name, const std::string& body) {
        std::filesystem::create_directories(kDir);
        std::ofstream f(kDir + "/" + name, std::ios::binary | std::ios::trunc);
        f << body;
    }
    std::string P(const std::string& name) { return kDir + "/" + name; }

    SceneSnapshot MakePlayerCube(ResourceGUID texG, ResourceGUID scrG) {
        SceneSnapshot snap;
        SerializedEntity player;
        player.name = "Player";
        player.px = 2.f; player.spriteGuid = texG; player.scriptGuid = scrG;
        snap.entities.push_back(player);
        SerializedEntity cube;
        cube.name = "Cube";
        cube.px = -3.f; cube.spriteGuid = texG;
        snap.entities.push_back(cube);
        return snap;
    }
    void ExpectSemanticEqual(const SceneSnapshot& a, const SceneSnapshot& b) {
        ASSERT_EQ(a.entities.size(), b.entities.size());
        for (size_t i = 0; i < a.entities.size(); ++i) {
            EXPECT_EQ(a.entities[i].name, b.entities[i].name);
            EXPECT_FLOAT_EQ(a.entities[i].px, b.entities[i].px);
            EXPECT_TRUE(a.entities[i].spriteGuid == b.entities[i].spriteGuid);
            EXPECT_TRUE(a.entities[i].scriptGuid == b.entities[i].scriptGuid);
        }
    }
}

// === Ring8: Asset Identity ===

TEST(ContentIdentity, R8_Import_IdempotentByPath) {
    ContentRegistry reg;
    auto g1 = reg.Import("assets/textures/test.png", AssetType::Texture);
    auto g2 = reg.Import("assets/textures/test.png", AssetType::Texture);
    EXPECT_TRUE(g1 == g2);
    EXPECT_FALSE(g1.IsNull());
    EXPECT_EQ(reg.Count(), 1u);
}

TEST(ContentIdentity, R8_DifferentPaths_DifferentGuids) {
    ContentRegistry reg;
    auto a = reg.Import("a.png", AssetType::Texture);
    auto b = reg.Import("b.lua", AssetType::Script);
    EXPECT_TRUE(a != b);
}

TEST(ContentIdentity, R8_ExplicitDuplicateGuid_Rejected) {
    ContentRegistry reg;
    auto g = reg.Import("a.png", AssetType::Texture);
    EXPECT_FALSE(reg.RegisterExplicit(g, "other.png", AssetType::Texture));
    auto other = ResourceGUID::Create();
    EXPECT_FALSE(reg.RegisterExplicit(other, "a.png", AssetType::Texture));
}

// === Ring9: Registry Manifest ===

TEST(ContentRegistryManifest, R9_Manifest_SurvivesSessionRestart) {
    std::string manifest = kDir + "/registry.json";
    ContentRegistry s1;
    auto tex = s1.Import("assets/textures/test.png", AssetType::Texture);
    auto scr = s1.Import("assets/scripts/dogfood_game.lua", AssetType::Script);
    ASSERT_TRUE(s1.SaveManifest(manifest));
    ContentRegistry s2;
    ASSERT_TRUE(s2.LoadManifest(manifest));
    EXPECT_EQ(s2.ResolvePath(tex), "assets/textures/test.png");
    EXPECT_EQ(s2.TypeOf(scr), AssetType::Script);
    EXPECT_EQ(s2.Count(), 2u);
}

TEST(ContentRegistryManifest, R9_CorruptedManifest_FailClean) {
    WriteFile("bad_manifest.json", "{ broken !!!");
    ContentRegistry reg;
    EXPECT_FALSE(reg.LoadManifest(P("bad_manifest.json")));
    EXPECT_EQ(reg.Count(), 0u);
}

// === Ring10/11: Round-trip ===

TEST(SceneRoundTrip, R10_R11_RoundTrip_SemanticEquality) {
    ContentRegistry reg;
    auto texG = reg.Import("assets/textures/test.png", AssetType::Texture);
    auto scrG = reg.Import("assets/scripts/sandbox_player.lua", AssetType::Script);
    auto snapA = MakePlayerCube(texG, scrG);
    ASSERT_TRUE(SaveSnapshotToFile(snapA, P("rt.scene")));
    SceneSnapshot snapB; std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile(P("rt.scene"), snapB, err)) << err;
    ExpectSemanticEqual(snapA, snapB);
    EXPECT_EQ(SerializeSnapshot(snapA).dump(), SerializeSnapshot(snapB).dump());
}

// === Ring12: Adversarial ===

TEST(SceneAdversarial, R12_MissingAsset_NonFatal) {
    ContentRegistry reg;
    auto snap = MakePlayerCube(ResourceGUID::Create(), ResourceGUID::Create());
    Engine::Scene scene;
    OpenGLGraphicsFactory gfxF;
    Engine::TextureManager texMgr(gfxF);
    Scripting::GameplayAPI::Reset();
    Scripting::GameplayAPI::SetScene(&scene);
    auto r = InstantiateScene(snap, scene, texMgr, reg);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(scene.GetObjectCount(), 2u);
    ASSERT_EQ(r.warnings.size(), 3u);
    Scripting::GameplayAPI::Reset();
}

TEST(SceneAdversarial, R12_PresentAssets_BindFully) {
    ContentRegistry reg;
    auto texG = reg.Import("assets/textures/test.png", AssetType::Texture);
    auto scrG = reg.Import("assets/scripts/sandbox_player.lua", AssetType::Script);
    auto snap = MakePlayerCube(texG, scrG);
    Engine::Scene scene;
    OpenGLGraphicsFactory gfxF;
    Engine::TextureManager texMgr(gfxF);
    Scripting::GameplayAPI::Reset();
    Scripting::GameplayAPI::SetScene(&scene);
    auto r = InstantiateScene(snap, scene, texMgr, reg);
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.warnings.empty());
    EXPECT_TRUE(r.bindings[0].scriptGuid == scrG);
    Scripting::GameplayAPI::Reset();
}

TEST(SceneAdversarial, R12_CorruptedScene_FailClean) {
    WriteFile("bad.scene", "{ broken");
    SceneSnapshot snap; std::string err;
    EXPECT_FALSE(LoadSnapshotFromFile(P("bad.scene"), snap, err));
    EXPECT_NE(err.find("corrupted"), std::string::npos);
    EXPECT_TRUE(snap.entities.empty());
}

// === Golden Gate ===

namespace { const char* kGS = "content_scratch/golden.scene"; const char* kGM = "content_scratch/golden_manifest.json"; }

TEST(ContentGolden, R13Golden_ProcessA_Save) {
    ContentRegistry reg;
    auto texG = reg.Import("assets/textures/test.png", AssetType::Texture);
    auto scrG = reg.Import("assets/scripts/sandbox_player.lua", AssetType::Script);
    ASSERT_TRUE(reg.SaveManifest(kGM));
    auto snap = MakePlayerCube(texG, scrG);
    ASSERT_TRUE(SaveSnapshotToFile(snap, kGS));
}

TEST(ContentGolden, R13Golden_ProcessB_RestartLoadPlay) {
    ASSERT_TRUE(std::filesystem::exists(kGS)) << "run ProcessA first";
    ContentRegistry reg;
    ASSERT_TRUE(reg.LoadManifest(kGM));
    SceneSnapshot snap; std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile(kGS, snap, err)) << err;
    Engine::Scene scene;
    OpenGLGraphicsFactory gfxF;
    Engine::TextureManager tm(gfxF);
    Scripting::GameplayAPI::Reset();
    Scripting::GameplayAPI::SetScene(&scene);
    auto r = InstantiateScene(snap, scene, tm, reg);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(scene.GetObjectCount(), 2u);
}

// === Ring14-lite: Runtime ===

TEST(ContentRuntime, R14_LoadedScene_ScriptDrivesEntity) {
    WriteFile("player_move.lua",
        "_PERSIST = _PERSIST or {}\n_PERSIST.updates = 0\nfunction OnUpdate(dt)\n    _PERSIST.updates = _PERSIST.updates + 1\nend\n");
    std::string scriptPath = P("player_move.lua");
    ContentRegistry reg;
    auto scrG = reg.Import(scriptPath, AssetType::Script);

    SceneSnapshot snap;
    SerializedEntity e; e.name = "Player"; e.scriptGuid = scrG; e.px = 5.f;
    snap.entities.push_back(e);
    Engine::Scene scene;
    OpenGLGraphicsFactory gfxF;
    Engine::TextureManager tm(gfxF);
    Scripting::GameplayAPI::Reset();
    Scripting::GameplayAPI::SetScene(&scene);
    auto r = InstantiateScene(snap, scene, tm, reg);
    ASSERT_TRUE(r.ok);

    ScriptInstance inst;
    ScriptInstance::Config cfg; cfg.instructionBudget = 2000000;
    ASSERT_TRUE(inst.Initialize(scriptPath, cfg));
    inst.OnCreate();
    inst.OnUpdate(0.016f);
    inst.OnUpdate(0.016f);
    bool updatesOk = inst.GetEngine()->RunString("assert(_PERSIST.updates == 2)");
    EXPECT_TRUE(updatesOk);
    EXPECT_FLOAT_EQ(scene.GetObjects()[0]->GetTransform().GetPosition().x, 5.0f);
}

// === Gameplay API v2.1: M001 + M005 ===

TEST(GameplayAPIV2, M001_EntityFind_ByName) {
    ContentRegistry reg;
    auto texG = reg.Import("t.png", AssetType::Texture);
    SceneSnapshot snap;
    SerializedEntity e1; e1.name = "Player"; e1.spriteGuid = texG;
    SerializedEntity e2; e2.name = "Goal";
    snap.entities = { e1, e2 };
    Engine::Scene scene;
    OpenGLGraphicsFactory gfxF;
    Engine::TextureManager tm(gfxF);
    Scripting::GameplayAPI::Reset();
    Scripting::GameplayAPI::SetScene(&scene);
    auto r = InstantiateScene(snap, scene, tm, reg);
    ASSERT_TRUE(r.ok);
    uint32_t eh = 1; for (auto& o : scene.GetObjects()) { Scripting::GameplayAPI::HandleAdopt(o); ++eh; }
    EXPECT_EQ(Scripting::GameplayAPI::HandleFindByName("Player"), 1u);
    EXPECT_EQ(Scripting::GameplayAPI::HandleFindByName("Goal"), 2u);
    EXPECT_EQ(Scripting::GameplayAPI::HandleFindByName("nonexistent"), 0u);
    Scripting::GameplayAPI::Reset();
}

// M005 verified separately in test_scripting.exe (see ScriptingMVPTest.cpp)
