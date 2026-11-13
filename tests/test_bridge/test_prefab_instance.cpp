#include <gtest/gtest.h>

#include "EditorSession.h"

#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/MeshComponent.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Content/PrefabApplyRecord.h"
#include "Engine/Core/Content/PrefabSerialization.h"
#include "Engine/Core/Content/PrefabReconciliationSources.h"
#include "Engine/Core/Content/PrefabReconciliation.h"
#include <nlohmann/json.hpp>
#include "Engine/Core/Content/AtomicFile.h"
#include "Engine/Core/Content/RevisionStore.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

class PrefabInstanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_Root = std::filesystem::temp_directory_path() /
                 ("engine_p8_g5_" + std::to_string(
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

    static std::string AssetGuid(const editor_bridge::EditorSession& session,
                                 int32_t index) {
        std::string out;
        session.GetAssetGuid(index, &out);
        return out;
    }

    static std::string AssetPath(const editor_bridge::EditorSession& session,
                                 int32_t index) {
        std::string out;
        session.GetAssetPath(index, &out);
        return out;
    }
    static int32_t FindPrefab(const editor_bridge::EditorSession& session) {
        for (int32_t i = 0; i < session.GetAssetCount(); ++i)
            if (session.GetAssetType(i) == 4) return i;
        return -1;
    }

    editor_bridge::EditorSession m_Session;
    std::filesystem::path m_Root;
};

} // namespace

TEST_F(PrefabInstanceTest, MultipleInstancesHaveIndependentIdsAndLocalMaps) {
    const int32_t rootIndex = m_Session.CreateEntity("InstanceRoot");
    const int32_t childIndex = m_Session.CreateEntity("InstanceChild");
    auto* scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const uint64_t childId = m_Session.GetEntitySceneId(childIndex);
    ASSERT_TRUE(m_Session.SetEntityParent(childId, rootId, 0));
    const Engine::ResourceGUID meshGuid =
        Engine::ResourceGUID::FromHex("0123456789abcdef0123456789abcdef");
    auto* mesh = scene->FindByEntityId(childId)->AddComponent<Engine::MeshComponent>();
    ASSERT_NE(mesh, nullptr);
    mesh->SetMeshGuid(meshGuid);

    const int32_t prefabIndex = m_Session.CreatePrefabFromEntity(rootId, "InstancePrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    const std::string prefabGuid = AssetGuid(m_Session, prefabIndex);

    uint64_t firstRoot = 0;
    uint64_t secondRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &firstRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &secondRoot))
        << m_Session.GetLastError();
    ASSERT_NE(firstRoot, 0u);
    ASSERT_NE(secondRoot, 0u);
    EXPECT_NE(firstRoot, secondRoot);
    EXPECT_EQ(m_Session.GetTotalEntityCount(), 6);

    Engine::PrefabInstanceMetadata first;
    Engine::PrefabInstanceMetadata second;
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(firstRoot, &first));
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(secondRoot, &second));
    EXPECT_EQ(first.prefabGuid.ToHex(), prefabGuid);
    EXPECT_EQ(second.prefabGuid.ToHex(), prefabGuid);
    ASSERT_EQ(first.localToScene.size(), 2u);
    ASSERT_EQ(second.localToScene.size(), 2u);
    EXPECT_EQ(first.localToScene.at(1), firstRoot);
    EXPECT_EQ(second.localToScene.at(1), secondRoot);
    EXPECT_NE(first.localToScene.at(2), second.localToScene.at(2));

    auto* firstChild = scene->FindByEntityId(first.localToScene.at(2));
    auto* secondChild = scene->FindByEntityId(second.localToScene.at(2));
    ASSERT_NE(firstChild, nullptr);
    ASSERT_NE(secondChild, nullptr);
    EXPECT_EQ(firstChild->GetParent()->GetSceneEntityId(), firstRoot);
    EXPECT_EQ(secondChild->GetParent()->GetSceneEntityId(), secondRoot);
    ASSERT_NE(firstChild->GetComponent<Engine::MeshComponent>(), nullptr);
    EXPECT_EQ(firstChild->GetComponent<Engine::MeshComponent>()->GetMeshGuid(),
              meshGuid);
    EXPECT_EQ(scene->GetParentEntityId(first.localToScene.at(2)), firstRoot);
}

TEST_F(PrefabInstanceTest, InstanceMetadataSurvivesSaveReopenAndRedo) {
    const int32_t rootIndex = m_Session.CreateEntity("PersistRoot");
    const int32_t childIndex = m_Session.CreateEntity("PersistChild");
    auto* scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const uint64_t childId = m_Session.GetEntitySceneId(childIndex);
    ASSERT_TRUE(m_Session.SetEntityParent(childId, rootId, 0));
    const int32_t prefabIndex = m_Session.CreatePrefabFromEntity(rootId, "PersistPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    const std::string prefabGuid = AssetGuid(m_Session, prefabIndex);

    uint64_t instanceRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &instanceRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.UndoSceneEdit());
    EXPECT_EQ(m_Session.GetTotalEntityCount(), 2);
    ASSERT_TRUE(m_Session.RedoSceneEdit());
    EXPECT_EQ(m_Session.GetTotalEntityCount(), 4);
    ASSERT_TRUE(m_Session.SaveProject());

    editor_bridge::EditorSession reopened;
    ASSERT_TRUE(reopened.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json",
        "assets/gp01/Main.scene"));
    ASSERT_EQ(reopened.GetTotalEntityCount(), 4);
    auto* reopenedScene = reopened.GetEditScene();
    ASSERT_NE(reopenedScene, nullptr);
    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(reopenedScene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    EXPECT_EQ(metadata.prefabGuid.ToHex(), prefabGuid);
    ASSERT_EQ(metadata.localToScene.size(), 2u);
    EXPECT_EQ(metadata.localToScene.at(1), instanceRoot);
    EXPECT_NE(reopenedScene->FindByEntityId(metadata.localToScene.at(2)), nullptr);
    EXPECT_EQ(reopenedScene->GetParentEntityId(metadata.localToScene.at(2)),
              instanceRoot);

    Engine::PrefabInstanceMetadata redone;
    ASSERT_TRUE(reopened.GetEditScene()->GetPrefabInstanceMetadata(
        instanceRoot, &redone));
    EXPECT_EQ(redone.localToScene.size(), 2u);
}

