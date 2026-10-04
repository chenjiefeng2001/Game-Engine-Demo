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

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

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

// === SceneSerializerV1 失败契约（JSON schema 校验）===
//
// 本组用例针对 DeserializeSnapshot / LoadSnapshotFromFile 的**已定义**
// 失败与宽容契约，不涉及 GL / TextureManager，可 headless 运行。
//
// 快照 schema:
//   { "format": "engine.scene", "version": <int>,
//     "entities": [ { "name": <string>, "position": [x,y,z],
//                     "sprite": <hex>, "script": <hex>,
//                     "components": [ { "type": <string>, "data": {...} } ] } ] }
//
// 既有的宽容契约（源码注释明确，予以保留）：
//   - 实体缺 name          → 跳过该实体，整体仍加载
//   - position 缺失/长度<3 → 该实体用默认坐标
//   - component 缺 type    → 跳过该 component，整体仍加载
// 严格失败的契约（根级）：
//   - 根不是 object / format 不符 / entities 缺失或非数组 / 版本超前

TEST(SceneSerializerV1Contract, RootNotAnObject_ReturnsFalse) {
    nlohmann::json j = nlohmann::json::array({1, 2, 3});
    SceneSnapshot snap; std::string err;
    EXPECT_FALSE(DeserializeSnapshot(j, snap, err));
    EXPECT_FALSE(err.empty());
    EXPECT_TRUE(snap.entities.empty());
}

TEST(SceneSerializerV1Contract, MissingFormat_ReturnsFalse) {
    nlohmann::json j;
    j["entities"] = nlohmann::json::array();
    SceneSnapshot snap; std::string err;
    EXPECT_FALSE(DeserializeSnapshot(j, snap, err));
    EXPECT_NE(err.find("engine.scene"), std::string::npos);
}

TEST(SceneSerializerV1Contract, WrongFormatValue_ReturnsFalse) {
    nlohmann::json j;
    j["format"] = "something.else";
    j["entities"] = nlohmann::json::array();
    SceneSnapshot snap; std::string err;
    EXPECT_FALSE(DeserializeSnapshot(j, snap, err));
}

TEST(SceneSerializerV1Contract, MissingEntitiesArray_ReturnsFalse) {
    nlohmann::json j;
    j["format"] = "engine.scene";
    SceneSnapshot snap; std::string err;
    EXPECT_FALSE(DeserializeSnapshot(j, snap, err));
    EXPECT_NE(err.find("entities"), std::string::npos);
}

TEST(SceneSerializerV1Contract, EntitiesNotAnArray_ReturnsFalse) {
    nlohmann::json j;
    j["format"] = "engine.scene";
    j["entities"] = "not_an_array";
    SceneSnapshot snap; std::string err;
    EXPECT_FALSE(DeserializeSnapshot(j, snap, err));
}

TEST(SceneSerializerV1Contract, FutureVersion_ReturnsFalse) {
    nlohmann::json j;
    j["format"] = "engine.scene";
    j["version"] = 9999;
    j["entities"] = nlohmann::json::array();
    SceneSnapshot snap; std::string err;
    EXPECT_FALSE(DeserializeSnapshot(j, snap, err));
    EXPECT_NE(err.find("version"), std::string::npos);
}

// ── 宽容契约：必须保持现状，不因修 bug 而改变 ──────────────────────────

TEST(SceneSerializerV1Contract, EntityWithoutName_IsSkipped_OverallStillLoads) {
    nlohmann::json j;
    j["format"] = "engine.scene";
    j["entities"] = nlohmann::json::array({
        nlohmann::json{{"name", "kept"}},
        nlohmann::json{{"px", 1.0}},                 // 无 name → 跳过
        nlohmann::json{{"name", 42}},               // name 类型错 → 跳过
        nlohmann::json{{"name", "kept2"}},
    });
    SceneSnapshot snap; std::string err;
    ASSERT_TRUE(DeserializeSnapshot(j, snap, err)) << err;
    ASSERT_EQ(snap.entities.size(), 2u);
    EXPECT_EQ(snap.entities[0].name, "kept");
    EXPECT_EQ(snap.entities[1].name, "kept2");
}

