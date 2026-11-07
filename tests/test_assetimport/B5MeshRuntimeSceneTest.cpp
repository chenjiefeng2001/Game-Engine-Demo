#include "../test_debugdraw/DebugDrawGLFixture.h"
#include "TestSupport.h"
#include "../test_content/StubGraphicsFactory.h"

#include "AssetImport/MeshRuntimeLoader.h"
#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/MeshAssetResolver.h"
#include "Engine/Core/Content/ProjectPaths.h"
#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/Content/RevisionStore.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/RHI/MeshRenderer.h"
#include "Engine/Core/RHI/MeshRuntimeScene.h"
#include "Engine/Core/RHI/TextureGpuResource.h"
#include "Engine/Core/Scene/Scene.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace Engine;
using namespace Engine::Content;
using AssetImportTest::Json;

namespace fs = std::filesystem;

class StubRenderContext : public IRenderContext {
public:
    void Init() override {}
    void ClearColor(float, float, float, float) override {}
    void SwapBuffers() override {}
    void DrawIndexed(const std::shared_ptr<VertexArray>&) override {}
    void DrawIndexed_RHI(const std::shared_ptr<RHI::IRHIVertexArray>&) override {}
    void OnResize(int32, int32) override {}
};

std::string MeshJson(const std::string& materialName, float x) {
    Json v0 = {{"position", {x, 0.0f, 0.0f}},
               {"normal", {0.0f, 0.0f, 1.0f}},
               {"uv0", {0.0f, 0.0f}},
               {"tangent", {1.0f, 0.0f, 0.0f}},
               {"flags", 7}};
    Json v1 = {{"position", {1.0f, 0.0f, 0.0f}},
               {"normal", {0.0f, 0.0f, 1.0f}},
               {"uv0", {1.0f, 0.0f}},
               {"tangent", {1.0f, 0.0f, 0.0f}},
               {"flags", 7}};
    Json v2 = {{"position", {0.0f, 1.0f, 0.0f}},
               {"normal", {0.0f, 0.0f, 1.0f}},
               {"uv0", {0.0f, 1.0f}},
               {"tangent", {1.0f, 0.0f, 0.0f}},
               {"flags", 7}};
    Json transform = Json::array(
        {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
    Json prim = {{"firstIndex", 0},
                 {"indexCount", 3},
                 {"materialIndex", materialName.empty() ? Json() : Json(0)},
                 {"nodeName", "Root"},
                 {"sourceTransform", transform}};
    Json doc = {{"format", "mesh"},
                {"formatVersion", 1},
                {"sourceFile", "tri.obj"},
                {"vertices", {v0, v1, v2}},
                {"indices", {0, 1, 2}},
                {"primitives", {prim}},
                {"materialNames",
                 materialName.empty() ? Json::array() : Json::array({materialName})},
                {"imageUris", Json::array()}};
    return doc.dump();
}

std::string MaterialJson(const std::string& texturePath) {
    auto texRef = [&](const std::string& p) {
        if (p.empty())
            return Json::object({{"present", false}, {"imageIndex", 4294967295u}});
        return Json::object(
            {{"present", true}, {"imageIndex", 0}, {"assetPath", p}});
    };
    Json j = {{"format", "material"},
              {"formatVersion", 1},
              {"slot", 0},
              {"name", "MatA"},
              {"baseColorFactor", {0.2f, 0.4f, 0.6f, 1.0f}},
              {"metallicFactor", 0.0f},
              {"roughnessFactor", 1.0f},
              {"emissiveFactor", {0.0f, 0.0f, 0.0f}},
              {"alphaCutoff", 0.5f},
              {"alphaMode", "OPAQUE"},
              {"baseColorTexture", texRef(texturePath)},
              {"normalTexture", texRef("")},
              {"occlusionTexture", texRef("")},
              {"emissiveTexture", texRef("")},
              {"metallicRoughnessTexture", texRef("")}};
    return j.dump();
}

std::vector<uint8_t> MakeTinyPng2x2(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t crcTable[256];
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crcTable[n] = c;
    }
    auto crc32 = [&](const uint8_t* d, size_t n) {
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < n; ++i)
            c = crcTable[(c ^ d[i]) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    };
    auto push32BE = [&](std::vector<uint8_t>& v, uint32_t x) {
        v.push_back(x >> 24 & 0xFF);
        v.push_back(x >> 16 & 0xFF);
        v.push_back(x >> 8 & 0xFF);
        v.push_back(x & 0xFF);
    };
    auto appendChunk = [&](std::vector<uint8_t>& png, const char* type,
                           const std::vector<uint8_t>& data) {
        push32BE(png, static_cast<uint32_t>(data.size()));
        std::vector<uint8_t> crcIn;
        for (int k = 0; k < 4; ++k) crcIn.push_back(uint8_t(type[k]));
        crcIn.insert(crcIn.end(), data.begin(), data.end());
        for (int k = 0; k < 4; ++k) png.push_back(static_cast<uint8_t>(type[k]));
        png.insert(png.end(), data.begin(), data.end());
        push32BE(png, crc32(&crcIn[0], crcIn.size()));
    };
    std::vector<uint8_t> raw;
    for (uint32_t y = 0; y < 2; ++y) {
        raw.push_back(0);
        for (uint32_t x = 0; x < 2; ++x) {
            raw.push_back(r);
            raw.push_back(g);
            raw.push_back(b);
            raw.push_back(255);
        }
    }
    std::vector<uint8_t> idat;
    idat.push_back(0x78);
    idat.push_back(0x01);
    const uint16_t len = static_cast<uint16_t>(raw.size());
    idat.push_back(0x01);
    idat.push_back(len & 0xFF);
    idat.push_back(len >> 8);
    idat.push_back(~len & 0xFF);
    idat.push_back((~len >> 8) & 0xFF);
    idat.insert(idat.end(), raw.begin(), raw.end());
    uint32_t a1 = 1, a2 = 0;
    for (const uint8_t v : raw) {
        a1 = (a1 + v) % 65521u;
        a2 = (a2 + a1) % 65521u;
    }
    push32BE(idat, (a2 << 16) | a1);
    std::vector<uint8_t> png;
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    png.insert(png.end(), sig, sig + 8);
    std::vector<uint8_t> ihdr;
    push32BE(ihdr, 2);
    push32BE(ihdr, 2);
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    appendChunk(png, "IHDR", ihdr);
    appendChunk(png, "IDAT", idat);
    appendChunk(png, "IEND", {});
    return png;
}

std::string Sidecar() {
    Json j = {{"format", "texture-meta"},
              {"ruleVersion", 1},
              {"role", "basecolor"},
              {"colorspace", "sRGB"},
              {"width", 2},
              {"height", 2},
              {"components", 4},
              {"sourceIdentity", "test"}};
    return j.dump();
}

constexpr const char* kMeshPath = "assets/tri.89abcdef.mesh";
constexpr const char* kMeshPath2 = "assets/tri.abcdef12.mesh";
constexpr const char* kMatPath = "assets/tri.m0.11223344.material.json";
constexpr const char* kTexPath = "assets/tex/color.basecolor.55667788.tex.png";

struct Project {
    fs::path root;
    ProjectRoot project;
    RevisionStore store;
    ContentRegistry registry;
    ResourceGUID meshGuid;
    ResourceGUID matGuid;
    ResourceGUID texGuid;

    Project() {
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() /
               ("gd_g4_runtime_" + std::to_string(stamp));
        fs::create_directories(root);
        PathError err = PathError::None;
        std::string diag;
        if (!ProjectRoot::Create(root.string(), &project, &err, &diag))
            throw std::runtime_error("ProjectRoot: " + diag);
        (void)store.Open(project);
    }
    ~Project() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void Write(const std::string& rel, const std::string& bytes) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    void Write(const std::string& rel, const std::vector<uint8_t>& bytes) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    bool Publish() {
        const RevisionPublishResult r = store.Publish(
            project, registry.SerializeManifest(),
            R"({"format":"engine.scene","version":1,"entities":[]})");
        return r.ok && r.committed;
    }
    void BuildFull(const std::string& meshPath = kMeshPath,
                   const std::string& meshBytes = "") {
        Write(meshPath, meshBytes.empty() ? MeshJson("MatA", 0.0f) : meshBytes);
        Write(kTexPath, MakeTinyPng2x2(200, 30, 30));
        Write(std::string(kTexPath) + ".tex.meta.json", Sidecar());
        Write(kMatPath, MaterialJson(kTexPath));
        registry = ContentRegistry();
        meshGuid = registry.Import(meshPath, AssetType::Mesh);
        texGuid = registry.Import(kTexPath, AssetType::Texture);
        matGuid = registry.Import(kMatPath, AssetType::Material);
    }
    void PublishMeshOnly(const std::string& meshPath, const std::string& bytes,
                         const ResourceGUID& guid) {
        Write(meshPath, bytes);
        ContentRegistry next;
        ASSERT_TRUE(next.RegisterExplicit(guid, meshPath, AssetType::Mesh));
        ASSERT_TRUE(next.RegisterExplicit(texGuid, kTexPath, AssetType::Texture));
        ASSERT_TRUE(next.RegisterExplicit(matGuid, kMatPath,
                                          AssetType::Material));
        registry = std::move(next);
        ASSERT_TRUE(Publish());
    }
};

MeshComponent* AddMesh(Scene& scene, const char* name, const ResourceGUID& guid) {
    auto go = std::make_shared<GameObject>(name);
    auto* mc = go->AddComponent<MeshComponent>();
    mc->SetMeshGuid(guid);
    scene.AddObject(go);
    return mc;
}

struct RuntimeFixture {
    Rendering::DebugDraw::Test::DebugDrawGLFixture gl;
    StubRenderContext context;
    TextureGpuCache textures;
    std::unique_ptr<MeshRenderer> renderer;
    std::unique_ptr<MeshAssetResolver> resolver;
    MeshRuntimeScene runtime;

    bool Setup(Project& project) {
        if (!gl.Setup()) return false;
        renderer = std::make_unique<MeshRenderer>(gl.Factory(), context);
        resolver = std::make_unique<MeshAssetResolver>(project.project,
                                                       project.store);
        runtime.SetDependencies(resolver.get(), renderer.get(), &textures,
                                &gl.Factory());
        return true;
    }
    ~RuntimeFixture() { runtime.Unload(); }
};

} // namespace