TEST_F(PrefabInstanceTest, OverrideMetadataSurvivesUndoRedoAndColdRestart) {
    const int32_t rootIndex = m_Session.CreateEntity("OverrideRoot");
    const int32_t childIndex = m_Session.CreateEntity("OverrideChild");
    auto* scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const uint64_t childId = m_Session.GetEntitySceneId(childIndex);
    ASSERT_TRUE(m_Session.SetEntityParent(childId, rootId, 0));
    const int32_t prefabIndex = m_Session.CreatePrefabFromEntity(rootId, "OverridePrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();

    uint64_t instanceRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &instanceRoot))
        << m_Session.GetLastError();

    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    Engine::Content::PrefabOverrideRecord record;
    record.localId = 1;
    record.component = "Transform";
    record.field = "position.x";
    record.value = 9.0;
    metadata.overrides.push_back(record);
    metadata.syncState = Engine::Content::PrefabSyncState::Overrides;
    ASSERT_TRUE(m_Session.BeginSceneEdit("override metadata"));
    std::string metadataError;
    ASSERT_TRUE(scene->SetPrefabInstanceMetadata(instanceRoot, metadata,
                                                 &metadataError)) << metadataError;
    ASSERT_TRUE(m_Session.CommitSceneEdit());

    ASSERT_TRUE(m_Session.UndoSceneEdit());
    Engine::PrefabInstanceMetadata undone;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        instanceRoot, &undone));
    EXPECT_TRUE(undone.overrides.empty());
    ASSERT_TRUE(m_Session.RedoSceneEdit());
    Engine::PrefabInstanceMetadata redone;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        instanceRoot, &redone));
    ASSERT_EQ(redone.overrides.size(), 1u);

    ASSERT_TRUE(m_Session.SaveProject());
    editor_bridge::EditorSession reopened;
    ASSERT_TRUE(reopened.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json", "assets/gp01/Main.scene"));
    Engine::PrefabInstanceMetadata restored;
    ASSERT_TRUE(reopened.GetEditScene()->GetPrefabInstanceMetadata(
        instanceRoot, &restored));
    ASSERT_EQ(restored.overrides.size(), 1u);
    EXPECT_EQ(restored.overrides[0].field, "position.x");
    EXPECT_EQ(restored.syncState, Engine::Content::PrefabSyncState::Overrides);
}
TEST_F(PrefabInstanceTest, RevertFailsClosedWhenSourceIsUnavailable) {
    const int32_t rootIndex = m_Session.CreateEntity("InvalidSourceRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex =
        m_Session.CreatePrefabFromEntity(rootId, "InvalidSourcePrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    uint64_t instanceRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &instanceRoot))
        << m_Session.GetLastError();
    float pos[3] = {3.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(instanceRoot, pos, zero, one));
    const std::string path = AssetPath(m_Session, prefabIndex);
    ASSERT_FALSE(path.empty());
    std::error_code ec;
    std::filesystem::remove(m_Root / path, ec);
    ASSERT_FALSE(ec);
    EXPECT_FALSE(m_Session.RevertPrefabOverride(instanceRoot, 1, "Transform",
                                                 "position.x"));
    EXPECT_FALSE(m_Session.RevertPrefabInstance(instanceRoot));
    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        instanceRoot, &metadata));
    EXPECT_FALSE(metadata.overrides.empty());
}
TEST_F(PrefabInstanceTest, InspectorWritesTrackAndClearOverrides) {
    const int32_t rootIndex = m_Session.CreateEntity("TrackRoot");
    const int32_t childIndex = m_Session.CreateEntity("TrackChild");
    auto* scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const uint64_t childId = m_Session.GetEntitySceneId(childIndex);
    ASSERT_TRUE(m_Session.SetEntityParent(childId, rootId, 0));
    const Engine::ResourceGUID baseMesh =
        Engine::ResourceGUID::FromHex("fedcba9876543210fedcba9876543210");
    auto* rootMesh =
        scene->FindByEntityId(rootId)->AddComponent<Engine::MeshComponent>();
    ASSERT_NE(rootMesh, nullptr);
    rootMesh->SetMeshGuid(baseMesh);
    const int32_t prefabIndex = m_Session.CreatePrefabFromEntity(rootId, "TrackPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();

    uint64_t instanceRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &instanceRoot))
        << m_Session.GetLastError();
    int32_t instanceIndex = -1;
    for (int32_t i = 0; i < m_Session.GetEntityCount(); ++i)
        if (m_Session.GetEntitySceneId(i) == instanceRoot) { instanceIndex = i; break; }
    ASSERT_GE(instanceIndex, 0);

    std::string instanceJson;
    ASSERT_TRUE(m_Session.GetPrefabInstanceJsonForEntity(instanceRoot, &instanceJson));
    EXPECT_NE(instanceJson.find("\"isInstance\":true"), std::string::npos)
        << instanceJson;
    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    EXPECT_TRUE(metadata.overrides.empty());
    EXPECT_EQ(metadata.syncState, Engine::Content::PrefabSyncState::Clean);

    float movedPos[3] = {5.f, 0.f, 0.f};
    float zeroRot[3] = {0.f, 0.f, 0.f};
    float oneScale[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(
        instanceRoot, movedPos, zeroRot, oneScale));
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    EXPECT_FALSE(metadata.overrides.empty());
    EXPECT_EQ(metadata.syncState, Engine::Content::PrefabSyncState::Overrides);

    ASSERT_TRUE(m_Session.UndoSceneEdit());
    scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    EXPECT_TRUE(metadata.overrides.empty());
    ASSERT_TRUE(m_Session.RedoSceneEdit());
    scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    EXPECT_FALSE(metadata.overrides.empty());

    float basePos[3] = {0.f, 0.f, 0.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(
        instanceRoot, basePos, zeroRot, oneScale));
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    EXPECT_TRUE(metadata.overrides.empty());
    EXPECT_EQ(metadata.syncState, Engine::Content::PrefabSyncState::Clean);

    const std::string meshGuid = "11111111111111111111111111111111";
    ASSERT_TRUE(m_Session.SetComponentProperty(instanceIndex, "Mesh", "meshGuid",
                                                meshGuid));
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    EXPECT_FALSE(metadata.overrides.empty());
    ASSERT_TRUE(m_Session.SaveProject());
    {
        editor_bridge::EditorSession reopened;
        ASSERT_TRUE(reopened.OpenProjectRooted(
            m_Root.string(), "assets/gp01/manifest.json",
            "assets/gp01/Main.scene"));
        Engine::PrefabInstanceMetadata persisted;
        ASSERT_TRUE(reopened.GetEditScene()->GetPrefabInstanceMetadata(
            instanceRoot, &persisted));
        EXPECT_FALSE(persisted.overrides.empty());
    }
    ASSERT_TRUE(m_Session.SetComponentProperty(instanceIndex, "Mesh", "meshGuid",
                                                baseMesh.ToHex()));
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    EXPECT_TRUE(metadata.overrides.empty());
    EXPECT_TRUE(metadata.overrides.empty());
    EXPECT_FALSE(m_Session.SetComponentProperty(instanceIndex, "Mesh", "visible",
                                                  "false"));
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    EXPECT_TRUE(metadata.overrides.empty());
}
TEST_F(PrefabInstanceTest, ApplyCandidateValidatesExactSourceAndLegacyIdentity) {
    const int32_t rootIndex = m_Session.CreateEntity("ApplyCandidateRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    auto* scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    const int32_t prefabIndex =
        m_Session.CreatePrefabFromEntity(rootId, "ApplyCandidatePrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    uint64_t instanceRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &instanceRoot))
        << m_Session.GetLastError();
    float pos[3] = {3.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(instanceRoot, pos, zero, one));

    Engine::Content::PrefabApplyCandidateResult candidate;
    ASSERT_TRUE(m_Session.BuildPrefabApplyCandidateForInstance(
        instanceRoot, &candidate)) << candidate.error;
    EXPECT_TRUE(candidate.ok);
    EXPECT_FALSE(candidate.bytes.empty());
    EXPECT_FALSE(candidate.digest.empty());
    EXPECT_EQ(candidate.model.entities[0].localTransform.position.x, 3.f);
    EXPECT_EQ(candidate.model.entities[0].localId, 1u);
    EXPECT_EQ(candidate.model.entities[0].name, "ApplyCandidateRoot");
    EXPECT_EQ(candidate.model.prefabGuid, Engine::ResourceGUID::FromHex(
        AssetGuid(m_Session, prefabIndex)));

    const std::string path = AssetPath(m_Session, prefabIndex);
    ASSERT_FALSE(path.empty());
    std::ofstream out(m_Root / path, std::ios::binary | std::ios::app);
    out << "\n";
    out.close();
    Engine::Content::PrefabApplyCandidateResult corrupted;
    EXPECT_FALSE(m_Session.BuildPrefabApplyCandidateForInstance(
        instanceRoot, &corrupted));
    EXPECT_FALSE(corrupted.ok);
    EXPECT_TRUE(corrupted.bytes.empty());

    const int32_t legacyIndex = m_Session.CreateEntity("LegacyApplyRoot");
    const uint64_t legacyId = m_Session.GetEntitySceneId(legacyIndex);
    Engine::PrefabInstanceMetadata legacy;
    legacy.prefabGuid = Engine::ResourceGUID::FromHex(
        "0123456789abcdef0123456789abcdef");
    legacy.sourceRevision = 1;
    legacy.legacySourceIdentity = true;
    legacy.localToScene[1] = legacyId;
    std::string metadataError;
    ASSERT_TRUE(m_Session.GetEditScene()->SetPrefabInstanceMetadata(
        legacyId, legacy, &metadataError)) << metadataError;
    Engine::Content::PrefabApplyCandidateResult legacyCandidate;
    EXPECT_FALSE(m_Session.BuildPrefabApplyCandidateForInstance(
        legacyId, &legacyCandidate));
    EXPECT_FALSE(legacyCandidate.ok);
    EXPECT_TRUE(legacyCandidate.bytes.empty());
}
TEST_F(PrefabInstanceTest, Phase1FaultPointsKeepOldCurrentOrExposeRecovery) {
    for (const int point : {0, 1, 2, 5, 6, 7}) {
        const auto root = m_Root / ("phase1_fault_" + std::to_string(point));
        std::filesystem::create_directories(root / "assets" / "gp01");
        Write(root / "assets" / "gp01" / "manifest.json",
              "{\"version\":1,\"assets\":[]}");
        Write(root / "assets" / "gp01" / "Main.scene",
              "{\"format\":\"engine.scene\",\"version\":1,\"entities\":[]}");
        editor_bridge::EditorSession session;
        ASSERT_TRUE(session.OpenProjectRooted(
            root.string(), "assets/gp01/manifest.json", "assets/gp01/Main.scene"));
        int32_t entity = session.CreateEntity("FaultRoot");
        ASSERT_GE(entity, 0);
        uint64_t entityId = session.GetEntitySceneId(entity);
        int32_t prefab = session.CreatePrefabFromEntity(entityId, "FaultPrefab");
        ASSERT_GE(prefab, 0) << session.GetLastError();
        uint64_t instanceRoot = 0;
        ASSERT_TRUE(session.InstantiatePrefab(prefab, 0, &instanceRoot))
            << session.GetLastError();
        float pos[3] = {3.f, 0.f, 0.f};
        float zero[3] = {0.f, 0.f, 0.f};
        float one[3] = {1.f, 1.f, 1.f};
        ASSERT_TRUE(session.SetEntityTransformById(instanceRoot, pos, zero, one));

        const auto currentPath = root / ".engine-content" / "current";
        std::string baseline;
        {
            std::ifstream in(currentPath, std::ios::binary);
            baseline.assign(std::istreambuf_iterator<char>(in),
                            std::istreambuf_iterator<char>());
        }
        ASSERT_FALSE(baseline.empty());
        const int64_t oldRevision = session.GetRevisionStoreForRuntime()->Revision();
        const auto prefabGuid = Engine::ResourceGUID::FromHex(
            AssetGuid(session, prefab));
        const auto recordPath = Engine::Content::PrefabApplyRecordPath(
            *session.GetProjectRootForRuntime(), prefabGuid, instanceRoot);
        ASSERT_FALSE(recordPath.empty());

        struct Ctx {
            int point = 0;
            bool armed = false;
            std::string targetSuffix;
            std::filesystem::path current;
            std::filesystem::path candidateScene;
            std::string baseline;
        } ctx;
        ctx.point = point;
        ctx.current = currentPath;
        ctx.baseline = baseline;
        ctx.candidateScene = root / ".engine-content" / "revisions" /
                             std::to_string(oldRevision + 1) / "scene.scene";
        Engine::Content::SetRevisionFaultHookForTesting(
            [&](Engine::Content::RevisionFaultPoint at) {
                if (static_cast<int>(at) != ctx.point) return;
                switch (ctx.point) {
                case 0: ctx.armed = true; ctx.targetSuffix = "manifest.json"; break;
                case 1: ctx.armed = true; ctx.targetSuffix = "scene.scene"; break;
                case 2: {
                    std::error_code ec;
                    std::filesystem::remove(ctx.candidateScene, ec);
                    break;
                }
                case 3:
                case 4: ctx.armed = true; ctx.targetSuffix = "versioned"; break;
                case 5:
                case 6: ctx.armed = true; ctx.targetSuffix = "current"; break;
                case 7: Write(ctx.current, ctx.baseline); break;
                }
            });
        Engine::Content::SetAtomicFaultHookForTesting(
            [&](Engine::Content::AtomicFaultPoint, const std::string&,
                const std::string& target) {
                if (!ctx.armed) return false;
                if (target.find(ctx.targetSuffix) == std::string::npos)
                    return false;
                ctx.armed = false;
                return true;
            });

        const bool published = session.PublishPrefabApplyPhase1(instanceRoot);
        Engine::Content::SetRevisionFaultHookForTesting({});
        Engine::Content::SetAtomicFaultHookForTesting({});
        SCOPED_TRACE(::testing::Message() << "phase1 fault point=" << point);

        if (point == 7) {
            EXPECT_FALSE(published);
            EXPECT_TRUE(session.IsRecoveryRequired());
            Engine::Content::PrefabApplyRecord record;
            std::string recordError;
            EXPECT_TRUE(Engine::Content::ReadPrefabApplyRecord(
                *session.GetProjectRootForRuntime(), prefabGuid, instanceRoot,
                &record, &recordError)) << recordError;
            EXPECT_EQ(record.state,
                      Engine::Content::PrefabApplyState::RecoveryRequired);
        } else {
            EXPECT_FALSE(published);
            EXPECT_FALSE(session.IsRecoveryRequired());
            EXPECT_EQ(session.GetRevisionStoreForRuntime()->Revision(),
                      oldRevision);
            std::ifstream current(currentPath, std::ios::binary);
            std::string currentBytes{std::istreambuf_iterator<char>(current),
                                     std::istreambuf_iterator<char>()};
            EXPECT_EQ(currentBytes, baseline);
            EXPECT_FALSE(std::filesystem::exists(recordPath));
        }
    }
}
TEST_F(PrefabInstanceTest, Phase1PublishesPrefabAndLeavesSceneMetadataOld) {
    const int32_t rootIndex = m_Session.CreateEntity("Phase1Root");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex = m_Session.CreatePrefabFromEntity(rootId, "Phase1Prefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    const std::string guid = AssetGuid(m_Session, prefabIndex);
    const std::string oldPath = AssetPath(m_Session, prefabIndex);
    ASSERT_FALSE(oldPath.empty());
    const int64_t oldRevision = m_Session.GetRevisionStoreForRuntime()->Revision();

    uint64_t instanceRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &instanceRoot))
        << m_Session.GetLastError();
    float pos[3] = {4.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(instanceRoot, pos, zero, one));

    ASSERT_TRUE(m_Session.PublishPrefabApplyPhase1(instanceRoot))
        << m_Session.GetLastError();
    const std::string newPath = AssetPath(m_Session, prefabIndex);
    ASSERT_FALSE(newPath.empty());
    EXPECT_NE(newPath, oldPath);
    EXPECT_TRUE(std::filesystem::exists(m_Root / oldPath));
    EXPECT_TRUE(std::filesystem::exists(m_Root / newPath));
    const int64_t phase1Revision = m_Session.GetRevisionStoreForRuntime()->Revision();
    EXPECT_GT(phase1Revision, oldRevision);

    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        instanceRoot, &metadata));
    EXPECT_EQ(metadata.sourcePath, oldPath);
    EXPECT_FALSE(metadata.overrides.empty());
    EXPECT_EQ(metadata.syncState, Engine::Content::PrefabSyncState::Overrides);

    const Engine::ResourceGUID expected =
        Engine::ResourceGUID::FromHex(guid);
    std::ifstream candidateFile(m_Root / newPath, std::ios::binary);
    ASSERT_TRUE(candidateFile.good());
    const std::string candidateBytes(
        (std::istreambuf_iterator<char>(candidateFile)),
        std::istreambuf_iterator<char>());
    const auto candidate = Engine::Content::ReadPrefab(candidateBytes, &expected);
    ASSERT_TRUE(candidate.ok) << candidate.error;
    ASSERT_FALSE(candidate.model.entities.empty());
    EXPECT_EQ(candidate.model.entities[0].localTransform.position.x, 4.f);

    Engine::Content::PrefabApplyRecord record;
    std::string recordError;
    ASSERT_TRUE(Engine::Content::ReadPrefabApplyRecord(
        *m_Session.GetProjectRootForRuntime(), expected, instanceRoot,
        &record, &recordError)) << recordError;
    EXPECT_EQ(record.state, Engine::Content::PrefabApplyState::PrefabPublished);
    EXPECT_EQ(record.phase1Revision, phase1Revision);
    EXPECT_EQ(record.newSourceRevision, phase1Revision);
    EXPECT_EQ(record.newPath, newPath);
    EXPECT_EQ(record.oldSourcePath, oldPath);

    EXPECT_FALSE(m_Session.PublishPrefabApplyPhase1(instanceRoot));
}
TEST_F(PrefabInstanceTest, Phase2UpdatesInstanceMetadataAndClearsOverrides) {
    const int32_t rootIndex = m_Session.CreateEntity("Phase2Root");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex = m_Session.CreatePrefabFromEntity(rootId, "Phase2Prefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    const std::string guid = AssetGuid(m_Session, prefabIndex);
    const std::string oldPath = AssetPath(m_Session, prefabIndex);
    uint64_t instanceRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &instanceRoot))
        << m_Session.GetLastError();
    uint64_t secondRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &secondRoot))
        << m_Session.GetLastError();
    float pos[3] = {6.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(instanceRoot, pos, zero, one));
    ASSERT_TRUE(m_Session.SaveProject());
    ASSERT_TRUE(m_Session.PublishPrefabApplyPhase1(instanceRoot))
        << m_Session.GetLastError();
    const int64_t phase1Revision = m_Session.GetRevisionStoreForRuntime()->Revision();
    const std::string phase1Path = AssetPath(m_Session, prefabIndex);
    ASSERT_TRUE(m_Session.PublishPrefabApplyPhase2(instanceRoot))
        << m_Session.GetLastError();
    const int64_t phase2Revision = m_Session.GetRevisionStoreForRuntime()->Revision();
    EXPECT_GT(phase2Revision, phase1Revision);
    EXPECT_EQ(AssetPath(m_Session, prefabIndex), phase1Path);

    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        instanceRoot, &metadata));
    EXPECT_EQ(metadata.sourceRevision, phase1Revision);
    EXPECT_EQ(metadata.sourcePath, phase1Path);
    EXPECT_TRUE(metadata.overrides.empty());
    EXPECT_EQ(metadata.syncState, Engine::Content::PrefabSyncState::Clean);
    EXPECT_EQ(metadata.localToScene.at(1), instanceRoot);
    EXPECT_NE(m_Session.GetEditScene()->FindByEntityId(instanceRoot), nullptr);
    Engine::PrefabInstanceMetadata second;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(secondRoot,
                                                                     &second));
    EXPECT_EQ(second.sourcePath, oldPath);
    EXPECT_TRUE(second.overrides.empty());

    Engine::Content::PrefabApplyRecord record;
    std::string recordError;
    const Engine::ResourceGUID expected = Engine::ResourceGUID::FromHex(guid);
    EXPECT_FALSE(Engine::Content::ReadPrefabApplyRecord(
        *m_Session.GetProjectRootForRuntime(), expected, instanceRoot,
        &record, &recordError));

    ASSERT_TRUE(m_Session.SaveProject());
    editor_bridge::EditorSession reopened;
    ASSERT_TRUE(reopened.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json", "assets/gp01/Main.scene"));
    Engine::PrefabInstanceMetadata restored;
    ASSERT_TRUE(reopened.GetEditScene()->GetPrefabInstanceMetadata(
        instanceRoot, &restored));
    EXPECT_EQ(restored.sourceRevision, phase1Revision);
    EXPECT_EQ(restored.sourcePath, phase1Path);
    EXPECT_TRUE(restored.overrides.empty());
}TEST_F(PrefabInstanceTest, Phase2FailureKeepsPhase1RecordAndOverrides) {
    const int32_t rootIndex = m_Session.CreateEntity("Phase2FailRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex = m_Session.CreatePrefabFromEntity(rootId, "Phase2FailPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    const std::string guid = AssetGuid(m_Session, prefabIndex);
    const std::string oldPath = AssetPath(m_Session, prefabIndex);
    uint64_t instanceRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &instanceRoot))
        << m_Session.GetLastError();
    float pos[3] = {5.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(instanceRoot, pos, zero, one));
    ASSERT_TRUE(m_Session.SaveProject());
    ASSERT_TRUE(m_Session.PublishPrefabApplyPhase1(instanceRoot))
        << m_Session.GetLastError();
    const int64_t phase1Revision = m_Session.GetRevisionStoreForRuntime()->Revision();

    bool armed = false;
    Engine::Content::SetAtomicFaultHookForTesting(
        [&](Engine::Content::AtomicFaultPoint, const std::string&,
            const std::string& target) {
            if (!armed || target.find("manifest.json") == std::string::npos)
                return false;
            armed = false;
            return true;
        });
    Engine::Content::SetRevisionFaultHookForTesting(
        [&](Engine::Content::RevisionFaultPoint point) {
            if (point == Engine::Content::RevisionFaultPoint::
                             BeforeCandidateManifestWrite)
                armed = true;
        });
    EXPECT_FALSE(m_Session.PublishPrefabApplyPhase2(instanceRoot));
    Engine::Content::SetRevisionFaultHookForTesting({});
    Engine::Content::SetAtomicFaultHookForTesting({});

    EXPECT_EQ(m_Session.GetRevisionStoreForRuntime()->Revision(), phase1Revision);
    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        instanceRoot, &metadata));
    EXPECT_FALSE(metadata.overrides.empty());
    EXPECT_EQ(metadata.sourcePath, oldPath);
    Engine::Content::PrefabApplyRecord record;
    std::string recordError;
    const Engine::ResourceGUID expected = Engine::ResourceGUID::FromHex(guid);
    ASSERT_TRUE(Engine::Content::ReadPrefabApplyRecord(
        *m_Session.GetProjectRootForRuntime(), expected, instanceRoot,
        &record, &recordError)) << recordError;
    EXPECT_EQ(record.state, Engine::Content::PrefabApplyState::PrefabPublished);
    EXPECT_EQ(record.phase1Revision, phase1Revision);
}
TEST_F(PrefabInstanceTest, StartupRecoveryCompletesPhase2AndIsIdempotent) {
    const int32_t rootIndex = m_Session.CreateEntity("RecoveryRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex = m_Session.CreatePrefabFromEntity(rootId, "RecoveryPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    const std::string guid = AssetGuid(m_Session, prefabIndex);
    uint64_t instanceRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &instanceRoot))
        << m_Session.GetLastError();
    float pos[3] = {7.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(instanceRoot, pos, zero, one));
    ASSERT_TRUE(m_Session.SaveProject());
    ASSERT_TRUE(m_Session.PublishPrefabApplyPhase1(instanceRoot))
        << m_Session.GetLastError();
    const int64_t phase1Revision = m_Session.GetRevisionStoreForRuntime()->Revision();

    editor_bridge::EditorSession recovered;
    ASSERT_TRUE(recovered.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json", "assets/gp01/Main.scene"))
        << recovered.GetLastError();
    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(recovered.GetEditScene()->GetPrefabInstanceMetadata(
        instanceRoot, &metadata));
    EXPECT_TRUE(metadata.overrides.empty());
    EXPECT_EQ(metadata.sourceRevision, phase1Revision);
    EXPECT_EQ(metadata.syncState, Engine::Content::PrefabSyncState::Clean);
    EXPECT_GT(recovered.GetRevisionStoreForRuntime()->Revision(),
              phase1Revision);
    Engine::Content::PrefabApplyRecord record;
    std::string error;
    EXPECT_FALSE(Engine::Content::ReadPrefabApplyRecord(
        *recovered.GetProjectRootForRuntime(), Engine::ResourceGUID::FromHex(guid),
        instanceRoot, &record, &error));

    // A second open must be a clean no-op, not a duplicate revision.
    const int64_t afterFirst = recovered.GetRevisionStoreForRuntime()->Revision();
    editor_bridge::EditorSession again;
    ASSERT_TRUE(again.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json", "assets/gp01/Main.scene"))
        << again.GetLastError();
    EXPECT_EQ(again.GetRevisionStoreForRuntime()->Revision(), afterFirst);
    EXPECT_FALSE(Engine::Content::ReadPrefabApplyRecord(
        *again.GetProjectRootForRuntime(), Engine::ResourceGUID::FromHex(guid),
        instanceRoot, &record, &error));
}
TEST_F(PrefabInstanceTest, StartupRecoveryFailsClosedOnCorruptRecord) {
    const int32_t rootIndex = m_Session.CreateEntity("CorruptRecoveryRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex = m_Session.CreatePrefabFromEntity(rootId, "CorruptRecoveryPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    const std::string guid = AssetGuid(m_Session, prefabIndex);
    uint64_t instanceRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &instanceRoot))
        << m_Session.GetLastError();
    float pos[3] = {2.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(instanceRoot, pos, zero, one));
    ASSERT_TRUE(m_Session.SaveProject());
    ASSERT_TRUE(m_Session.PublishPrefabApplyPhase1(instanceRoot))
        << m_Session.GetLastError();
    const Engine::ResourceGUID expected = Engine::ResourceGUID::FromHex(guid);
    const std::string recordPath = Engine::Content::PrefabApplyRecordPath(
        *m_Session.GetProjectRootForRuntime(), expected, instanceRoot);
    ASSERT_FALSE(recordPath.empty());
    Write(recordPath, "{not a recovery record");

    editor_bridge::EditorSession recovered;
    EXPECT_FALSE(recovered.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json", "assets/gp01/Main.scene"));
    EXPECT_NE(recovered.GetLastError().find("RecoveryRequired"), std::string::npos)
        << recovered.GetLastError();
}

TEST_F(PrefabInstanceTest, StartupRecoveryRetriesAfterPhase2PublishFailure) {
    const int32_t rootIndex = m_Session.CreateEntity("RetryRecoveryRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex = m_Session.CreatePrefabFromEntity(rootId, "RetryRecoveryPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    const std::string guid = AssetGuid(m_Session, prefabIndex);
    uint64_t instanceRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &instanceRoot))
        << m_Session.GetLastError();
    float pos[3] = {2.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(instanceRoot, pos, zero, one));
    ASSERT_TRUE(m_Session.SaveProject());
    ASSERT_TRUE(m_Session.PublishPrefabApplyPhase1(instanceRoot))
        << m_Session.GetLastError();
    const int64_t phase1Revision = m_Session.GetRevisionStoreForRuntime()->Revision();

    bool armed = false;
    Engine::Content::SetRevisionFaultHookForTesting(
        [&](Engine::Content::RevisionFaultPoint point) {
            if (point == Engine::Content::RevisionFaultPoint::
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
    editor_bridge::EditorSession failed;
    EXPECT_FALSE(failed.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json", "assets/gp01/Main.scene"));
    Engine::Content::SetRevisionFaultHookForTesting({});
    Engine::Content::SetAtomicFaultHookForTesting({});

    Engine::Content::PrefabApplyRecord record;
    std::string error;
    EXPECT_TRUE(Engine::Content::ReadPrefabApplyRecord(
        *m_Session.GetProjectRootForRuntime(), Engine::ResourceGUID::FromHex(guid),
        instanceRoot, &record, &error)) << error;
    EXPECT_EQ(record.state, Engine::Content::PrefabApplyState::PrefabPublished);
    EXPECT_EQ(m_Session.GetRevisionStoreForRuntime()->Revision(), phase1Revision);

    editor_bridge::EditorSession retried;
    ASSERT_TRUE(retried.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json", "assets/gp01/Main.scene"))
        << retried.GetLastError();
    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(retried.GetEditScene()->GetPrefabInstanceMetadata(
        instanceRoot, &metadata));
    EXPECT_TRUE(metadata.overrides.empty());
    EXPECT_EQ(metadata.syncState, Engine::Content::PrefabSyncState::Clean);
}
TEST_F(PrefabInstanceTest, Phase2FaultPointsKeepPhase1OrRecovery) {
    for (const int point : {0, 1, 2, 5, 6, 7}) {
        const auto root = m_Root / ("phase2_fault_" + std::to_string(point));
        std::filesystem::create_directories(root / "assets" / "gp01");
        Write(root / "assets" / "gp01" / "manifest.json",
              "{\"version\":1,\"assets\":[]}");
        Write(root / "assets" / "gp01" / "Main.scene",
              "{\"format\":\"engine.scene\",\"version\":1,\"entities\":[]}");
        editor_bridge::EditorSession session;
        ASSERT_TRUE(session.OpenProjectRooted(
            root.string(), "assets/gp01/manifest.json", "assets/gp01/Main.scene"));
        int32_t entity = session.CreateEntity("Phase2FaultRoot");
        ASSERT_GE(entity, 0);
        uint64_t entityId = session.GetEntitySceneId(entity);
        int32_t prefab = session.CreatePrefabFromEntity(entityId, "Phase2FaultPrefab");
        ASSERT_GE(prefab, 0) << session.GetLastError();
        const std::string guid = AssetGuid(session, prefab);
        uint64_t instanceRoot = 0;
        ASSERT_TRUE(session.InstantiatePrefab(prefab, 0, &instanceRoot))
            << session.GetLastError();
        float pos[3] = {3.f, 0.f, 0.f};
        float zero[3] = {0.f, 0.f, 0.f};
        float one[3] = {1.f, 1.f, 1.f};
        ASSERT_TRUE(session.SetEntityTransformById(instanceRoot, pos, zero, one));
        ASSERT_TRUE(session.SaveProject());
        ASSERT_TRUE(session.PublishPrefabApplyPhase1(instanceRoot))
            << session.GetLastError();
        const int64_t phase1Revision = session.GetRevisionStoreForRuntime()->Revision();
        const auto currentPath = root / ".engine-content" / "current";
        std::string baseline;
        {
            std::ifstream in(currentPath, std::ios::binary);
            baseline.assign(std::istreambuf_iterator<char>(in),
                            std::istreambuf_iterator<char>());
        }
        struct Ctx {
            int point = 0;
            bool armed = false;
            std::string targetSuffix;
            std::filesystem::path current;
            std::filesystem::path candidateScene;
            std::string baseline;
        } ctx;
        ctx.point = point;
        ctx.current = currentPath;
        ctx.baseline = baseline;
        ctx.candidateScene = root / ".engine-content" / "revisions" /
                             std::to_string(phase1Revision + 1) / "scene.scene";
        Engine::Content::SetRevisionFaultHookForTesting(
            [&](Engine::Content::RevisionFaultPoint at) {
                if (static_cast<int>(at) != ctx.point) return;
                switch (ctx.point) {
                case 0: ctx.armed = true; ctx.targetSuffix = "manifest.json"; break;
                case 1: ctx.armed = true; ctx.targetSuffix = "scene.scene"; break;
                case 2: {
                    std::error_code ec;
                    std::filesystem::remove(ctx.candidateScene, ec);
                    break;
                }
                case 5:
                case 6: ctx.armed = true; ctx.targetSuffix = "current"; break;
                case 7: Write(ctx.current, ctx.baseline); break;
                }
            });
        Engine::Content::SetAtomicFaultHookForTesting(
            [&](Engine::Content::AtomicFaultPoint, const std::string&,
                const std::string& target) {
                if (!ctx.armed) return false;
                if (target.find(ctx.targetSuffix) == std::string::npos)
                    return false;
                ctx.armed = false;
                return true;
            });
        const bool published = session.PublishPrefabApplyPhase2(instanceRoot);
        Engine::Content::SetRevisionFaultHookForTesting({});
        Engine::Content::SetAtomicFaultHookForTesting({});
        SCOPED_TRACE(::testing::Message() << "phase2 fault point=" << point);

        if (point == 7) {
            EXPECT_FALSE(published);
            EXPECT_TRUE(session.IsRecoveryRequired());
            Engine::Content::PrefabApplyRecord record;
            std::string recordError;
            const Engine::ResourceGUID expected = Engine::ResourceGUID::FromHex(guid);
            EXPECT_TRUE(Engine::Content::ReadPrefabApplyRecord(
                *session.GetProjectRootForRuntime(), expected, instanceRoot,
                &record, &recordError)) << recordError;
            EXPECT_EQ(record.state,
                      Engine::Content::PrefabApplyState::RecoveryRequired);
        } else {
            EXPECT_FALSE(published);
            EXPECT_FALSE(session.IsRecoveryRequired());
            EXPECT_EQ(session.GetRevisionStoreForRuntime()->Revision(),
                      phase1Revision);
            Engine::PrefabInstanceMetadata metadata;
            ASSERT_TRUE(session.GetEditScene()->GetPrefabInstanceMetadata(
                instanceRoot, &metadata));
            EXPECT_FALSE(metadata.overrides.empty());
            Engine::Content::PrefabApplyRecord record;
            std::string recordError;
            const Engine::ResourceGUID expected = Engine::ResourceGUID::FromHex(guid);
            EXPECT_TRUE(Engine::Content::ReadPrefabApplyRecord(
                *session.GetProjectRootForRuntime(), expected, instanceRoot,
                &record, &recordError)) << recordError;
            EXPECT_EQ(record.state,
                      Engine::Content::PrefabApplyState::PrefabPublished);
        }
    }
}
TEST_F(PrefabInstanceTest, StartupRecoveryCancelsPreparedBeforePhase1) {
    const int32_t rootIndex = m_Session.CreateEntity("CancelRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex = m_Session.CreatePrefabFromEntity(rootId, "CancelPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    const std::string guid = AssetGuid(m_Session, prefabIndex);
    const std::string oldPath = AssetPath(m_Session, prefabIndex);
    uint64_t instanceRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &instanceRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.SaveProject());
    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        instanceRoot, &metadata));
    const auto expected = Engine::ResourceGUID::FromHex(guid);

    Engine::Content::PrefabApplyRecord record;
    record.transactionId = "cancel-prepared";
    record.state = Engine::Content::PrefabApplyState::Prepared;
    record.prefabGuid = expected;
    record.oldSourceRevision = metadata.sourceRevision;
    record.oldSourceDigest = metadata.sourceDigest;
    record.oldSourcePath = oldPath;
    record.newDigest = "0011223344556677";
    record.newPath = "assets/prefabs/CancelPrefab.0011223344556677.prefab.json";
    record.newSourceRevision = -1;
    record.instanceRootEntityId = instanceRoot;
    record.oldInstanceMetadata = {{"sourceRevision", metadata.sourceRevision}};
    record.newInstanceMetadata = {{"sourceRevision", 0}};
    record.oldRevisionSnapshot =
        m_Session.GetRevisionStoreForRuntime()->Revision();
    record.phase1Revision = -1;
    record.phase2Revision = -1;
    std::string recordError;
    ASSERT_TRUE(Engine::Content::WritePrefabApplyRecord(
        *m_Session.GetProjectRootForRuntime(), record, &recordError))
        << recordError;

    editor_bridge::EditorSession reopened;
    ASSERT_TRUE(reopened.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json", "assets/gp01/Main.scene"))
        << reopened.GetLastError();
    Engine::PrefabInstanceMetadata restored;
    ASSERT_TRUE(reopened.GetEditScene()->GetPrefabInstanceMetadata(
        instanceRoot, &restored));
    EXPECT_EQ(restored.sourcePath, oldPath);
    EXPECT_EQ(restored.sourceRevision, metadata.sourceRevision);
    EXPECT_FALSE(Engine::Content::ReadPrefabApplyRecord(
        *reopened.GetProjectRootForRuntime(), expected, instanceRoot,
        &record, &recordError));
}
TEST_F(PrefabInstanceTest, StartupRecoveryInfersPhase1WhenRecordUpdateWasLost) {
    const int32_t rootIndex = m_Session.CreateEntity("InferRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex = m_Session.CreatePrefabFromEntity(rootId, "InferPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    const std::string guid = AssetGuid(m_Session, prefabIndex);
    uint64_t instanceRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &instanceRoot))
        << m_Session.GetLastError();
    float pos[3] = {4.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(instanceRoot, pos, zero, one));
    ASSERT_TRUE(m_Session.SaveProject());
    ASSERT_TRUE(m_Session.PublishPrefabApplyPhase1(instanceRoot))
        << m_Session.GetLastError();
    const auto expected = Engine::ResourceGUID::FromHex(guid);
    Engine::Content::PrefabApplyRecord record;
    std::string recordError;
    ASSERT_TRUE(Engine::Content::ReadPrefabApplyRecord(
        *m_Session.GetProjectRootForRuntime(), expected, instanceRoot,
        &record, &recordError)) << recordError;
    record.state = Engine::Content::PrefabApplyState::Prepared;
    record.phase1Revision = -1;
    record.phase2Revision = -1;
    record.newSourceRevision = -1;
    record.newInstanceMetadata["sourceRevision"] = -1;
    ASSERT_TRUE(Engine::Content::WritePrefabApplyRecord(
        *m_Session.GetProjectRootForRuntime(), record, &recordError))
        << recordError;

    editor_bridge::EditorSession recovered;
    ASSERT_TRUE(recovered.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json", "assets/gp01/Main.scene"))
        << recovered.GetLastError();
    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(recovered.GetEditScene()->GetPrefabInstanceMetadata(
        instanceRoot, &metadata));
    EXPECT_TRUE(metadata.overrides.empty());
    EXPECT_EQ(metadata.syncState, Engine::Content::PrefabSyncState::Clean);
    EXPECT_FALSE(Engine::Content::ReadPrefabApplyRecord(
        *recovered.GetProjectRootForRuntime(), expected, instanceRoot,
        &record, &recordError));
}
TEST_F(PrefabInstanceTest, ReconciliationSourcesReadBaseLatestAndLocal) {
    const int32_t rootIndex = m_Session.CreateEntity("ResolveRoot");
    const int32_t childIndex = m_Session.CreateEntity("ResolveChild");
    auto* scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const uint64_t childId = m_Session.GetEntitySceneId(childIndex);
    ASSERT_TRUE(m_Session.SetEntityParent(childId, rootId, 0));
    const int32_t prefabIndex = m_Session.CreatePrefabFromEntity(rootId, "ResolvePrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    const std::string guid = AssetGuid(m_Session, prefabIndex);
    const std::string oldPath = AssetPath(m_Session, prefabIndex);
    uint64_t firstRoot = 0;
    uint64_t secondRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &firstRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &secondRoot))
        << m_Session.GetLastError();
    float pos[3] = {4.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(firstRoot, pos, zero, one));
    ASSERT_TRUE(m_Session.SaveProject());
    ASSERT_TRUE(m_Session.ApplyPrefabOverrides(firstRoot))
        << m_Session.GetLastError();

    Engine::Content::PrefabReconciliationSources sources;
    ASSERT_TRUE(m_Session.LoadPrefabReconciliationSources(secondRoot, &sources))
        << m_Session.GetLastError();
    EXPECT_EQ(sources.base.contentPath, oldPath);
    EXPECT_EQ(sources.base.prefabGuid, Engine::ResourceGUID::FromHex(guid));
    EXPECT_TRUE(sources.outOfDate);
    EXPECT_EQ(sources.latest.contentPath, AssetPath(m_Session, prefabIndex));
    EXPECT_EQ(sources.latest.prefabGuid, sources.base.prefabGuid);
    EXPECT_EQ(sources.local.rootEntityId, secondRoot);
    EXPECT_EQ(sources.local.entities.size(), 2u);
    ASSERT_EQ(sources.local.entities[0].localId, 1u);
    EXPECT_EQ(sources.local.entities[0].name, "ResolveRoot");
    ASSERT_EQ(sources.local.entities[1].localId, 2u);
    EXPECT_EQ(sources.local.entities[1].name, "ResolveChild");
    EXPECT_EQ(sources.local.entities[1].parentLocalId, 1u);
    EXPECT_TRUE(sources.local.overrides.empty());
    EXPECT_EQ(sources.local.syncState, Engine::Content::PrefabSyncState::Clean);

    const std::string childCopyPath = sources.base.contentPath;
    // A missing Base must not fall back to Latest.
    std::error_code ec;
    std::filesystem::remove(m_Root / childCopyPath, ec);
    ASSERT_FALSE(ec);
    Engine::Content::PrefabReconciliationSources unavailable;
    EXPECT_FALSE(m_Session.LoadPrefabReconciliationSources(secondRoot, &unavailable));
}

TEST_F(PrefabInstanceTest, ReconciliationSourcesRejectExtraSubtreeEntity) {
    const int32_t rootIndex = m_Session.CreateEntity("MappingRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex = m_Session.CreatePrefabFromEntity(rootId, "MappingPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    uint64_t instanceRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &instanceRoot))
        << m_Session.GetLastError();
    auto* scene = m_Session.GetEditScene();
    auto extra = std::make_shared<Engine::GameObject>("ExtraLocalEntity");
    std::string addError;
    ASSERT_TRUE(scene->AddEntity(extra, instanceRoot, &addError)) << addError;

    Engine::Content::PrefabReconciliationSources sources;
    EXPECT_FALSE(m_Session.LoadPrefabReconciliationSources(instanceRoot, &sources));
    EXPECT_NE(m_Session.GetLastError().find("subtree/mapping"), std::string::npos)
        << m_Session.GetLastError();
}
TEST_F(PrefabInstanceTest, RevertFieldAndInstanceRestoreBaseValues) {
    const int32_t rootIndex = m_Session.CreateEntity("RevertRoot");
    const int32_t childIndex = m_Session.CreateEntity("RevertChild");
    auto* scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const uint64_t childId = m_Session.GetEntitySceneId(childIndex);
    ASSERT_TRUE(m_Session.SetEntityParent(childId, rootId, 0));
    const Engine::ResourceGUID baseMesh =
        Engine::ResourceGUID::FromHex("fedcba9876543210fedcba9876543210");
    auto* rootMesh =
        scene->FindByEntityId(rootId)->AddComponent<Engine::MeshComponent>();
    ASSERT_NE(rootMesh, nullptr);
    rootMesh->SetMeshGuid(baseMesh);
    const int32_t prefabIndex =
        m_Session.CreatePrefabFromEntity(rootId, "RevertPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();

    uint64_t instanceRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &instanceRoot))
        << m_Session.GetLastError();
    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    const uint64_t instanceChild = metadata.localToScene.at(2);
    int32_t instanceIndex = -1;
    for (int32_t i = 0; i < m_Session.GetEntityCount(); ++i)
        if (m_Session.GetEntitySceneId(i) == instanceRoot) { instanceIndex = i; break; }
    ASSERT_GE(instanceIndex, 0);

    float x5y6[3] = {5.f, 6.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(instanceChild, x5y6, zero, one));
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    ASSERT_EQ(metadata.overrides.size(), 2u);

    ASSERT_TRUE(m_Session.RevertPrefabOverride(instanceRoot, 2, "Transform",
                                                "position.x"));
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    ASSERT_EQ(metadata.overrides.size(), 1u);
    EXPECT_EQ(scene->FindByEntityId(instanceChild)
                  ->GetTransform().GetPosition().x, 0.f);
    EXPECT_EQ(scene->FindByEntityId(instanceChild)
                  ->GetTransform().GetPosition().y, 6.f);

    ASSERT_TRUE(m_Session.UndoSceneEdit());
    scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    EXPECT_EQ(metadata.overrides.size(), 2u);
    EXPECT_EQ(scene->FindByEntityId(instanceChild)
                  ->GetTransform().GetPosition().x, 5.f);
    ASSERT_TRUE(m_Session.RedoSceneEdit());
    scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    EXPECT_EQ(metadata.overrides.size(), 1u);
    EXPECT_EQ(scene->FindByEntityId(instanceChild)
                  ->GetTransform().GetPosition().x, 0.f);

    ASSERT_TRUE(m_Session.SetComponentProperty(instanceIndex, "Mesh", "meshGuid",
                                                "11111111111111111111111111111111"));
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    ASSERT_TRUE(m_Session.RevertPrefabInstance(instanceRoot));
    scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(instanceRoot, &metadata));
    EXPECT_TRUE(metadata.overrides.empty());
    EXPECT_EQ(metadata.syncState, Engine::Content::PrefabSyncState::Clean);
    EXPECT_EQ(scene->FindByEntityId(instanceRoot)
                  ->GetComponent<Engine::MeshComponent>()->GetMeshGuid(),
              baseMesh);
    EXPECT_EQ(scene->FindByEntityId(instanceChild)
                  ->GetTransform().GetPosition().y, 0.f);
    EXPECT_NE(scene->FindByEntityId(instanceChild), nullptr);
    EXPECT_EQ(scene->GetParentEntityId(instanceChild), instanceRoot);

    ASSERT_TRUE(m_Session.SaveProject());
    editor_bridge::EditorSession reopened;
    ASSERT_TRUE(reopened.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json", "assets/gp01/Main.scene"));
    Engine::PrefabInstanceMetadata restored;
    ASSERT_TRUE(reopened.GetEditScene()->GetPrefabInstanceMetadata(
        instanceRoot, &restored));
    EXPECT_TRUE(restored.overrides.empty());
    EXPECT_EQ(restored.sourceDigest, metadata.sourceDigest);
}
TEST_F(PrefabInstanceTest, MissingPrefabFailsWithoutPartialInstance) {
    const int32_t rootIndex = m_Session.CreateEntity("MissingRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex = m_Session.CreatePrefabFromEntity(rootId, "MissingPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    const std::string path = AssetPath(m_Session, prefabIndex);
    ASSERT_FALSE(path.empty());
    std::error_code ec;
    std::filesystem::remove(m_Root / path, ec);
    ASSERT_FALSE(ec);

    const int32_t before = m_Session.GetTotalEntityCount();
    uint64_t newRoot = 0;
    EXPECT_FALSE(m_Session.InstantiatePrefab(prefabIndex, 0, &newRoot));
    EXPECT_EQ(newRoot, 0u);
    EXPECT_EQ(m_Session.GetTotalEntityCount(), before);
}

TEST_F(PrefabInstanceTest, ConflictStatePersistsColdStartAndResolutionUndoRedo) {
    const int32_t rootIndex = m_Session.CreateEntity("ConflictRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex =
        m_Session.CreatePrefabFromEntity(rootId, "ConflictPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    uint64_t firstRoot = 0;
    uint64_t secondRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &firstRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &secondRoot))
        << m_Session.GetLastError();

    float four[3] = {4.f, 0.f, 0.f};
    float two[3] = {2.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(firstRoot, four, zero, one));
    ASSERT_TRUE(m_Session.SaveProject());
    ASSERT_TRUE(m_Session.ApplyPrefabOverrides(firstRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.SetEntityTransformById(secondRoot, two, zero, one));

    Engine::Content::PrefabReconciliationPlan plan;
    ASSERT_TRUE(m_Session.PreviewPrefabReconciliation(secondRoot, &plan))
        << m_Session.GetLastError();
    ASSERT_TRUE(plan.hasConflicts);
    const auto fieldConflict = std::find_if(
        plan.conflicts.begin(), plan.conflicts.end(),
        [](const Engine::Content::PrefabReconciliationConflict& conflict) {
            return conflict.type == Engine::Content::PrefabConflictType::Field;
        });
    ASSERT_NE(fieldConflict, plan.conflicts.end());
    ASSERT_TRUE(m_Session.PersistPrefabReconciliationConflicts(secondRoot, plan))
        << m_Session.GetLastError();

    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        secondRoot, &metadata));
    ASSERT_TRUE(metadata.reconciliation.has_value());
    EXPECT_EQ(metadata.syncState, Engine::Content::PrefabSyncState::Conflict);
    EXPECT_EQ(metadata.reconciliation->conflicts.size(), plan.conflicts.size());

    ASSERT_TRUE(m_Session.SaveProject());
    editor_bridge::EditorSession reopened;
    ASSERT_TRUE(reopened.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json", "assets/gp01/Main.scene"))
        << reopened.GetLastError();
    Engine::PrefabInstanceMetadata restored;
    ASSERT_TRUE(reopened.GetEditScene()->GetPrefabInstanceMetadata(
        secondRoot, &restored));
    ASSERT_TRUE(restored.reconciliation.has_value());
    EXPECT_EQ(restored.syncState, Engine::Content::PrefabSyncState::Conflict);
    EXPECT_EQ(reopened.GetEditScene()->FindByEntityId(secondRoot)
                  ->GetTransform().GetPosition().x, 2.f);

    const std::string conflictId = fieldConflict->id;
    ASSERT_TRUE(reopened.ResolvePrefabReconciliationConflict(
        secondRoot, conflictId, "take_source", nlohmann::json()))
        << reopened.GetLastError();
    Engine::PrefabInstanceMetadata resolved;
    ASSERT_TRUE(reopened.GetEditScene()->GetPrefabInstanceMetadata(
        secondRoot, &resolved));
    EXPECT_FALSE(resolved.reconciliation.has_value());
    EXPECT_TRUE(resolved.overrides.empty());
    EXPECT_EQ(resolved.syncState, Engine::Content::PrefabSyncState::Clean);
    EXPECT_EQ(reopened.GetEditScene()->FindByEntityId(secondRoot)
                  ->GetTransform().GetPosition().x, 4.f);

    ASSERT_TRUE(reopened.UndoSceneEdit());
    Engine::PrefabInstanceMetadata undone;
    ASSERT_TRUE(reopened.GetEditScene()->GetPrefabInstanceMetadata(
        secondRoot, &undone));
    ASSERT_TRUE(undone.reconciliation.has_value());
    EXPECT_EQ(undone.syncState, Engine::Content::PrefabSyncState::Conflict);
    EXPECT_EQ(reopened.GetEditScene()->FindByEntityId(secondRoot)
                  ->GetTransform().GetPosition().x, 2.f);

    ASSERT_TRUE(reopened.RedoSceneEdit());
    Engine::PrefabInstanceMetadata redone;
    ASSERT_TRUE(reopened.GetEditScene()->GetPrefabInstanceMetadata(
        secondRoot, &redone));
    EXPECT_FALSE(redone.reconciliation.has_value());
    EXPECT_EQ(redone.syncState, Engine::Content::PrefabSyncState::Clean);
    EXPECT_EQ(reopened.GetEditScene()->FindByEntityId(secondRoot)
                  ->GetTransform().GetPosition().x, 4.f);
}

TEST_F(PrefabInstanceTest, KeepLocalConflictRebasesOverrideAgainstLatest) {
    const int32_t rootIndex = m_Session.CreateEntity("KeepRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex =
        m_Session.CreatePrefabFromEntity(rootId, "KeepPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    uint64_t firstRoot = 0;
    uint64_t secondRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &firstRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &secondRoot))
        << m_Session.GetLastError();

    float five[3] = {5.f, 0.f, 0.f};
    float three[3] = {3.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(firstRoot, five, zero, one));
    ASSERT_TRUE(m_Session.SaveProject());
    ASSERT_TRUE(m_Session.ApplyPrefabOverrides(firstRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.SetEntityTransformById(secondRoot, three, zero, one));

    Engine::Content::PrefabReconciliationPlan plan;
    ASSERT_TRUE(m_Session.PreviewPrefabReconciliation(secondRoot, &plan))
        << m_Session.GetLastError();
    ASSERT_TRUE(plan.hasConflicts);
    ASSERT_TRUE(m_Session.PersistPrefabReconciliationConflicts(secondRoot, plan))
        << m_Session.GetLastError();
    const std::string conflictId = "field:1:Transform:position.x";
    ASSERT_TRUE(m_Session.ResolvePrefabReconciliationConflict(
        secondRoot, conflictId, "keep_local", nlohmann::json()))
        << m_Session.GetLastError();

    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        secondRoot, &metadata));
    EXPECT_FALSE(metadata.reconciliation.has_value());
    EXPECT_EQ(metadata.syncState, Engine::Content::PrefabSyncState::Overrides);
    ASSERT_EQ(metadata.overrides.size(), 1u);
    EXPECT_EQ(metadata.overrides[0].field, "position.x");
    EXPECT_EQ(metadata.overrides[0].value, 3.0);
    EXPECT_EQ(m_Session.GetEditScene()->FindByEntityId(secondRoot)
                  ->GetTransform().GetPosition().x, 3.f);
}

TEST_F(PrefabInstanceTest, ManualEditConflictResolvesToExplicitValue) {
    const int32_t rootIndex = m_Session.CreateEntity("ManualRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex =
        m_Session.CreatePrefabFromEntity(rootId, "ManualPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    uint64_t firstRoot = 0;
    uint64_t secondRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &firstRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &secondRoot))
        << m_Session.GetLastError();

    float six[3] = {6.f, 0.f, 0.f};
    float three[3] = {3.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(firstRoot, six, zero, one));
    ASSERT_TRUE(m_Session.SaveProject());
    ASSERT_TRUE(m_Session.ApplyPrefabOverrides(firstRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.SetEntityTransformById(secondRoot, three, zero, one));

    Engine::Content::PrefabReconciliationPlan plan;
    ASSERT_TRUE(m_Session.PreviewPrefabReconciliation(secondRoot, &plan))
        << m_Session.GetLastError();
    ASSERT_TRUE(plan.hasConflicts);
    ASSERT_TRUE(m_Session.PersistPrefabReconciliationConflicts(secondRoot, plan))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.ResolvePrefabReconciliationConflict(
        secondRoot, "field:1:Transform:position.x", "manual_edit",
        nlohmann::json(7.0)))
        << m_Session.GetLastError();

    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        secondRoot, &metadata));
    EXPECT_FALSE(metadata.reconciliation.has_value());
    ASSERT_EQ(metadata.overrides.size(), 1u);
    EXPECT_EQ(metadata.overrides[0].value, 7.0);
    EXPECT_EQ(m_Session.GetEditScene()->FindByEntityId(secondRoot)
                  ->GetTransform().GetPosition().x, 7.f);
}

