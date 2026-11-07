#include "EngineHost.h"
#include "EditorSession.h"


#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/ProjectPaths.h"
#include "Engine/Core/Content/RevisionStore.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/MeshComponent.h"
#include "Engine/Core/Scene/Scene.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using namespace Engine;
using namespace Engine::Content;

std::string MeshJson() {
    nlohmann::json v = {{"position", {0.0f, 0.0f, 0.0f}},
                        {"normal", {0.0f, 0.0f, 1.0f}},
                        {"uv0", {0.0f, 0.0f}},
                        {"tangent", {1.0f, 0.0f, 0.0f}},
                        {"flags", 7}};
    nlohmann::json v1 = v;
    v1["position"] = {1.0f, 0.0f, 0.0f};
    nlohmann::json v2 = v;
    v2["position"] = {0.0f, 1.0f, 0.0f};
    nlohmann::json transform = nlohmann::json::array(
        {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
    nlohmann::json doc = {
        {"format", "mesh"},
        {"formatVersion", 1},
        {"sourceFile", "host_tri.obj"},
        {"vertices", {v, v1, v2}},
        {"indices", {0, 1, 2}},
        {"primitives",
         nlohmann::json::array({{{"firstIndex", 0},
                                 {"indexCount", 3},
                                 {"materialIndex", nullptr},
                                 {"nodeName", "Root"},
                                 {"sourceTransform", transform}}})},
        {"materialNames", nlohmann::json::array()},
        {"imageUris", nlohmann::json::array()}};
    return doc.dump();
}

} // namespace

TEST(EngineHostMeshRuntimeTest, HostPumpsCommittedMeshThroughSharedRenderer) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);

    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
                          ("gd_g4_host_" + std::to_string(stamp));
    fs::create_directories(root / "assets");
    ProjectRoot project;
    PathError pathError = PathError::None;
    std::string pathDiagnostic;
    ASSERT_TRUE(ProjectRoot::Create(root.string(), &project, &pathError,
                                    &pathDiagnostic))
        << pathDiagnostic;

    const std::string meshPath = "assets/host_tri.89abcdef.mesh";
    {
        std::ofstream f(root / meshPath, std::ios::binary | std::ios::trunc);
        const std::string bytes = MeshJson();
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    ContentRegistry registry;
    const ResourceGUID guid = registry.Import(meshPath, AssetType::Mesh);
    const std::string manifestJson = registry.SerializeManifest();
    const std::string sceneJson =
        R"({"format":"engine.scene","version":1,"entities":[{"name":"MeshHost","position":[0,0,0],"components":[{"type":"Mesh","data":{"meshGuid":")" +
        guid.ToHex() + R"("}}]}]})";
    {
        std::ofstream f(root / "assets" / "manifest.json",
                        std::ios::binary | std::ios::trunc);
        f.write(manifestJson.data(),
                static_cast<std::streamsize>(manifestJson.size()));
        std::ofstream s(root / "assets" / "Main.scene",
                        std::ios::binary | std::ios::trunc);
        s.write(sceneJson.data(), static_cast<std::streamsize>(sceneJson.size()));
    }
    RevisionStore revisions;
    (void)revisions.Open(project);
    const RevisionPublishResult published =
        revisions.Publish(project, manifestJson, sceneJson);
    ASSERT_TRUE(published.ok && published.committed) << published.diagnostic;

    RuntimeResult result{};
    EngineHostHandle host = EngineHost_Create(&result);
    ASSERT_NE(host, nullptr);
    ASSERT_EQ(EngineHost_OpenProjectRooted(host, root.string().c_str(),
                                           "assets/manifest.json",
                                           "assets/Main.scene", &result),
              0);

    auto* engineHost = static_cast<editor_bridge::EngineHost*>(host);
    ASSERT_EQ(EngineHost_PumpOneFrame(host, 0.016f, &result), 0);
    int32_t status = -1;
    uint64_t identity = 0;
    uint64_t payloadDigest = 0;
    ASSERT_TRUE(engineHost->QueryRuntimeMeshForTesting(
        guid.ToHex().c_str(), &status, &identity, &payloadDigest))
        << "runtime mesh map has no entry";
    EXPECT_EQ(status, static_cast<int32_t>(MeshRuntimeStatus::Ready));
    EXPECT_EQ(identity, MeshComponent::FoldGuid(guid));
    EXPECT_NE(payloadDigest, 0u);

    EngineHost_SetProductionUseRenderGraph(host, 1);
    ASSERT_EQ(EngineHost_PumpOneFrame(host, 0.016f, &result), 0);
    int32_t rgStatus = -1;
    uint64_t rgIdentity = 0;
    uint64_t rgPayload = 0;
    ASSERT_TRUE(engineHost->QueryRuntimeMeshForTesting(
        guid.ToHex().c_str(), &rgStatus, &rgIdentity, &rgPayload));
    EXPECT_EQ(rgStatus, static_cast<int32_t>(MeshRuntimeStatus::Ready));
    EXPECT_EQ(rgIdentity, identity);
    EXPECT_EQ(rgPayload, payloadDigest);
    EngineHost_SetProductionUseRenderGraph(host, 0);

    ASSERT_EQ(EngineHost_Shutdown(host, &result), 0);
    EngineHost_Destroy(host);
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(EditorSessionMeshPersistenceTest, SaveProjectColdReopenRestoresMeshGuid) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
                          ("gd_g4_session_" + std::to_string(stamp));
    fs::create_directories(root / "assets");
    const std::string manifest =
        R"({"version":1,"assets":[]})";
    const std::string scene =
        R"({"format":"engine.scene","version":1,"entities":[]})";
    {
        std::ofstream m(root / "assets" / "manifest.json",
                        std::ios::binary | std::ios::trunc);
        m.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));
        std::ofstream s(root / "assets" / "Main.scene",
                        std::ios::binary | std::ios::trunc);
        s.write(scene.data(), static_cast<std::streamsize>(scene.size()));
    }
    const ResourceGUID guid =
        ResourceGUID::FromHex("00112233445566778899aabbccddeeff");

    EditorSessionHandle session = EditorSession_Create();
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(EditorSession_OpenProjectRooted(session, root.string().c_str(),
                                              "assets/manifest.json",
                                              "assets/Main.scene"),
              1);
    auto* native = static_cast<editor_bridge::EditorSession*>(session);
    ASSERT_NE(native->GetEditScene(), nullptr);
    {
        auto object = std::make_shared<GameObject>("SavedMesh");
        auto* mc = object->AddComponent<MeshComponent>();
        mc->SetMeshGuid(guid);
        native->GetEditScene()->AddObject(object);
    }
    ASSERT_EQ(EditorSession_SaveProject(session), 1);
    EditorSession_Destroy(session);

    session = EditorSession_Create();
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(EditorSession_OpenProjectRooted(session, root.string().c_str(),
                                              "assets/manifest.json",
                                              "assets/Main.scene"),
              1);
    native = static_cast<editor_bridge::EditorSession*>(session);
    ASSERT_NE(native->GetEditScene(), nullptr);
    ASSERT_EQ(native->GetEditScene()->GetObjects().size(), 1u);
    auto* restored =
        native->GetEditScene()->GetObjects()[0]->GetComponent<MeshComponent>();
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->GetMeshGuid(), guid);
    EXPECT_EQ(restored->GetRuntimeStatus(), MeshRuntimeStatus::NotRequested);
    EditorSession_Destroy(session);
    std::error_code ec;
    fs::remove_all(root, ec);
}
