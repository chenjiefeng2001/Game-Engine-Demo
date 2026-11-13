#include <gtest/gtest.h>

#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/GameObject/ComponentRegistry_Go.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/MeshComponent.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/Scene/Scene.h"
#include "StubGraphicsFactory.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace Engine;
using namespace Engine::Content;

namespace {

namespace fs = std::filesystem;

const ResourceGUID kGuidA = ResourceGUID::FromHex("00112233445566778899aabbccddeeff");
const ResourceGUID kGuidB = ResourceGUID::FromHex("ffeeddccbbaa99887766554433221100");

struct Instantiated {
    std::unique_ptr<Scene> scene;
    LoadResult result;
};

Instantiated Instantiate(const SceneSnapshot& snap) {
    auto scene = std::make_unique<Scene>();
    StubGraphicsFactory gfx;
    TextureManager texMgr(gfx);
    ContentRegistry registry;
    LoadResult result = InstantiateScene(snap, *scene, texMgr, registry);
    return {std::move(scene), std::move(result)};
}

std::string TempScenePath() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / "gd_g4_scene";
    fs::create_directories(dir);
    return (dir / ("mesh_" + std::to_string(stamp) + ".scene")).string();
}

MeshComponent* AddMesh(Scene& scene, const char* name,
                       const ResourceGUID& guid) {
    auto go = std::make_shared<GameObject>(name);
    auto* mc = go->AddComponent<MeshComponent>();
    mc->SetMeshGuid(guid);
    scene.AddObject(go);
    return mc;
}

} // namespace

TEST(MeshComponentContractTest, RegisteredAndRoundTripsGuid) {
    EXPECT_TRUE(ComponentRegistryGo::IsRegistered("Mesh"));
    auto created = ComponentRegistryGo::Create("Mesh");
    ASSERT_NE(created, nullptr);
    auto* mc = static_cast<MeshComponent*>(created.get());
    EXPECT_EQ(std::string(mc->GetComponentTypeName()), "Mesh");
    EXPECT_FALSE(mc->HasMeshGuid());

    mc->SetMeshGuid(kGuidA);
    nlohmann::json data;
    mc->Serialize(data);
    EXPECT_EQ(data["meshGuid"], kGuidA.ToHex());
    EXPECT_FALSE(data.contains("status"));
    EXPECT_FALSE(data.contains("revision"));

    auto restored = ComponentRegistryGo::Create("Mesh");
    auto* rc = static_cast<MeshComponent*>(restored.get());
    EXPECT_TRUE(rc->Deserialize(data));
    EXPECT_EQ(rc->GetMeshGuid(), kGuidA);
}

TEST(MeshComponentContractTest, ReflectionPropertiesExposeGuidAndDiagnostics) {
    MeshComponent mc;
    ComponentPropertyValue value;
    ASSERT_EQ(mc.GetPropertyCount(), 10u);
    ComponentPropertyDesc desc{};
    ASSERT_TRUE(mc.GetPropertyDesc(0, &desc));
    EXPECT_EQ(std::string(desc.name), "meshGuid");
    EXPECT_EQ(desc.type, ComponentValueType::String);
    EXPECT_TRUE(desc.editable);

    mc.SetMeshGuid(kGuidA);
    ASSERT_TRUE(mc.GetPropertyValue(0, &value));
    EXPECT_EQ(value.stringValue, kGuidA.ToHex());
    ASSERT_TRUE(mc.GetPropertyValue(4, &value));
    EXPECT_EQ(value.stringValue, "NotRequested");
    ASSERT_TRUE(mc.GetPropertyValue(2, &value));
    EXPECT_EQ(value.intValue, 0);
    ASSERT_TRUE(mc.GetPropertyValue(3, &value));
    EXPECT_EQ(value.intValue, 0);

    ComponentPropertyValue write;
    write.type = ComponentValueType::String;
    write.stringValue = kGuidB.ToHex();
    EXPECT_TRUE(mc.SetPropertyValue(0, write));
    EXPECT_EQ(mc.GetMeshGuid(), kGuidB);
    write.stringValue = "not-a-guid";
    EXPECT_FALSE(mc.SetPropertyValue(0, write));

    ComponentPropertyValue vis;
    vis.type = ComponentValueType::Bool;
    vis.boolValue = false;
    EXPECT_TRUE(mc.SetPropertyValue(8, vis));
    EXPECT_FALSE(mc.m_Visible);
}