TEST_F(PrefabInstanceTest, ExecutorAppliesFieldMergeAndCommitsSourceIdentity) {
    const int32_t rootIndex = m_Session.CreateEntity("ExecutorRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex =
        m_Session.CreatePrefabFromEntity(rootId, "ExecutorPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    uint64_t firstRoot = 0;
    uint64_t secondRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &firstRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &secondRoot))
        << m_Session.GetLastError();

    float eight[3] = {8.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(firstRoot, eight, zero, one));
    ASSERT_TRUE(m_Session.SaveProject());
    ASSERT_TRUE(m_Session.ApplyPrefabOverrides(firstRoot))
        << m_Session.GetLastError();
    Engine::PrefabInstanceMetadata before;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        secondRoot, &before));
    const std::string oldDigest = before.sourceDigest;
    const std::string oldPath = before.sourcePath;
    ASSERT_TRUE(m_Session.ExecutePrefabReconciliation(secondRoot))
        << m_Session.GetLastError();

    Engine::PrefabInstanceMetadata after;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        secondRoot, &after));
    EXPECT_EQ(m_Session.GetEditScene()->FindByEntityId(secondRoot)
                  ->GetTransform().GetPosition().x, 8.f);
    EXPECT_NE(after.sourceDigest, oldDigest);
    EXPECT_NE(after.sourcePath, oldPath);
    EXPECT_EQ(after.syncState, Engine::Content::PrefabSyncState::Clean);
    EXPECT_FALSE(after.reconciliation.has_value());
    EXPECT_TRUE(after.overrides.empty());

    ASSERT_TRUE(m_Session.UndoSceneEdit());
    Engine::PrefabInstanceMetadata undone;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        secondRoot, &undone));
    EXPECT_EQ(undone.sourceDigest, oldDigest);
    EXPECT_EQ(undone.sourcePath, oldPath);
    EXPECT_EQ(m_Session.GetEditScene()->FindByEntityId(secondRoot)
                  ->GetTransform().GetPosition().x, 0.f);

    ASSERT_TRUE(m_Session.RedoSceneEdit());
    Engine::PrefabInstanceMetadata redone;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        secondRoot, &redone));
    EXPECT_EQ(redone.sourceDigest, after.sourceDigest);
    EXPECT_EQ(m_Session.GetEditScene()->FindByEntityId(secondRoot)
                  ->GetTransform().GetPosition().x, 8.f);
}