TEST(MeshRuntimeSceneGLTest, ReadyLoadsCpuGpuAndMaterialBinding) {
    Project project;
    project.BuildFull();
    ASSERT_TRUE(project.Publish());
    RuntimeFixture fx;
    ASSERT_TRUE(fx.Setup(project)) << fx.gl.FailStep();

    Scene scene;
    auto* mc = AddMesh(scene, "A", project.meshGuid);
    const MeshRuntimeScene::SyncStats stats = fx.runtime.Sync(scene);
    EXPECT_EQ(stats.loaded, 1u);
    EXPECT_EQ(stats.components, 1u);
    ASSERT_EQ(mc->GetRuntimeStatus(), MeshRuntimeStatus::Ready)
        << mc->GetRuntimeError();
    ASSERT_TRUE(mc->HasMesh());
    EXPECT_EQ(mc->GetResolvedRevision(), 1);
    EXPECT_EQ(mc->GetResolvedAssetPath(), kMeshPath);
    EXPECT_EQ(fx.renderer->GpuMeshCountForTesting(), 1u);
    EXPECT_EQ(fx.textures.Size(), 1u);

    const MeshRuntimeScene::Entry* entry =
        fx.runtime.FindForTesting(project.meshGuid);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->key.identity, MeshComponent::FoldGuid(project.meshGuid));
    ASSERT_NE(entry->binding, nullptr);
    ASSERT_TRUE(entry->binding->slots[0].baseColor.gpu);
    EXPECT_TRUE(entry->binding->slots[0].baseColor.gpu->IsSrgb());
    EXPECT_EQ(fx.renderer->GetMeshMaterialBindingForTesting(entry->key),
              entry->binding);

    const MeshRuntimeScene::SyncStats again = fx.runtime.Sync(scene);
    EXPECT_EQ(again.reused, 1u);
    EXPECT_EQ(again.loaded, 0u);
    EXPECT_EQ(fx.renderer->GpuMeshCountForTesting(), 1u);
    EXPECT_EQ(fx.renderer->GetMeshMaterialBindingForTesting(entry->key),
              entry->binding);
}