TEST(SceneSerializerV1Contract, EntityWithShortOrMissingPosition_UsesDefaultCoords) {
    nlohmann::json j;
    j["format"] = "engine.scene";
    j["entities"] = nlohmann::json::array({
        nlohmann::json{{"name", "short"}, {"position", nlohmann::json::array({1.0})}},
        nlohmann::json{{"name", "missing"}},
    });
    SceneSnapshot snap; std::string err;
    ASSERT_TRUE(DeserializeSnapshot(j, snap, err)) << err;
    ASSERT_EQ(snap.entities.size(), 2u);
    EXPECT_FLOAT_EQ(snap.entities[0].px, 0.0f);
    EXPECT_FLOAT_EQ(snap.entities[1].px, 0.0f);
}

TEST(SceneSerializerV1Contract, ComponentWithoutType_IsSkipped_OverallStillLoads) {
    nlohmann::json j;
    j["format"] = "engine.scene";
    nlohmann::json good{{"type", "ContractComp"}, {"data", nlohmann::json::object()}};
    nlohmann::json noType{{"data", nlohmann::json::object()}};
    nlohmann::json badType{{"type", 7}, {"data", nlohmann::json::object()}};
    nlohmann::json notObj = 5;
    j["entities"] = nlohmann::json::array({
        nlohmann::json{{"name", "e"}, {"components",
            nlohmann::json::array({good, noType, badType, notObj})}},
    });
    SceneSnapshot snap; std::string err;
    ASSERT_TRUE(DeserializeSnapshot(j, snap, err)) << err;
    ASSERT_EQ(snap.entities.size(), 1u);
    ASSERT_EQ(snap.entities[0].components.size(), 1u);
    EXPECT_EQ(snap.entities[0].components[0].type, "ContractComp");
}

TEST(SceneSerializerV1Contract, ValidSnapshot_WithOptionalFieldsAbsent_Loads) {
    nlohmann::json j;
    j["format"] = "engine.scene";
    j["version"] = 1;
    j["entities"] = nlohmann::json::array({
        nlohmann::json{{"name", "plain"}, {"position", nlohmann::json::array({1.0, 2.0, 3.0})}},
    });
    SceneSnapshot snap; std::string err;
    ASSERT_TRUE(DeserializeSnapshot(j, snap, err)) << err;
    ASSERT_EQ(snap.entities.size(), 1u);
    EXPECT_FLOAT_EQ(snap.entities[0].px, 1.0f);
    EXPECT_FLOAT_EQ(snap.entities[0].py, 2.0f);
    EXPECT_FLOAT_EQ(snap.entities[0].pz, 3.0f);
    EXPECT_TRUE(snap.entities[0].spriteGuid.IsNull());
    EXPECT_TRUE(snap.entities[0].scriptGuid.IsNull());
}

TEST(SceneSerializerV1Contract, LoadSnapshotFromFile_ValidSnapshot_NormalLoad) {
    WriteFile("v1_valid.scene", R"({"format":"engine.scene","version":1,"entities":[
        {"name":"a","position":[1,2,3]}]})");
    SceneSnapshot snap; std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile(P("v1_valid.scene"), snap, err)) << err;
    ASSERT_EQ(snap.entities.size(), 1u);
    EXPECT_EQ(snap.entities[0].name, "a");
    EXPECT_TRUE(err.empty());
}

TEST(SceneSerializerV1Contract, LoadSnapshotFromFile_MalformedJson_StillFailsClean) {
    WriteFile("v1_broken.scene", "{ broken");
    SceneSnapshot snap; std::string err;
    EXPECT_FALSE(LoadSnapshotFromFile(P("v1_broken.scene"), snap, err));
    EXPECT_NE(err.find("corrupted"), std::string::npos);
    EXPECT_TRUE(snap.entities.empty());
}