TEST_F(PrefabInstanceTest, ExecutorAddsLatestEntityAndUpdatesMapping) {
    const int32_t rootIndex = m_Session.CreateEntity("AddRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex =
        m_Session.CreatePrefabFromEntity(rootId, "AddPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    uint64_t firstRoot = 0;
    uint64_t secondRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &firstRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &secondRoot))
        << m_Session.GetLastError();
    float four[3] = {4.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(firstRoot, four, zero, one));
    ASSERT_TRUE(m_Session.SaveProject());
    ASSERT_TRUE(m_Session.ApplyPrefabOverrides(firstRoot))
        << m_Session.GetLastError();

    const std::string latestPath = AssetPath(m_Session, prefabIndex);
    ASSERT_FALSE(latestPath.empty());
    std::ifstream input(m_Root / latestPath, std::ios::binary);
    ASSERT_TRUE(input.good());
    nlohmann::json doc;
    input >> doc;
    input.close();
    doc["entities"].push_back({
        {"localId", 3},
        {"name", "LatestAdded"},
        {"parentLocalId", 1},
        {"localTransform",
         {{"position", {1.f, 0.f, 0.f}},
          {"rotation", {0.f, 0.f, 0.f, 1.f}},
          {"scale", {1.f, 1.f, 1.f}}}},
        {"components", nlohmann::json::array()}});
    const std::string text = doc.dump(2);
    Write(m_Root / latestPath, text);
    const std::string latestDigest =
        Engine::Content::ComputePrefabDigest(text);

    Engine::Content::PrefabReconciliationPlan plan;
    ASSERT_TRUE(m_Session.PreviewPrefabReconciliation(secondRoot, &plan))
        << m_Session.GetLastError();
    bool added = false;
    for (const auto& change : plan.structural)
        if (change.kind == Engine::Content::PrefabStructuralChangeKind::AddEntity &&
            change.localId == 3)
            added = true;
    EXPECT_TRUE(added);
    ASSERT_TRUE(m_Session.ExecutePrefabReconciliation(secondRoot))
        << m_Session.GetLastError();

    auto* scene = m_Session.GetEditScene();
    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(secondRoot, &metadata));
    ASSERT_EQ(metadata.localToScene.size(), 2u);
    const auto addedIt = metadata.localToScene.find(3);
    ASSERT_NE(addedIt, metadata.localToScene.end());
    auto* addedObject = scene->FindByEntityId(addedIt->second);
    ASSERT_NE(addedObject, nullptr);
    EXPECT_EQ(addedObject->GetName(), "LatestAdded");
    EXPECT_EQ(scene->GetParentEntityId(addedIt->second), secondRoot);
    EXPECT_EQ(metadata.sourceDigest, latestDigest);
    EXPECT_EQ(metadata.sourcePath, latestPath);
    EXPECT_EQ(metadata.syncState, Engine::Content::PrefabSyncState::Clean);

    ASSERT_TRUE(m_Session.SaveProject());
    editor_bridge::EditorSession reopened;
    ASSERT_TRUE(reopened.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json", "assets/gp01/Main.scene"))
        << reopened.GetLastError();
    Engine::PrefabInstanceMetadata restored;
    ASSERT_TRUE(reopened.GetEditScene()->GetPrefabInstanceMetadata(
        secondRoot, &restored));
    EXPECT_EQ(restored.localToScene.size(), 2u);
    EXPECT_EQ(restored.localToScene.count(3), 1u);
    EXPECT_EQ(restored.sourceDigest, latestDigest);
}