TEST(MeshRuntimeSceneGLTest, MultiEntitySharesResourcesAndRemovalIsSafe) {
    Project project;
    project.BuildFull();
    ASSERT_TRUE(project.Publish());
    RuntimeFixture fx;
    ASSERT_TRUE(fx.Setup(project)) << fx.gl.FailStep();

    Scene scene;
    auto* a = AddMesh(scene, "A", project.meshGuid);
    auto* b = AddMesh(scene, "B", project.meshGuid);
    const MeshRuntimeScene::SyncStats first = fx.runtime.Sync(scene);
    EXPECT_EQ(first.loaded, 1u);
    EXPECT_EQ(first.components, 2u);
    EXPECT_EQ(fx.renderer->GpuMeshCountForTesting(), 1u);
    EXPECT_EQ(a->GetMesh(), b->GetMesh());

    const MeshRuntimeScene::Entry* entry =
        fx.runtime.FindForTesting(project.meshGuid);
    ASSERT_NE(entry, nullptr);
    const MeshGpuResourceKey key = entry->key;

    scene.RemoveObject(scene.GetObjects()[0].get());
    const MeshRuntimeScene::SyncStats afterOne = fx.runtime.Sync(scene);
    EXPECT_EQ(afterOne.removed, 0u);
    EXPECT_EQ(fx.runtime.TrackedCount(), 1u);
    EXPECT_EQ(fx.renderer->GpuMeshCountForTesting(), 1u);
    EXPECT_TRUE(b->IsRuntimeReady());

    b->GetOwner()->RemoveComponent<MeshComponent>();
    const MeshRuntimeScene::SyncStats afterAll = fx.runtime.Sync(scene);
    EXPECT_EQ(afterAll.removed, 1u);
    EXPECT_EQ(fx.runtime.TrackedCount(), 0u);
    EXPECT_EQ(fx.renderer->GpuMeshCountForTesting(), 0u);
    EXPECT_EQ(fx.renderer->GetMeshMaterialBindingForTesting(key), nullptr);
}