TEST(SceneSerializerV1Contract, LoadSnapshotFromFile_MissingFile_FailsClean) {
    SceneSnapshot snap; std::string err;
    EXPECT_FALSE(LoadSnapshotFromFile(P("v1_absent.scene"), snap, err));
    EXPECT_NE(err.find("cannot open"), std::string::npos);
}

// === 标量类型契约 ===
//
// 修复前有两处提取不做类型校验（version、position 元素），会抛
// nlohmann::json::type_error 并穿出返回 bool 的加载器 —— 本组用例曾以
// EXPECT_THROW 记录该缺陷，现已改为断言正式契约，且不再保留 EXPECT_THROW。
//
// 区分两类字段（与源码既有宽容契约一致）：
//   - version 是根级字段，类型不符无法解释 → 硬失败 false + err
//   - position 是可选的实体级数据，不可用时按既有约定退回默认坐标

TEST(SceneSerializerV1TypeContract, VersionWrongType_ReturnsFalseWithErr) {
    nlohmann::json j;
    j["format"] = "engine.scene";
    j["version"] = "1";                 // 期望 int
    j["entities"] = nlohmann::json::array();
    SceneSnapshot snap; std::string err;
    EXPECT_FALSE(DeserializeSnapshot(j, snap, err));
    EXPECT_NE(err.find("version"), std::string::npos);
    EXPECT_TRUE(snap.entities.empty());
}

TEST(SceneSerializerV1TypeContract, VersionWrongType_ViaFile_ReturnsFalseNotThrow) {
    WriteFile("v1_badver.scene",
              R"({"format":"engine.scene","version":"1","entities":[]})");
    SceneSnapshot snap; std::string err;
    EXPECT_FALSE(LoadSnapshotFromFile(P("v1_badver.scene"), snap, err));
    EXPECT_FALSE(err.empty());
    EXPECT_TRUE(snap.entities.empty());
}

TEST(SceneSerializerV1TypeContract, PositionElementWrongType_FallsBackToDefaultCoords) {
    // position 是可选字段：元素类型不可用时按既有宽容契约退回默认坐标，
    // 而不是让整个快照失败。
    nlohmann::json j;
    j["format"] = "engine.scene";
    j["entities"] = nlohmann::json::array({
        nlohmann::json{{"name", "e"},
                       {"position", nlohmann::json::array({"a", "b", "c"})}},
    });
    SceneSnapshot snap; std::string err;
    ASSERT_TRUE(DeserializeSnapshot(j, snap, err)) << err;
    ASSERT_EQ(snap.entities.size(), 1u);
    EXPECT_EQ(snap.entities[0].name, "e");
    EXPECT_FLOAT_EQ(snap.entities[0].px, 0.0f);
    EXPECT_FLOAT_EQ(snap.entities[0].py, 0.0f);
    EXPECT_FLOAT_EQ(snap.entities[0].pz, 0.0f);
}

TEST(SceneSerializerV1TypeContract, PositionMixedTypes_KeepsDefaultsForWholeTriple) {
    nlohmann::json j;
    j["format"] = "engine.scene";
    j["entities"] = nlohmann::json::array({
        nlohmann::json{{"name", "mixed"},
                       {"position", nlohmann::json::array({1.5, "bad", 3.5})}},
    });
    SceneSnapshot snap; std::string err;
    ASSERT_TRUE(DeserializeSnapshot(j, snap, err)) << err;
    ASSERT_EQ(snap.entities.size(), 1u);
    EXPECT_FLOAT_EQ(snap.entities[0].px, 0.0f);
    EXPECT_FLOAT_EQ(snap.entities[0].pz, 0.0f);
}