TEST_F(PrefabInstanceTest, ExecutorDeletesLatestRemovedEntityAndMapping) {
    const int32_t rootIndex = m_Session.CreateEntity("DeleteRoot");
    const int32_t childIndex = m_Session.CreateEntity("DeleteChild");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const uint64_t childId = m_Session.GetEntitySceneId(childIndex);
    ASSERT_TRUE(m_Session.SetEntityParent(childId, rootId, 0));
    const int32_t prefabIndex =
        m_Session.CreatePrefabFromEntity(rootId, "DeletePrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    uint64_t firstRoot = 0;
    uint64_t secondRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &firstRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &secondRoot))
        << m_Session.GetLastError();
    float four[3] = {4.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(firstRoot, four, zero, one));
    ASSERT_TRUE(m_Session.SaveProject());
    ASSERT_TRUE(m_Session.ApplyPrefabOverrides(firstRoot))
        << m_Session.GetLastError();

    const std::string latestPath = AssetPath(m_Session, prefabIndex);
    ASSERT_FALSE(latestPath.empty());
    std::ifstream input(m_Root / latestPath, std::ios::binary);
    ASSERT_TRUE(input.good());
    nlohmann::json doc;
    input >> doc;
    input.close();
    auto& entities = doc["entities"];
    nlohmann::json kept = nlohmann::json::array();
    for (const auto& entity : entities)
        if (entity["localId"].get<uint64_t>() == 1) kept.push_back(entity);
    doc["entities"] = std::move(kept);
    const std::string text = doc.dump(2);
    Write(m_Root / latestPath, text);

    Engine::PrefabInstanceMetadata before;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        secondRoot, &before));
    ASSERT_EQ(before.localToScene.size(), 2u);
    const uint64_t removedScene = before.localToScene.at(2);
    ASSERT_TRUE(m_Session.ExecutePrefabReconciliation(secondRoot))
        << m_Session.GetLastError();

    auto* scene = m_Session.GetEditScene();
    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(secondRoot, &metadata));
    EXPECT_EQ(metadata.localToScene.size(), 1u);
    EXPECT_EQ(metadata.localToScene.count(2), 0u);
    EXPECT_EQ(scene->FindByEntityId(removedScene), nullptr);
    EXPECT_EQ(metadata.sourceDigest, Engine::Content::ComputePrefabDigest(text));
    EXPECT_EQ(metadata.sourcePath, latestPath);
    EXPECT_EQ(metadata.syncState, Engine::Content::PrefabSyncState::Clean);
}