TEST(MeshRuntimeSceneGLTest, MissingThenResolvedByNewRevision) {
    Project project;
    project.BuildFull();
    ASSERT_TRUE(project.Publish());
    RuntimeFixture fx;
    ASSERT_TRUE(fx.Setup(project)) << fx.gl.FailStep();

    const ResourceGUID future =
        ResourceGUID::FromHex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    Scene scene;
    auto* mc = AddMesh(scene, "A", future);
    EXPECT_EQ(fx.runtime.Sync(scene).failed, 1u);
    EXPECT_EQ(mc->GetRuntimeStatus(), MeshRuntimeStatus::MissingAsset);
    EXPECT_FALSE(mc->HasMesh());

    project.Write(kMeshPath, MeshJson("", 0.0f));
    ContentRegistry next;
    ASSERT_TRUE(next.RegisterExplicit(future, kMeshPath, AssetType::Mesh));
    project.registry = std::move(next);
    ASSERT_TRUE(project.Publish());
    const MeshRuntimeScene::SyncStats stats = fx.runtime.Sync(scene);
    EXPECT_EQ(stats.loaded, 1u);
    EXPECT_EQ(mc->GetRuntimeStatus(), MeshRuntimeStatus::Ready);
    EXPECT_TRUE(mc->HasMesh());
    EXPECT_EQ(mc->GetResolvedRevision(), 2);
    EXPECT_EQ(fx.runtime.FindFailureForTesting(future), nullptr);
}

