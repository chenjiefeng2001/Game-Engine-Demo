#include <gtest/gtest.h>

#include "EditorSession.h"
#include "editor_bridge/capi.h"

#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/PrefabSerialization.h"
#include "Engine/Core/Content/RevisionStore.h"
#include "Engine/Core/Content/AtomicFile.h"
#include <nlohmann/json.hpp>
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/MeshComponent.h"
#include "Engine/Core/Scene/Scene.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

class PrefabAssetSessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_Root = std::filesystem::temp_directory_path() /
                 ("engine_p8_g4_3_" + std::to_string(
                     static_cast<unsigned long long>(
                         std::chrono::steady_clock::now()
                             .time_since_epoch()
                             .count())));
        std::filesystem::create_directories(m_Root / "assets" / "gp01");
        Write(m_Root / "assets" / "gp01" / "manifest.json",
              "{\"version\":1,\"assets\":[]}");
        Write(m_Root / "assets" / "gp01" / "Main.scene",
              "{\"format\":\"engine.scene\",\"version\":1,\"entities\":[]}");
        ASSERT_TRUE(m_Session.OpenProjectRooted(
            m_Root.string(), "assets/gp01/manifest.json",
            "assets/gp01/Main.scene"));
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(m_Root, ec);
    }

    static void Write(const std::filesystem::path& path,
                      const std::string& text) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }

    static std::string Read(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
    }

    static int32_t FindPrefabIndex(const editor_bridge::EditorSession& session) {
        for (int32_t i = 0; i < session.GetAssetCount(); ++i) {
            if (session.GetAssetType(i) == 4) return i;
        }
        return -1;
    }

    static std::string AssetPath(const editor_bridge::EditorSession& session,
                                 int32_t index) {
        std::string out;
        session.GetAssetPath(index, &out);
        return out;
    }

    static std::string AssetGuid(const editor_bridge::EditorSession& session,
                                 int32_t index) {
        std::string out;
        session.GetAssetGuid(index, &out);
        return out;
    }

    editor_bridge::EditorSession m_Session;
    std::filesystem::path m_Root;
};

static std::string ReadAssetBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