TEST_F(PrefabInstanceTest, ExecutorPreservesResolvedKeepLocalConflict) {
    const int32_t rootIndex = m_Session.CreateEntity("PreserveRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex =
        m_Session.CreatePrefabFromEntity(rootId, "PreservePrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    uint64_t firstRoot = 0;
    uint64_t secondRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &firstRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &secondRoot))
        << m_Session.GetLastError();
    float five[3] = {5.f, 0.f, 0.f};
    float three[3] = {3.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(firstRoot, five, zero, one));
    ASSERT_TRUE(m_Session.SaveProject());
    ASSERT_TRUE(m_Session.ApplyPrefabOverrides(firstRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.SetEntityTransformById(secondRoot, three, zero, one));

    Engine::Content::PrefabReconciliationPlan plan;
    ASSERT_TRUE(m_Session.PreviewPrefabReconciliation(secondRoot, &plan))
        << m_Session.GetLastError();
    ASSERT_TRUE(plan.hasConflicts);
    ASSERT_TRUE(m_Session.PersistPrefabReconciliationConflicts(secondRoot, plan))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.ResolvePrefabReconciliationConflict(
        secondRoot, "field:1:Transform:position.x", "keep_local",
        nlohmann::json()))
        << m_Session.GetLastError();

    Engine::Content::PrefabReconciliationPlan resolvedPlan;
    ASSERT_TRUE(m_Session.PreviewPrefabReconciliation(secondRoot, &resolvedPlan))
        << m_Session.GetLastError();
    EXPECT_TRUE(resolvedPlan.hasConflicts);
    ASSERT_TRUE(m_Session.ExecutePrefabReconciliation(secondRoot))
        << m_Session.GetLastError();

    auto* scene = m_Session.GetEditScene();
    Engine::PrefabInstanceMetadata metadata;
    ASSERT_TRUE(scene->GetPrefabInstanceMetadata(secondRoot, &metadata));
    EXPECT_EQ(scene->FindByEntityId(secondRoot)
                  ->GetTransform().GetPosition().x, 3.f);
    EXPECT_EQ(metadata.syncState, Engine::Content::PrefabSyncState::Overrides);
    ASSERT_EQ(metadata.overrides.size(), 1u);
    EXPECT_EQ(metadata.overrides[0].value, 3.0);
    EXPECT_FALSE(metadata.reconciliation.has_value());
    EXPECT_EQ(metadata.sourcePath, AssetPath(m_Session, prefabIndex));
}