TEST(MeshRuntimeSceneGLTest, ReaderGpuAndBindingFailuresAreDistinct) {
    Project project;
    project.Write(kMeshPath, "{broken");
    project.registry = ContentRegistry();
    project.meshGuid = project.registry.Import(kMeshPath, AssetType::Mesh);
    ASSERT_TRUE(project.Publish());
    RuntimeFixture fx;
    ASSERT_TRUE(fx.Setup(project)) << fx.gl.FailStep();

    Scene scene;
    auto* mc = AddMesh(scene, "A", project.meshGuid);
    EXPECT_EQ(fx.runtime.Sync(scene).failed, 1u);
    EXPECT_EQ(mc->GetRuntimeStatus(), MeshRuntimeStatus::ReaderFailure);

    project.Write(kMeshPath, MeshJson("", 0.0f));
    ContentRegistry next;
    ASSERT_TRUE(next.RegisterExplicit(project.meshGuid, kMeshPath,
                                      AssetType::Mesh));
    project.registry = std::move(next);
    ASSERT_TRUE(project.Publish());
    MeshGpuCache::SetUploadFailureStageForTesting(
        MeshGpuUploadStage::AfterVertexArray);
    EXPECT_EQ(fx.runtime.Sync(scene).failed, 1u);
    EXPECT_EQ(mc->GetRuntimeStatus(), MeshRuntimeStatus::GPUUploadFailure);
    MeshGpuCache::SetUploadFailureStageForTesting(MeshGpuUploadStage::None);

    ASSERT_TRUE(project.Publish());
    EXPECT_EQ(fx.runtime.Sync(scene).loaded, 1u);
    EXPECT_EQ(mc->GetRuntimeStatus(), MeshRuntimeStatus::Ready);
    EXPECT_EQ(fx.renderer->GpuMeshCountForTesting(), 1u);
}

TEST(MeshRuntimeSceneGLTest, MissingMaterialAndTextureFailClosed) {
    Project project;
    project.BuildFull();
    project.Write(kMatPath, MaterialJson(kTexPath));
    project.registry = ContentRegistry();
    project.meshGuid = project.registry.Import(kMeshPath, AssetType::Mesh);
    project.matGuid = project.registry.Import(kMatPath, AssetType::Material);
    ASSERT_TRUE(project.Publish());
    RuntimeFixture fx;
    ASSERT_TRUE(fx.Setup(project)) << fx.gl.FailStep();

    Scene scene;
    auto* mc = AddMesh(scene, "A", project.meshGuid);
    EXPECT_EQ(fx.runtime.Sync(scene).failed, 1u);
    EXPECT_EQ(mc->GetRuntimeStatus(), MeshRuntimeStatus::TextureBindingFailure);
    EXPECT_FALSE(mc->HasMesh());

    MeshRuntimeScene r2;
    const ResourceGUID stableGuid = mc->GetMeshGuid();
    project.registry = ContentRegistry();
    ASSERT_TRUE(project.registry.RegisterExplicit(stableGuid, kMeshPath,
                                                  AssetType::Mesh));
    project.meshGuid = stableGuid;
    ASSERT_TRUE(project.Publish());
    r2.SetDependencies(fx.resolver.get(), fx.renderer.get(), &fx.textures,
                       &fx.gl.Factory());
    EXPECT_EQ(r2.Sync(scene).failed, 1u);
    EXPECT_EQ(mc->GetRuntimeStatus(),
              MeshRuntimeStatus::MaterialBindingFailure);
    EXPECT_FALSE(mc->HasMesh());
}

