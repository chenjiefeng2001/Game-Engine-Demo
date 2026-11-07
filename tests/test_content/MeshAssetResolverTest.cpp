#include <gtest/gtest.h>

#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/MeshAssetResolver.h"
#include "Engine/Core/Content/ProjectPaths.h"
#include "Engine/Core/Content/RevisionStore.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Engine;
using namespace Engine::Content;

namespace {

namespace fs = std::filesystem;

std::string MakeMeshJson(float x) {
    nlohmann::json v0 = {{"position", {x, 0.0f, 0.0f}},
                         {"normal", {0.0f, 0.0f, 1.0f}},
                         {"uv0", {0.0f, 0.0f}},
                         {"tangent", {1.0f, 0.0f, 0.0f}},
                         {"flags", 7}};
    nlohmann::json v1 = {{"position", {1.0f, 0.0f, 0.0f}},
                         {"normal", {0.0f, 0.0f, 1.0f}},
                         {"uv0", {1.0f, 0.0f}},
                         {"tangent", {1.0f, 0.0f, 0.0f}},
                         {"flags", 7}};
    nlohmann::json v2 = {{"position", {0.0f, 1.0f, 0.0f}},
                         {"normal", {0.0f, 0.0f, 1.0f}},
                         {"uv0", {0.0f, 1.0f}},
                         {"tangent", {1.0f, 0.0f, 0.0f}},
                         {"flags", 7}};
    nlohmann::json transform = nlohmann::json::array(
        {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
    nlohmann::json prim = {{"firstIndex", 0},
                           {"indexCount", 3},
                           {"materialIndex", nullptr},
                           {"nodeName", "Root"},
                           {"sourceTransform", transform}};
    nlohmann::json doc = {{"format", "mesh"},
                          {"formatVersion", 1},
                          {"sourceFile", "tri.obj"},
                          {"vertices", {v0, v1, v2}},
                          {"indices", {0, 1, 2}},
                          {"primitives", {prim}},
                          {"materialNames", nlohmann::json::array()},
                          {"imageUris", nlohmann::json::array()}};
    return doc.dump();
}

struct TempProject {
    fs::path root;
    ProjectRoot project;
    RevisionStore store;

    TempProject() {
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() /
               ("gd_g4_resolver_" + std::to_string(stamp));
        fs::create_directories(root / "assets" / "mesh");
        PathError err = PathError::None;
        std::string diag;
        const bool ok = ProjectRoot::Create(root.string(), &project, &err,
                                            &diag);
        if (!ok) throw std::runtime_error("ProjectRoot: " + diag);
        (void)store.Open(project);
    }
    ~TempProject() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void Write(const std::string& rel, const std::string& bytes) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    bool Publish(ContentRegistry& reg, const std::string& scene =
                                           "{\"format\":\"engine.scene\","
                                           "\"version\":1,\"entities\":[]}") {
        const RevisionPublishResult r =
            store.Publish(project, reg.SerializeManifest(), scene);
        return r.ok && r.committed;
    }
};

std::string Hex(const ResourceGUID& guid) { return guid.ToHex(); }

} // namespace

TEST(MeshAssetResolverTest, ResolvesCommittedCurrentMeshBytes) {
    TempProject p;
    p.Write("assets/mesh/tri.11112222.mesh", MakeMeshJson(0.0f));
    ContentRegistry reg;
    const ResourceGUID guid =
        reg.Import("assets/mesh/tri.11112222.mesh", AssetType::Mesh);
    ASSERT_TRUE(p.Publish(reg));

    MeshAssetResolver resolver(p.project, p.store);
    const MeshAssetResolveResult r = resolver.Resolve(guid);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.revision, 1);
    EXPECT_EQ(r.assetPath, "assets/mesh/tri.11112222.mesh");
    EXPECT_FALSE(r.bytes.empty());
    EXPECT_NE(std::string(r.bytes.begin(), r.bytes.end()).find("format"),
              std::string::npos);
}

TEST(MeshAssetResolverTest, MissingNonMeshAndUnversionedFailClosed) {
    TempProject p;
    ContentRegistry reg;
    const ResourceGUID tex = reg.Import("assets/a.png", AssetType::Texture);
    const ResourceGUID missing = ResourceGUID::Create();
    MeshAssetResolver resolver(p.project, p.store);
    EXPECT_EQ(resolver.Resolve(ResourceGUID::Null).status,
              MeshResolveStatus::InvalidGuid);

    p.Write("assets/a.png", "png");
    ASSERT_TRUE(p.Publish(reg));
    EXPECT_EQ(resolver.Resolve(tex).status,
              MeshResolveStatus::UnsupportedAssetType);
    EXPECT_EQ(resolver.Resolve(missing).status, MeshResolveStatus::MissingAsset);

    TempProject unversioned;
    ContentRegistry reg2;
    const ResourceGUID mesh =
        reg2.Import("assets/mesh/uncommitted.mesh", AssetType::Mesh);
    MeshAssetResolver unversionedResolver(unversioned.project,
                                          unversioned.store);
    EXPECT_EQ(unversionedResolver.Resolve(mesh).status,
              MeshResolveStatus::NotCommitted);
}

TEST(MeshAssetResolverTest, StagingAndFailedPublishNeverBecomeCurrent) {
    TempProject p;
    p.Write("assets/mesh/tri.11112222.mesh", MakeMeshJson(0.0f));
    ContentRegistry reg;
    const ResourceGUID guid =
        reg.Import("assets/mesh/tri.11112222.mesh", AssetType::Mesh);
    ASSERT_TRUE(p.Publish(reg));
    const MeshAssetResolveResult first = [&] {
        MeshAssetResolver r(p.project, p.store);
        return r.Resolve(guid);
    }();
    ASSERT_TRUE(first.ok);

    p.Write("assets/mesh/tri.33334444.mesh", MakeMeshJson(9.0f));
    fs::create_directories(p.root / ".engine-content" / "revisions" / "999");
    p.Write(".engine-content/revisions/999/manifest.json",
            R"({"version":1,"assets":[{"guid":")" + guid.ToHex() +
                R"(","path":"assets/mesh/tri.33334444.mesh","type":"Mesh"}]})");
    ContentRegistry changed;
    ASSERT_TRUE(changed.RegisterExplicit(guid,
                                         "assets/mesh/tri.33334444.mesh",
                                         AssetType::Mesh));
    bool published = false;
    try {
        SetRevisionFaultHookForTesting([](RevisionFaultPoint point) {
            if (point == RevisionFaultPoint::BeforeCurrentReplace)
                throw std::runtime_error("injected publish failure");
        });
        published = p.Publish(changed);
    } catch (...) {
        published = false;
    }
    SetRevisionFaultHookForTesting({});
    EXPECT_FALSE(published);
    EXPECT_EQ(p.store.Revision(), 1);

    MeshAssetResolver resolver(p.project, p.store);
    const MeshAssetResolveResult after = resolver.Resolve(guid);
    ASSERT_TRUE(after.ok) << after.error;
    EXPECT_EQ(after.revision, 1);
    EXPECT_EQ(after.assetPath, "assets/mesh/tri.11112222.mesh");
    EXPECT_EQ(after.bytes, first.bytes);
}