TEST_F(PrefabInstanceTest, ExecutorRejectsUnresolvedConflicts) {
    const int32_t rootIndex = m_Session.CreateEntity("BlockedRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex =
        m_Session.CreatePrefabFromEntity(rootId, "BlockedPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    uint64_t firstRoot = 0;
    uint64_t secondRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &firstRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &secondRoot))
        << m_Session.GetLastError();
    float four[3] = {4.f, 0.f, 0.f};
    float two[3] = {2.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(firstRoot, four, zero, one));
    ASSERT_TRUE(m_Session.SaveProject());
    ASSERT_TRUE(m_Session.ApplyPrefabOverrides(firstRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.SetEntityTransformById(secondRoot, two, zero, one));

    Engine::Content::PrefabReconciliationPlan plan;
    ASSERT_TRUE(m_Session.PreviewPrefabReconciliation(secondRoot, &plan))
        << m_Session.GetLastError();
    ASSERT_TRUE(plan.hasConflicts);
    ASSERT_TRUE(m_Session.PersistPrefabReconciliationConflicts(secondRoot, plan))
        << m_Session.GetLastError();
    Engine::PrefabInstanceMetadata before;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        secondRoot, &before));
    EXPECT_FALSE(m_Session.ExecutePrefabReconciliation(secondRoot));
    EXPECT_NE(m_Session.GetLastError().find("unresolved conflicts"),
              std::string::npos) << m_Session.GetLastError();
    Engine::PrefabInstanceMetadata after;
    ASSERT_TRUE(m_Session.GetEditScene()->GetPrefabInstanceMetadata(
        secondRoot, &after));
    ASSERT_TRUE(after.reconciliation.has_value());
    EXPECT_EQ(after.reconciliation->conflicts.size(),
              before.reconciliation->conflicts.size());
    EXPECT_EQ(after.sourceDigest, before.sourceDigest);
}