TEST(SceneSerializerV1TypeContract, PositionWithIntegerElements_IsAccepted) {
    // 整型元素是合法数值，必须被接受（避免把 number_unsigned 误判为坏类型）
    nlohmann::json j;
    j["format"] = "engine.scene";
    j["entities"] = nlohmann::json::array({
        nlohmann::json{{"name", "ints"},
                       {"position", nlohmann::json::array({1, 2, 3})}},
    });
    SceneSnapshot snap; std::string err;
    ASSERT_TRUE(DeserializeSnapshot(j, snap, err)) << err;
    ASSERT_EQ(snap.entities.size(), 1u);
    EXPECT_FLOAT_EQ(snap.entities[0].px, 1.0f);
    EXPECT_FLOAT_EQ(snap.entities[0].pz, 3.0f);
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

// RLCycle contract tests moved to ResourceLifecycleTest.cpp (I3: dedicated file)
// === Texture Resource Golden Path (M2-B) ===
// Chain: PNG -> Import -> GUID -> ResolvePath -> SpriteComponent -> Renderer

TEST(TextureGoldenPath, T1_Import_IdentityStable) {
    ContentRegistry reg;
    auto g1 = reg.Import("assets/textures/test.png", AssetType::Texture);
    auto g2 = reg.Import("assets/textures/test.png", AssetType::Texture);
    ASSERT_TRUE(g1 == g2);
    EXPECT_FALSE(g1.IsNull());
}

TEST(TextureGoldenPath, T2_GuidResolves_ToRegisteredPath) {
    ContentRegistry reg;
    auto guid = reg.Import("assets/textures/test.png", AssetType::Texture);
    std::string path = reg.ResolvePath(guid);
    EXPECT_EQ(path, "assets/textures/test.png");
}

TEST(TextureGoldenPath, T3_Reload_PreservesGuid) {
    std::string manifest = kDir + "/tex_manifest.json";
    ContentRegistry regA;
    auto guidA = regA.Import("assets/textures/test.png", AssetType::Texture);
    ASSERT_TRUE(regA.SaveManifest(manifest));

    ContentRegistry regB;
    ASSERT_TRUE(regB.LoadManifest(manifest));
    EXPECT_EQ(regB.ResolvePath(guidA), "assets/textures/test.png");

    auto guidB = regB.Import("assets/textures/test.png", AssetType::Texture);
    EXPECT_TRUE(guidA == guidB);
}

TEST(TextureGoldenPath, T4_SceneSpriteGuid_RoundTrip) {
    ContentRegistry reg;
    auto texG = reg.Import("assets/textures/test.png", AssetType::Texture);

    SceneSnapshot snap;
    SerializedEntity e; e.name = "TexturedCube";
    e.spriteGuid = texG; e.px = 1.0f;
    snap.entities.push_back(e);

    ASSERT_TRUE(SaveSnapshotToFile(snap, kDir + "/tex_rt.scene"));

    SceneSnapshot loaded; std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile(kDir + "/tex_rt.scene", loaded, err)) << err;

    ASSERT_EQ(loaded.entities.size(), 1u);
    EXPECT_TRUE(loaded.entities[0].spriteGuid == texG);
    EXPECT_EQ(loaded.entities[0].name, "TexturedCube");
}

TEST(TextureGoldenPath, T5_MissingTextureGuid_CleanFailure) {
    ContentRegistry reg;
    auto fakeGuid = ResourceGUID::Create();

    SceneSnapshot snap;
    SerializedEntity e; e.name = "BrokenSprite"; e.spriteGuid = fakeGuid;
    snap.entities.push_back(e);

    Engine::Scene scene;
    OpenGLGraphicsFactory gfxF;
    Engine::TextureManager tm(gfxF);
    Scripting::GameplayAPI::Reset();
    Scripting::GameplayAPI::SetScene(&scene);
    auto r = InstantiateScene(snap, scene, tm, reg);

    EXPECT_TRUE(r.ok);
    EXPECT_EQ(scene.GetObjectCount(), 1u);
    ASSERT_EQ(r.warnings.size(), 1u);
    EXPECT_NE(r.warnings[0].find("missing"), std::string::npos);
    Scripting::GameplayAPI::Reset();
}
// === Dogfood-03: Dodge & Collect (13 entities, timer, chaser AI) ===