static void WriteAssetBytes(const std::filesystem::path& path,
                            const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

static int64_t ReadCurrentRevision(const std::filesystem::path& root) {
    const auto text = ReadAssetBytes(root / ".engine-content" / "current");
    if (text.empty()) return -1;
    try {
        const auto j = nlohmann::json::parse(text);
        return j.value("revision", -1LL);
    } catch (...) {
        return -1;
    }
}
TEST_F(PrefabAssetSessionTest, PrefabPublishFaultPointsKeepOldCurrent) {
    for (const int point : {0, 1, 2, 5, 6, 7}) {
        editor_bridge::EditorSession session;
        ASSERT_TRUE(session.OpenProjectRooted(
            m_Root.string(), "assets/gp01/manifest.json",
            "assets/gp01/Main.scene"));
        const std::string suffix = std::to_string(point);
        const int32_t source = session.CreateEntity(("FaultRoot" + suffix).c_str());
        ASSERT_GE(source, 0);
        const uint64_t sourceId = session.GetEntitySceneId(source);
        ASSERT_NE(sourceId, 0u);
        ASSERT_GE(session.CreatePrefabFromEntity(sourceId, ("Base" + suffix).c_str()), 0)
            << session.GetLastError();
        const int32_t assetsBefore = session.GetAssetCount();
        const auto currentPath = m_Root / ".engine-content" / "current";
        const std::string baseline = ReadAssetBytes(currentPath);
        ASSERT_FALSE(baseline.empty());
        const int64_t revision = ReadCurrentRevision(m_Root);
        ASSERT_GT(revision, 0);

        struct Ctx {
            int point = 0;
            bool armed = false;
            std::string suffix;
            std::string baseline;
            std::filesystem::path current;
            std::filesystem::path candidateScene;
        } ctx;
        ctx.point = point;
        ctx.suffix = suffix;
        ctx.baseline = baseline;
        ctx.current = currentPath;
        ctx.candidateScene = m_Root / ".engine-content" / "revisions" /
                             std::to_string(revision + 1) / "scene.scene";

        Engine::Content::SetRevisionFaultHookForTesting(
            [&](Engine::Content::RevisionFaultPoint at) {
                if (static_cast<int>(at) != ctx.point) return;
                switch (ctx.point) {
                case 0: ctx.armed = true; ctx.suffix = "manifest.json"; break;
                case 1: ctx.armed = true; ctx.suffix = "scene.scene"; break;
                case 2: {
                    std::error_code ec;
                    std::filesystem::remove(ctx.candidateScene, ec);
                    break;
                }
                case 5:
                case 6: ctx.armed = true; ctx.suffix = "current"; break;
                case 7: WriteAssetBytes(ctx.current, ctx.baseline); break;
                default: break;
                }
            });
        Engine::Content::SetAtomicFaultHookForTesting(
            [&](Engine::Content::AtomicFaultPoint, const std::string&,
                const std::string& target) {
                if (!ctx.armed) return false;
                if (target.find(ctx.suffix) == std::string::npos) return false;
                ctx.armed = false;
                return true;
            });

        const int32_t result =
            session.CreatePrefabFromEntity(sourceId, ("Fault" + suffix).c_str());
        Engine::Content::SetRevisionFaultHookForTesting({});
        Engine::Content::SetAtomicFaultHookForTesting({});

        EXPECT_LT(result, 0) << "fault point " << point;
        EXPECT_EQ(session.GetAssetCount(), assetsBefore)
            << "fault point " << point;
        EXPECT_EQ(ReadAssetBytes(currentPath), baseline)
            << "fault point " << point;
    }
}

TEST_F(PrefabAssetSessionTest, FailedPublishLeavesOrphanCandidateNotInManifest) {
    const int32_t source = m_Session.CreateEntity("OrphanRoot");
    ASSERT_GE(source, 0);
    const uint64_t sourceId = m_Session.GetEntitySceneId(source);
    ASSERT_NE(sourceId, 0u);

    const auto prefabDir = m_Root / "assets" / "prefabs";
    const auto countPrefabs = [&]() {
        std::error_code ec;
        if (!std::filesystem::exists(prefabDir, ec) || ec) return size_t{0};
        size_t count = 0;
        for (const auto& item : std::filesystem::directory_iterator(prefabDir, ec))
            if (item.is_regular_file()) ++count;
        return count;
    };
    const size_t before = countPrefabs();
    bool armed = false;
    Engine::Content::SetRevisionFaultHookForTesting(
        [&](Engine::Content::RevisionFaultPoint at) {
            if (at == Engine::Content::RevisionFaultPoint::
                          BeforeCandidateManifestWrite)
                armed = true;
        });
    Engine::Content::SetAtomicFaultHookForTesting(
        [&](Engine::Content::AtomicFaultPoint, const std::string&,
            const std::string& target) {
            if (!armed || target.find("manifest.json") == std::string::npos)
                return false;
            armed = false;
            return true;
        });
    EXPECT_LT(m_Session.CreatePrefabFromEntity(sourceId, "Orphan"), 0);
    Engine::Content::SetRevisionFaultHookForTesting({});
    Engine::Content::SetAtomicFaultHookForTesting({});

    EXPECT_EQ(countPrefabs(), before + 1);
    EXPECT_TRUE(ReadAssetBytes(m_Root / ".engine-content" / "current").empty());
    editor_bridge::EditorSession reopened;
    ASSERT_TRUE(reopened.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json", "assets/gp01/Main.scene"));
    EXPECT_EQ(FindPrefabIndex(reopened), -1);
}
TEST_F(PrefabAssetSessionTest, PrefabMarkerFaultPointsOnFirstPublish) {
    for (const int point : {3, 4}) {
        const auto root = m_Root / ("marker_" + std::to_string(point));
        std::filesystem::create_directories(root / "assets" / "gp01");
        WriteAssetBytes(root / "assets" / "gp01" / "manifest.json",
                        "{\"version\":1,\"assets\":[]}");
        WriteAssetBytes(root / "assets" / "gp01" / "Main.scene",
                        "{\"format\":\"engine.scene\",\"version\":1,\"entities\":[]}");

        editor_bridge::EditorSession session;
        ASSERT_TRUE(session.OpenProjectRooted(
            root.string(), "assets/gp01/manifest.json", "assets/gp01/Main.scene"));
        const int32_t source = session.CreateEntity("MarkerRoot");
        const uint64_t sourceId = session.GetEntitySceneId(source);
        ASSERT_NE(sourceId, 0u);

        bool armed = false;
        Engine::Content::SetRevisionFaultHookForTesting(
            [&](Engine::Content::RevisionFaultPoint at) {
                if (static_cast<int>(at) == point) armed = true;
            });
        Engine::Content::SetAtomicFaultHookForTesting(
            [&](Engine::Content::AtomicFaultPoint, const std::string&,
                const std::string& target) {
                if (!armed || target.find("versioned") == std::string::npos)
                    return false;
                armed = false;
                return true;
            });
        const int32_t result =
            session.CreatePrefabFromEntity(sourceId, ("Marker" + std::to_string(point)).c_str());
        Engine::Content::SetRevisionFaultHookForTesting({});
        Engine::Content::SetAtomicFaultHookForTesting({});

        EXPECT_LT(result, 0) << "marker fault point " << point;
        EXPECT_EQ(session.GetAssetCount(), 0);
        EXPECT_TRUE(ReadAssetBytes(root / ".engine-content" / "current").empty());
    }
}
} // namespace

TEST_F(PrefabAssetSessionTest, CreatePublishesContentAddressedPrefabAndReadsIt) {
    const int32_t rootIndex = m_Session.CreateEntity("PrefabRoot");
    const int32_t childIndex = m_Session.CreateEntity("PrefabChild");
    ASSERT_GE(rootIndex, 0);
    ASSERT_GE(childIndex, 0);
    auto* scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const uint64_t childId = m_Session.GetEntitySceneId(childIndex);
    ASSERT_TRUE(m_Session.SetEntityParent(childId, rootId, 0));

    const Engine::ResourceGUID meshGuid =
        Engine::ResourceGUID::FromHex("0123456789abcdef0123456789abcdef");
    auto* mesh = scene->FindByEntityId(rootId)->AddComponent<Engine::MeshComponent>();
    ASSERT_NE(mesh, nullptr);
    mesh->SetMeshGuid(meshGuid);
    const int32_t beforeTotal = m_Session.GetTotalEntityCount();

    const int32_t prefabIndex = m_Session.CreatePrefabFromEntity(rootId, "MyPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    EXPECT_EQ(m_Session.GetAssetType(prefabIndex), 4);
    EXPECT_EQ(m_Session.GetTotalEntityCount(), beforeTotal);
    EXPECT_NE(scene->FindByEntityId(rootId), nullptr);
    EXPECT_NE(scene->FindByEntityId(childId), nullptr);

    const std::string path = AssetPath(m_Session, prefabIndex);
    EXPECT_EQ(path.rfind("assets/prefabs/MyPrefab.", 0), 0u);
    EXPECT_NE(path.find(".prefab.json"), std::string::npos);
    const std::string guidText = AssetGuid(m_Session, prefabIndex);
    EXPECT_EQ(guidText.size(), 32u);

    const std::string bytes = Read(m_Root / path);
    ASSERT_FALSE(bytes.empty());
    const Engine::ResourceGUID prefabGuid = Engine::ResourceGUID::FromHex(guidText);
    const auto prefab = Engine::Content::ReadPrefab(bytes, &prefabGuid);
    ASSERT_TRUE(prefab.ok) << prefab.error;
    ASSERT_EQ(prefab.model.entities.size(), 2u);
    EXPECT_EQ(prefab.model.rootLocalId, 1u);
    EXPECT_EQ(prefab.model.entities[0].name, "PrefabRoot");
    EXPECT_EQ(prefab.model.entities[1].name, "PrefabChild");
    ASSERT_TRUE(prefab.model.entities[1].parentLocalId.has_value());
    EXPECT_EQ(*prefab.model.entities[1].parentLocalId, 1u);
    ASSERT_EQ(prefab.model.entities[0].components.size(), 1u);
    EXPECT_EQ(prefab.model.entities[0].components[0].type, "Mesh");
    EXPECT_EQ(prefab.model.entities[0].components[0].data["meshGuid"],
              meshGuid.ToHex());

    std::string dependencies;
    ASSERT_TRUE(m_Session.GetAssetDependencies(prefabIndex, &dependencies));
    const auto deps = nlohmann::json::parse(dependencies);
    ASSERT_TRUE(deps["dependencies"].is_array());
    bool foundMesh = false;
    for (const auto& dep : deps["dependencies"]) {
        if (dep.value("type", "") == "Mesh" &&
            dep.value("guid", "") == meshGuid.ToHex()) {
            foundMesh = true;
        }
    }
    EXPECT_TRUE(foundMesh);
}

TEST_F(PrefabAssetSessionTest, CAbiCreatePrefabRegistersAsset) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    EditorSessionHandle handle = EditorSession_Create();
    ASSERT_NE(handle, nullptr);
    ASSERT_EQ(EditorSession_OpenProjectRooted(
                  handle, m_Root.string().c_str(),
                  "assets/gp01/manifest.json", "assets/gp01/Main.scene"), 1);
    const int32_t entity = EditorSession_CreateEntity(handle, "CAbiRoot");
    ASSERT_GE(entity, 0);
    const uint64_t entityId = EditorSession_GetEntitySceneId(handle, entity);
    ASSERT_NE(entityId, 0u);
    const int32_t before = EditorSession_GetAssetCount(handle);
    const int32_t asset = EditorSession_CreatePrefabFromEntity(
        handle, entityId, "CAbiPrefab");
    ASSERT_GE(asset, 0);
    EXPECT_EQ(EditorSession_GetAssetCount(handle), before + 1);
    EXPECT_EQ(EditorSession_GetAssetType(handle, asset), 4);
    char path[512] = {};
    ASSERT_GT(EditorSession_GetAssetPath(handle, asset, path, sizeof(path)), 0);
    EXPECT_NE(std::strstr(path, "assets/prefabs/CAbiPrefab."), nullptr);
    char guid[64] = {};
    ASSERT_GT(EditorSession_GetAssetGuid(handle, asset, guid, sizeof(guid)), 0);
    EXPECT_EQ(std::strlen(guid), 32u);
    uint64_t instanceRoot = 0;
    ASSERT_EQ(EditorSession_InstantiatePrefab(handle, asset, 0, &instanceRoot), 0);
    ASSERT_NE(instanceRoot, 0u);
    ASSERT_TRUE(EditorSession_SaveProject(handle));
    EXPECT_NE(EditorSession_GetAssetCount(handle), before);
    char instanceJson[4096] = {};
    ASSERT_GT(EditorSession_GetPrefabInstanceJson(handle, instanceRoot,
                                                         instanceJson,
                                                         sizeof(instanceJson)), 0);
    EXPECT_NE(std::strstr(instanceJson, "\"isInstance\":true"), nullptr);
    float pos[3] = {3.0f, 0.0f, 0.0f};
    float zero[3] = {0.0f, 0.0f, 0.0f};
    float one[3] = {1.0f, 1.0f, 1.0f};
    ASSERT_EQ(EditorSession_SetEntityTransformById(handle, instanceRoot, pos, zero, one), 0);
    char overrideJson[4096] = {};
    ASSERT_GT(EditorSession_GetPrefabInstanceJson(handle, instanceRoot,
                                                  overrideJson,
                                                  sizeof(overrideJson)), 0);
    EXPECT_NE(std::strstr(overrideJson, "\"field\":\"position.x\""), nullptr);
    ASSERT_EQ(EditorSession_RevertOverride(handle, instanceRoot, 1,
                                            "Transform", "position.x"), 0);
    char revertedJson[4096] = {};
    ASSERT_GT(EditorSession_GetPrefabInstanceJson(handle, instanceRoot,
                                                  revertedJson,
                                                  sizeof(revertedJson)), 0);
    EXPECT_EQ(std::strstr(revertedJson, "\"field\":\"position.x\""), nullptr);
    char pathBefore[512] = {};
    ASSERT_GT(EditorSession_GetAssetPath(handle, asset, pathBefore,
                                                sizeof(pathBefore)), 0);
    float applyPos[3] = {4.0f, 0.0f, 0.0f};
    ASSERT_EQ(EditorSession_SetEntityTransformById(handle, instanceRoot,
                                                 applyPos, zero, one), 0);
    ASSERT_EQ(EditorSession_ApplyPrefabOverrides(handle, instanceRoot), 0);
    char pathAfter[512] = {};
    ASSERT_GT(EditorSession_GetAssetPath(handle, asset, pathAfter,
                                                sizeof(pathAfter)), 0);
    EXPECT_NE(std::strcmp(pathBefore, pathAfter), 0);
    char appliedJson[4096] = {};
    ASSERT_GT(EditorSession_GetPrefabInstanceJson(handle, instanceRoot,
                                                  appliedJson,
                                                  sizeof(appliedJson)), 0);
    EXPECT_NE(std::strstr(appliedJson, "\"syncState\":\"clean\""), nullptr);
    EXPECT_EQ(std::strstr(appliedJson, "\"field\":\"position.x\""), nullptr);
    ASSERT_EQ(EditorSession_RevertPrefabInstance(handle, instanceRoot), -1);
    EditorSession_Destroy(handle);
}
TEST_F(PrefabAssetSessionTest, PrefabSurvivesColdReopenWithStableGuid) {
    const int32_t rootIndex = m_Session.CreateEntity("PersistentPrefab");
    const int32_t childIndex = m_Session.CreateEntity("PersistentChild");
    auto* scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const uint64_t childId = m_Session.GetEntitySceneId(childIndex);
    ASSERT_TRUE(m_Session.SetEntityParent(childId, rootId, 0));
    const int32_t index = m_Session.CreatePrefabFromEntity(rootId, "PersistentPrefab");
    ASSERT_GE(index, 0) << m_Session.GetLastError();
    const std::string guid = AssetGuid(m_Session, index);
    const std::string path = AssetPath(m_Session, index);
    ASSERT_FALSE(guid.empty());
    ASSERT_FALSE(path.empty());

    editor_bridge::EditorSession reopened;
    ASSERT_TRUE(reopened.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json",
        "assets/gp01/Main.scene"));
    const int32_t reopenedIndex = FindPrefabIndex(reopened);
    ASSERT_GE(reopenedIndex, 0);
    EXPECT_EQ(AssetGuid(reopened, reopenedIndex), guid);
    EXPECT_EQ(AssetPath(reopened, reopenedIndex), path);
    EXPECT_EQ(reopened.GetAssetType(reopenedIndex), 4);
    EXPECT_EQ(reopened.GetTotalEntityCount(), 0);
    const Engine::ResourceGUID expectedGuid = Engine::ResourceGUID::FromHex(guid);
    const auto prefab = Engine::Content::ReadPrefab(
        Read(m_Root / path), &expectedGuid);
    ASSERT_TRUE(prefab.ok) << prefab.error;
    ASSERT_EQ(prefab.model.entities.size(), 2u);
}

TEST_F(PrefabAssetSessionTest, CorruptAndMissingPrefabBytesAreNotReportedAsValid) {
    const int32_t rootIndex = m_Session.CreateEntity("CorruptRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    ASSERT_NE(rootId, 0u);
    const int32_t index = m_Session.CreatePrefabFromEntity(rootId, "CorruptPrefab");
    ASSERT_GE(index, 0) << m_Session.GetLastError();
    const std::string path = AssetPath(m_Session, index);
    const std::string guid = AssetGuid(m_Session, index);
    ASSERT_FALSE(path.empty());

    const auto absolute = m_Root / path;
    const std::string original = Read(absolute);
    ASSERT_FALSE(original.empty());
    Write(absolute, "{not valid prefab");
    const Engine::ResourceGUID expectedGuid = Engine::ResourceGUID::FromHex(guid);
    EXPECT_FALSE(Engine::Content::ReadPrefab(
        Read(absolute), &expectedGuid).ok);
    std::string dependencies;
    EXPECT_FALSE(m_Session.GetAssetDependencies(index, &dependencies));

    std::error_code ec;
    std::filesystem::remove(absolute, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(m_Session.GetAssetStatus(index), 1);
}