TEST(MeshComponentContractTest, RejectsMalformedGuidAndAcceptsExplicitNull) {
    MeshComponent mc;
    EXPECT_TRUE(mc.Deserialize(nlohmann::json::object()));
    EXPECT_TRUE(mc.GetMeshGuid().IsNull());

    EXPECT_FALSE(mc.Deserialize({{"meshGuid", "not-a-guid"}}));
    EXPECT_TRUE(mc.GetMeshGuid().IsNull());
    EXPECT_FALSE(mc.Deserialize({{"meshGuid", 12}}));
    EXPECT_TRUE(mc.GetMeshGuid().IsNull());

    mc.SetMeshGuid(kGuidB);
    EXPECT_TRUE(mc.Deserialize({{"meshGuid", "00000000000000000000000000000000"}}));
    EXPECT_TRUE(mc.GetMeshGuid().IsNull());
}

TEST(MeshComponentSerializationTest, CaptureSaveLoadColdStartKeepsGuid) {
    Scene scene;
    AddMesh(scene, "A", kGuidA);
    AddMesh(scene, "B", kGuidA);
    AddMesh(scene, "C", kGuidB);
    const SceneSnapshot captured = CaptureScene(scene, {});
    ASSERT_EQ(captured.entities.size(), 3u);
    ASSERT_EQ(captured.entities[0].components.size(), 1u);
    EXPECT_EQ(captured.entities[0].components[0].type, "Mesh");

    const std::string path = TempScenePath();
    ASSERT_TRUE(SaveSnapshotToFile(captured, path));
    SceneSnapshot loaded;
    std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile(path, loaded, err)) << err;
    std::error_code ec;
    fs::remove(path, ec);

    Instantiated out = Instantiate(loaded);
    ASSERT_EQ(out.scene->GetObjects().size(), 3u);
    auto* a = out.scene->GetObjects()[0]->GetComponent<MeshComponent>();
    auto* b = out.scene->GetObjects()[1]->GetComponent<MeshComponent>();
    auto* c = out.scene->GetObjects()[2]->GetComponent<MeshComponent>();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(a->GetMeshGuid(), kGuidA);
    EXPECT_EQ(b->GetMeshGuid(), kGuidA);
    EXPECT_EQ(c->GetMeshGuid(), kGuidB);
    EXPECT_EQ(a->GetRuntimeStatus(), MeshRuntimeStatus::NotRequested);
    EXPECT_FALSE(a->HasMesh());
    EXPECT_TRUE(out.result.warnings.empty());
}

TEST(MeshComponentSerializationTest, LegacySceneWithoutMeshComponentLoads) {
    SceneSnapshot snap;
    snap.entities.push_back(SerializedEntity{"legacy"});
    Instantiated out = Instantiate(snap);
    ASSERT_EQ(out.scene->GetObjects().size(), 1u);
    EXPECT_EQ(out.scene->GetObjects()[0]->GetComponent<MeshComponent>(), nullptr);
    EXPECT_TRUE(out.result.warnings.empty());
}

TEST(MeshComponentSerializationTest, InvalidGuidNeverCreatesHalfValidComponent) {
    SceneSnapshot snap;
    SerializedEntity e{"bad"};
    SerializedComponent comp;
    comp.type = "Mesh";
    comp.data = {{"meshGuid", "zzzz"}};
    e.components.push_back(comp);
    snap.entities.push_back(e);
    Instantiated out = Instantiate(snap);
    ASSERT_EQ(out.scene->GetObjects().size(), 1u);
    auto* mc = out.scene->GetObjects()[0]->GetComponent<MeshComponent>();
    ASSERT_NE(mc, nullptr);
    EXPECT_FALSE(mc->HasMeshGuid());
    EXPECT_EQ(mc->GetRuntimeStatus(), MeshRuntimeStatus::NotRequested);
    ASSERT_FALSE(out.result.warnings.empty());
    EXPECT_NE(out.result.warnings[0].find("DataBad"), std::string::npos);
}

TEST(MeshComponentSerializationTest, RepeatedSaveLoadIsStable) {
    Scene scene;
    AddMesh(scene, "A", kGuidA);
    const SceneSnapshot first = CaptureScene(scene, {});
    const std::string path = TempScenePath();
    ASSERT_TRUE(SaveSnapshotToFile(first, path));
    SceneSnapshot loaded;
    std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile(path, loaded, err)) << err;
    std::error_code ec;
    fs::remove(path, ec);

    Instantiated out = Instantiate(loaded);
    ASSERT_NE(out.scene, nullptr);
    const SceneSnapshot second = CaptureScene(*out.scene, {});
    EXPECT_EQ(SerializeSnapshot(first).dump(), SerializeSnapshot(second).dump());
}