TEST(ContentDogfood, DF03_Smoke_LoadRunTimerExpire) {
    ContentRegistry reg;
    ASSERT_TRUE(reg.LoadManifest("assets/scenes/dogfood03.manifest.json"));

    SceneSnapshot snap; std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile("assets/scenes/dogfood03.scene", snap, err)) << err;
    ASSERT_EQ(snap.entities.size(), 13u);

    Engine::Scene scene;
    OpenGLGraphicsFactory gfxF;
    Engine::TextureManager tm(gfxF);
    Scripting::GameplayAPI::Reset();
    Scripting::GameplayAPI::SetScene(&scene);
    auto r = InstantiateScene(snap, scene, tm, reg);
    ASSERT_TRUE(r.ok);

    uint32_t eh = 1;
    for (auto& o : scene.GetObjects()) {
        auto h = Scripting::GameplayAPI::HandleAdopt(o);
        ASSERT_EQ(h, eh); ++eh;
    }

    std::string scriptPath;
    for (size_t i = 0; i < snap.entities.size(); ++i)
        if (snap.entities[i].name == "Player")
            scriptPath = reg.ResolvePath(r.bindings[i].scriptGuid);
    ASSERT_FALSE(scriptPath.empty());

    ScriptInstance inst;
    ScriptInstance::Config cfg; cfg.instructionBudget = 5000000;
    ASSERT_TRUE(inst.Initialize(scriptPath, cfg));
    inst.OnCreate();

    // Simulate 35 seconds (2100 frames at dt=1/60) with NO input
    // Timer should expire at ~30s -> gameState becomes "lost"
    for (int f = 0; f < 2100; ++f) {
        inst.OnUpdate(1.0f / 60.0f);
        if (!inst.IsValid()) {
            std::printf("    [DBG] instance invalid at frame %d\n", f); break;
        }
        if (f % 600 == 599) {
            std::printf("    [DBG] f=%d err='%s'\n", f+1,
                inst.GetEngine()->GetLastError().message.c_str());
            fflush(stdout);
        }
    }

    // After 35s without input: chasers catch player (~2s) OR timer expires (30s)
    // Either way, gameState MUST be "lost"
    bool lostCheck = inst.GetEngine()->RunString(
        "assert(_PERSIST.gameState == 'lost', 'state=' .. tostring(_PERSIST.gameState))");
    EXPECT_TRUE(lostCheck) << "game should be in lost state";

    auto* veng2 = inst.GetEngine();
    veng2->RunString("g_tc = (_PERSIST.timer <= 0.01)");
    lua_getglobal(veng2->GetState(), "g_tc");
    bool timerExpired = lua_toboolean(veng2->GetState(), -1) != 0;
    lua_pop(veng2->GetState(), 1);
    // Timer may or may not expire first (chaser might catch first)
    // But gameState must be "lost" either way
}

TEST(ContentDogfood, DF03_EntityCount_MultiScript) {
    ContentRegistry reg;
    ASSERT_TRUE(reg.LoadManifest("assets/scenes/dogfood03.manifest.json"));
    SceneSnapshot snap; std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile("assets/scenes/dogfood03.scene", snap, err)) << err;

    Engine::Scene scene;
    OpenGLGraphicsFactory gfxF;
    Engine::TextureManager tm(gfxF);
    Scripting::GameplayAPI::Reset();
    Scripting::GameplayAPI::SetScene(&scene);
    auto r = InstantiateScene(snap, scene, tm, reg);
    ASSERT_TRUE(r.ok);

    uint32_t eh = 1;
    for (auto& o : scene.GetObjects()) {
        auto h = Scripting::GameplayAPI::HandleAdopt(o);
        ASSERT_EQ(h, eh); ++eh;
    }
    EXPECT_EQ(Scripting::GameplayAPI::HandleCount(), 13u);
}
// ── M3-B: Asset Assignment Contract (headless) ──
TEST(EditorWorkflow, M3B_BrowserAssign_SyncsBinding) {
    ContentRegistry reg;
    auto texG = reg.Import("t.png", AssetType::Texture);
    auto scrG = reg.Import("s.lua", AssetType::Script);
    EXPECT_TRUE(reg.ContainsGuid(texG));
    EXPECT_FALSE(reg.ResolvePath(texG).empty());
}

// === Dogfood-05: Arena Survival (13 entities, chaser AI, state machine) ===