TEST(MeshAssetResolverTest, ReimportSameGuidResolvesNewCurrent) {
    TempProject p;
    p.Write("assets/mesh/tri.11112222.mesh", MakeMeshJson(0.0f));
    ContentRegistry reg;
    const ResourceGUID guid =
        reg.Import("assets/mesh/tri.11112222.mesh", AssetType::Mesh);
    ASSERT_TRUE(p.Publish(reg));
    MeshAssetResolver resolver(p.project, p.store);
    ASSERT_TRUE(resolver.Resolve(guid).ok);

    p.Write("assets/mesh/tri.33334444.mesh", MakeMeshJson(7.0f));
    ContentRegistry next;
    ASSERT_TRUE(next.RegisterExplicit(
        guid, "assets/mesh/tri.33334444.mesh", AssetType::Mesh));
    ASSERT_TRUE(p.Publish(next));
    const MeshAssetResolveResult r = resolver.Resolve(guid);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.revision, 2);
    EXPECT_EQ(r.assetPath, "assets/mesh/tri.33334444.mesh");
    const std::string text(r.bytes.begin(), r.bytes.end());
    EXPECT_NE(text.find("7"), std::string::npos);
}

TEST(MeshAssetResolverTest, MissingBytesAndPathTraversalAreRejected) {
    TempProject p;
    ContentRegistry reg;
    const ResourceGUID guid =
        reg.Import("assets/mesh/absent.11112222.mesh", AssetType::Mesh);
    ASSERT_TRUE(p.Publish(reg));
    MeshAssetResolver resolver(p.project, p.store);
    EXPECT_EQ(resolver.Resolve(guid).status,
              MeshResolveStatus::AssetBytesUnreadable);

    std::vector<uint8_t> bytes;
    EXPECT_FALSE(resolver.ReadProjectFile("../outside.txt", bytes));
    EXPECT_FALSE(resolver.ReadProjectFile("/absolute.txt", bytes));
    EXPECT_TRUE(resolver.AbsoluteProjectPath("../outside.txt").empty());

    const std::string committedManifest = p.store.CommittedManifestPath();
    ASSERT_FALSE(committedManifest.empty());
    fs::remove(committedManifest);
    MeshAssetResolver broken(p.project, p.store);
    EXPECT_EQ(broken.Resolve(guid).status,
              MeshResolveStatus::CommittedManifestInvalid);
}