TEST(MeshRuntimeSceneGLTest, RevisionSwitchKeepsOldUntilNewIsReady) {
    Project project;
    project.BuildFull();
    ASSERT_TRUE(project.Publish());
    RuntimeFixture fx;
    ASSERT_TRUE(fx.Setup(project)) << fx.gl.FailStep();
    Scene scene;
    auto* mc = AddMesh(scene, "A", project.meshGuid);
    ASSERT_EQ(fx.runtime.Sync(scene).loaded, 1u);
    const MeshRuntimeScene::Entry* oldEntry =
        fx.runtime.FindForTesting(project.meshGuid);
    ASSERT_NE(oldEntry, nullptr);
    const MeshGpuResourceKey oldKey = oldEntry->key;
    const std::shared_ptr<Mesh> oldMesh = mc->GetMesh();

    project.Write(kMeshPath2, "{invalid");
    project.registry = ContentRegistry();
    ASSERT_TRUE(project.registry.RegisterExplicit(
        project.meshGuid, kMeshPath2, AssetType::Mesh));
    ASSERT_TRUE(project.registry.RegisterExplicit(
        project.texGuid, kTexPath, AssetType::Texture));
    ASSERT_TRUE(project.registry.RegisterExplicit(
        project.matGuid, kMatPath, AssetType::Material));
    ASSERT_TRUE(project.Publish());
    EXPECT_EQ(fx.runtime.Sync(scene).failed, 1u);
    EXPECT_EQ(mc->GetMesh(), oldMesh);
    EXPECT_EQ(fx.renderer->GpuMeshCountForTesting(), 1u);
    EXPECT_NE(fx.renderer->GetMeshMaterialBindingForTesting(oldKey), nullptr);

    project.PublishMeshOnly(kMeshPath2, MeshJson("MatA", 5.0f),
                            project.meshGuid);
    const MeshRuntimeScene::SyncStats stats = fx.runtime.Sync(scene);
    EXPECT_EQ(stats.loaded, 1u);
    ASSERT_EQ(mc->GetRuntimeStatus(), MeshRuntimeStatus::Ready)
        << mc->GetRuntimeError();
    EXPECT_NE(mc->GetMesh(), oldMesh);
    EXPECT_EQ(mc->GetResolvedRevision(), 3);
    EXPECT_EQ(fx.renderer->GpuMeshCountForTesting(), 1u);
    EXPECT_EQ(fx.renderer->GetMeshMaterialBindingForTesting(oldKey), nullptr);
}

TEST(MeshRuntimeSceneGLTest, TextureOnlyRevisionRebuildsBindingUnderSameKey) {
    Project project;
    project.BuildFull();
    ASSERT_TRUE(project.Publish());
    RuntimeFixture fx;
    ASSERT_TRUE(fx.Setup(project)) << fx.gl.FailStep();
    Scene scene;
    auto* mc = AddMesh(scene, "A", project.meshGuid);
    ASSERT_EQ(fx.runtime.Sync(scene).loaded, 1u);
    const MeshRuntimeScene::Entry* first =
        fx.runtime.FindForTesting(project.meshGuid);
    ASSERT_NE(first, nullptr);
    const MeshGpuResourceKey key = first->key;
    const auto firstBinding = first->binding;

    project.Write(kTexPath, MakeTinyPng2x2(10, 220, 10));
    ASSERT_TRUE(project.Publish());
    ASSERT_EQ(fx.runtime.Sync(scene).loaded, 1u);
    const MeshRuntimeScene::Entry* second =
        fx.runtime.FindForTesting(project.meshGuid);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->key, key);
    EXPECT_NE(second->binding, firstBinding);
    EXPECT_EQ(second->revision, 2);
    EXPECT_EQ(mc->GetRuntimeStatus(), MeshRuntimeStatus::Ready);
    EXPECT_EQ(fx.renderer->GpuMeshCountForTesting(), 1u);
}