TEST_F(PrefabInstanceTest, ReconciliationStatusAndPreviewJson) {
    const int32_t rootIndex = m_Session.CreateEntity("StatusRoot");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const int32_t prefabIndex =
        m_Session.CreatePrefabFromEntity(rootId, "StatusPrefab");
    ASSERT_GE(prefabIndex, 0) << m_Session.GetLastError();
    uint64_t firstRoot = 0;
    uint64_t secondRoot = 0;
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &firstRoot))
        << m_Session.GetLastError();
    ASSERT_TRUE(m_Session.InstantiatePrefab(prefabIndex, 0, &secondRoot))
        << m_Session.GetLastError();
    float four[3] = {4.f, 0.f, 0.f};
    float zero[3] = {0.f, 0.f, 0.f};
    float one[3] = {1.f, 1.f, 1.f};
    ASSERT_TRUE(m_Session.SetEntityTransformById(firstRoot, four, zero, one));
    ASSERT_TRUE(m_Session.SaveProject());
    ASSERT_TRUE(m_Session.ApplyPrefabOverrides(firstRoot))
        << m_Session.GetLastError();

    std::string statusJson;
    ASSERT_TRUE(m_Session.GetPrefabReconciliationStatusJson(secondRoot,
                                                             &statusJson))
        << m_Session.GetLastError();
    nlohmann::json status = nlohmann::json::parse(statusJson);
    EXPECT_TRUE(status["outOfDate"].get<bool>());
    EXPECT_FALSE(status["sourceUnavailable"].get<bool>());
    EXPECT_EQ(status["planConflictCount"].get<int>(), 0);
    EXPECT_EQ(status["persistedConflictCount"].get<int>(), 0);

    std::string previewJson;
    ASSERT_TRUE(m_Session.GetPrefabReconciliationPreviewJson(secondRoot,
                                                              &previewJson))
        << m_Session.GetLastError();
    nlohmann::json preview = nlohmann::json::parse(previewJson);
    EXPECT_TRUE(preview["outOfDate"].get<bool>());
    EXPECT_TRUE(preview["plan"]["conflicts"].empty());
    bool takeLatest = false;
    for (const auto& field : preview["plan"]["fields"]) {
        if (field["component"] == "Transform" &&
            field["field"] == "position.x" &&
            field["decision"] == "take_latest")
            takeLatest = true;
    }
    EXPECT_TRUE(takeLatest);
}