TEST(MeshRuntimeSceneGLTest, GuidChangeAndUnloadReleaseResources) {
    Project project;
    project.BuildFull();
    ASSERT_TRUE(project.Publish());
    RuntimeFixture fx;
    ASSERT_TRUE(fx.Setup(project)) << fx.gl.FailStep();
    Scene scene;
    auto* mc = AddMesh(scene, "A", project.meshGuid);
    ASSERT_EQ(fx.runtime.Sync(scene).loaded, 1u);
    const MeshGpuResourceKey oldKey =
        fx.runtime.FindForTesting(project.meshGuid)->key;

    const ResourceGUID other =
        ResourceGUID::FromHex("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    project.Write("assets/tri.ffeeddcc.mesh", MeshJson("", 3.0f));
    ContentRegistry next;
    ASSERT_TRUE(next.RegisterExplicit(other, "assets/tri.ffeeddcc.mesh",
                                      AssetType::Mesh));
    project.registry = std::move(next);
    ASSERT_TRUE(project.Publish());
    mc->SetMeshGuid(other);
    const MeshRuntimeScene::SyncStats switched = fx.runtime.Sync(scene);
    EXPECT_EQ(switched.removed, 1u);
    EXPECT_EQ(switched.loaded, 1u);
    EXPECT_EQ(fx.runtime.FindForTesting(project.meshGuid), nullptr);
    ASSERT_NE(fx.runtime.FindForTesting(other), nullptr);
    EXPECT_EQ(fx.renderer->GetMeshMaterialBindingForTesting(oldKey), nullptr);
    EXPECT_EQ(fx.renderer->GpuMeshCountForTesting(), 1u);

    fx.runtime.Unload();
    EXPECT_EQ(fx.runtime.TrackedCount(), 0u);
    EXPECT_EQ(fx.renderer->GpuMeshCountForTesting(), 0u);
    EXPECT_TRUE(fx.textures.Clear());
}

TEST(MeshRuntimeSceneGLTest, ColdSceneReloadResolvesCommittedCurrentAfterReimport) {
    Project project;
    project.BuildFull();
    ASSERT_TRUE(project.Publish());

    const fs::path scenePath = project.root / "mesh_reload.scene";
    {
        Scene scene;
        AddMesh(scene, "A", project.meshGuid);
        const SceneSnapshot snap = CaptureScene(scene, {});
        ASSERT_TRUE(SaveSnapshotToFile(snap, scenePath.string()));
    }

    SceneSnapshot loaded;
    std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile(scenePath.string(), loaded, err)) << err;
    auto coldScene = std::make_unique<Scene>();
    StubGraphicsFactory gfx;
    TextureManager texMgr(gfx);
    ContentRegistry registry;
    const LoadResult instantiated =
        InstantiateScene(loaded, *coldScene, texMgr, registry, {});
    ASSERT_TRUE(instantiated.warnings.empty());
    auto* mc = coldScene->GetObjects()[0]->GetComponent<MeshComponent>();
    ASSERT_NE(mc, nullptr);
    EXPECT_EQ(mc->GetMeshGuid(), project.meshGuid);

    RuntimeFixture fx;
    ASSERT_TRUE(fx.Setup(project)) << fx.gl.FailStep();
    EXPECT_EQ(fx.runtime.Sync(*coldScene).loaded, 1u);
    ASSERT_EQ(mc->GetRuntimeStatus(), MeshRuntimeStatus::Ready)
        << mc->GetRuntimeError();
    EXPECT_EQ(mc->GetResolvedRevision(), 1);
    const std::shared_ptr<Mesh> firstMesh = mc->GetMesh();

    project.PublishMeshOnly(kMeshPath2, MeshJson("MatA", 6.0f),
                            project.meshGuid);
    EXPECT_EQ(fx.runtime.Sync(*coldScene).loaded, 1u);
    ASSERT_EQ(mc->GetRuntimeStatus(), MeshRuntimeStatus::Ready)
        << mc->GetRuntimeError();
    EXPECT_EQ(mc->GetResolvedRevision(), 2);
    EXPECT_EQ(mc->GetMeshGuid(), project.meshGuid);
    EXPECT_NE(mc->GetMesh(), firstMesh);

    SceneSnapshot reloadedAgain;
    ASSERT_TRUE(
        LoadSnapshotFromFile(scenePath.string(), reloadedAgain, err)) << err;
    auto secondScene = std::make_unique<Scene>();
    const LoadResult second = InstantiateScene(reloadedAgain, *secondScene,
                                               texMgr, registry, {});
    ASSERT_TRUE(second.warnings.empty());
    auto* mc2 = secondScene->GetObjects()[0]->GetComponent<MeshComponent>();
    ASSERT_NE(mc2, nullptr);
    EXPECT_EQ(mc2->GetMeshGuid(), project.meshGuid);
    const MeshRuntimeScene::SyncStats secondStats =
        fx.runtime.Sync(*secondScene);
    EXPECT_EQ(secondStats.reused, 1u);
    EXPECT_EQ(secondStats.loaded, 0u);
    EXPECT_EQ(mc2->GetRuntimeStatus(), MeshRuntimeStatus::Ready);
    EXPECT_EQ(mc2->GetResolvedRevision(), 2);
}

TEST(MeshRuntimeSceneGLTest, NonMeshGuidIsRejected) {
    Project project;
    project.BuildFull();
    ASSERT_TRUE(project.Publish());
    RuntimeFixture fx;
    ASSERT_TRUE(fx.Setup(project)) << fx.gl.FailStep();
    Scene scene;
    auto* mc = AddMesh(scene, "A", project.texGuid);
    EXPECT_EQ(fx.runtime.Sync(scene).failed, 1u);
    EXPECT_EQ(mc->GetRuntimeStatus(), MeshRuntimeStatus::UnsupportedAssetType);
    EXPECT_FALSE(mc->HasMesh());
    EXPECT_EQ(fx.renderer->GpuMeshCountForTesting(), 0u);
}

TEST(MeshRuntimeSceneGLTest, GuidChangeToMissingDropsOldMeshSafely) {
    Project project;
    project.BuildFull();
    ASSERT_TRUE(project.Publish());
    RuntimeFixture fx;
    ASSERT_TRUE(fx.Setup(project)) << fx.gl.FailStep();
    Scene scene;
    auto* mc = AddMesh(scene, "A", project.meshGuid);
    ASSERT_EQ(fx.runtime.Sync(scene).loaded, 1u);
    const MeshGpuResourceKey oldKey =
        fx.runtime.FindForTesting(project.meshGuid)->key;

    const ResourceGUID missing =
        ResourceGUID::FromHex("cccccccccccccccccccccccccccccccc");
    mc->SetMeshGuid(missing);
    const MeshRuntimeScene::SyncStats stats = fx.runtime.Sync(scene);
    EXPECT_EQ(stats.failed, 1u);
    EXPECT_EQ(stats.removed, 1u);
    EXPECT_EQ(mc->GetRuntimeStatus(), MeshRuntimeStatus::MissingAsset);
    EXPECT_FALSE(mc->HasMesh());
    EXPECT_EQ(fx.runtime.FindForTesting(project.meshGuid), nullptr);
    EXPECT_EQ(fx.renderer->GetMeshMaterialBindingForTesting(oldKey), nullptr);
    EXPECT_EQ(fx.renderer->GpuMeshCountForTesting(), 0u);
}
