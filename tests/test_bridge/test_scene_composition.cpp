#include <gtest/gtest.h>

#include "EditorSession.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

class SceneHierarchySessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_Root = std::filesystem::temp_directory_path() /
                 ("engine_p8_g1_" + std::to_string(
                     static_cast<unsigned long long>(
                         std::chrono::steady_clock::now()
                             .time_since_epoch()
                             .count())));
        std::filesystem::create_directories(m_Root / "assets" / "gp01");
        Write(m_Root / "assets" / "gp01" / "manifest.json",
              "{\"version\":1,\"assets\":[]}");
        Write(m_Root / "assets" / "gp01" / "scene.scene",
              "{\"format\":\"engine.scene\",\"version\":1,\"entities\":[]}");
        ASSERT_TRUE(m_Session.OpenProjectRooted(
            m_Root.string(), "assets/gp01/manifest.json",
            "assets/gp01/scene.scene"));
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

    editor_bridge::EditorSession m_Session;
    std::filesystem::path m_Root;
};

TEST_F(SceneHierarchySessionTest, ParentCommitUndoRedoAndColdRestart) {
    const int32_t a = m_Session.CreateEntity("A");
    const int32_t b = m_Session.CreateEntity("B");
    ASSERT_GE(a, 0);
    ASSERT_GE(b, 0);
    const uint64_t aId = m_Session.GetEntitySceneId(a);
    const uint64_t bId = m_Session.GetEntitySceneId(b);
    ASSERT_NE(aId, 0u);
    ASSERT_NE(bId, 0u);
    ASSERT_NE(aId, bId);

    ASSERT_TRUE(m_Session.BeginSceneEdit("parent"));
    ASSERT_TRUE(m_Session.SetEntityParent(bId, aId, 0));
    ASSERT_TRUE(m_Session.CommitSceneEdit());
    EXPECT_EQ(m_Session.GetTotalEntityCount(), 2);
    EXPECT_EQ(m_Session.GetEntityCount(), 1);
    ASSERT_NE(m_Session.GetEditScene(), nullptr);
    EXPECT_EQ(m_Session.GetEditScene()->GetParentEntityId(bId), aId);

    ASSERT_TRUE(m_Session.UndoSceneEdit());
    EXPECT_EQ(m_Session.GetEntityCount(), 2);
    EXPECT_EQ(m_Session.GetEditScene()->GetParentEntityId(bId), 0u);
    ASSERT_TRUE(m_Session.RedoSceneEdit());
    EXPECT_EQ(m_Session.GetEntityCount(), 1);
    EXPECT_EQ(m_Session.GetEditScene()->GetParentEntityId(bId), aId);

    ASSERT_TRUE(m_Session.SaveProject());
    editor_bridge::EditorSession reopened;
    ASSERT_TRUE(reopened.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json",
        "assets/gp01/scene.scene"));
    EXPECT_EQ(reopened.GetTotalEntityCount(), 2);
    EXPECT_EQ(reopened.GetEntityCount(), 1);
    EXPECT_NE(reopened.GetEditScene()->FindByEntityId(aId), nullptr);
    EXPECT_NE(reopened.GetEditScene()->FindByEntityId(bId), nullptr);
    EXPECT_EQ(reopened.GetEditScene()->GetParentEntityId(bId), aId);
}

TEST_F(SceneHierarchySessionTest, KeepWorldDeleteSubtreeAndUndo) {
    const int32_t parentIndex = m_Session.CreateEntity("Parent");
    const int32_t childIndex = m_Session.CreateEntity("Child");
    const uint64_t parentId = m_Session.GetEntitySceneId(parentIndex);
    const uint64_t childId = m_Session.GetEntitySceneId(childIndex);
    auto* scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    scene->FindByEntityId(parentId)->GetTransform().SetPosition(10.f, 0.f, 0.f);
    scene->FindByEntityId(childId)->GetTransform().SetPosition(15.f, 0.f, 0.f);

    ASSERT_TRUE(m_Session.BeginSceneEdit("keep world"));
    ASSERT_TRUE(m_Session.SetEntityParent(childId, parentId, 1));
    ASSERT_TRUE(m_Session.CommitSceneEdit());
    EXPECT_NEAR(scene->FindByEntityId(childId)->GetTransform().GetPosition().x,
                5.f, 1e-4f);
    EXPECT_NEAR(scene->FindByEntityId(childId)
                    ->GetTransform().LocalToWorld(Engine::Vec3(0, 0, 0)).x,
                15.f, 1e-4f);

    ASSERT_TRUE(m_Session.BeginSceneEdit("delete subtree"));
    ASSERT_TRUE(m_Session.DeleteEntityById(parentId));
    ASSERT_TRUE(m_Session.CommitSceneEdit());
    EXPECT_EQ(m_Session.GetTotalEntityCount(), 0);
    EXPECT_EQ(m_Session.GetEntityCount(), 0);

    ASSERT_TRUE(m_Session.UndoSceneEdit());
    EXPECT_EQ(m_Session.GetTotalEntityCount(), 2);
    EXPECT_EQ(m_Session.GetEntityCount(), 1);
    EXPECT_EQ(m_Session.GetEditScene()->GetParentEntityId(childId), parentId);
}

TEST_F(SceneHierarchySessionTest, DuplicateSubtreeIsOneTransactionAndSurvivesRestart) {
    const int32_t rootIndex = m_Session.CreateEntity("Parent");
    const int32_t childIndex = m_Session.CreateEntity("Child");
    const uint64_t rootId = m_Session.GetEntitySceneId(rootIndex);
    const uint64_t childId = m_Session.GetEntitySceneId(childIndex);
    auto* scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    scene->FindByEntityId(childId)->GetTransform().SetPosition(2.f, 3.f, 4.f);

    ASSERT_TRUE(m_Session.BeginSceneEdit("parent for duplicate"));
    ASSERT_TRUE(m_Session.SetEntityParent(childId, rootId, 0));
    ASSERT_TRUE(m_Session.CommitSceneEdit());
    EXPECT_EQ(m_Session.GetEntityCount(), 1);

    uint64_t copyId = 0;
    ASSERT_TRUE(m_Session.DuplicateEntityById(rootId, true, &copyId));
    ASSERT_NE(copyId, 0u);
    EXPECT_NE(copyId, rootId);
    EXPECT_EQ(m_Session.GetEntityCount(), 2);
    EXPECT_EQ(m_Session.GetTotalEntityCount(), 4);
    auto* copy = scene->FindByEntityId(copyId);
    ASSERT_NE(copy, nullptr);
    ASSERT_EQ(copy->GetChildren().size(), 1u);
    const uint64_t copyChildId = copy->GetChildren()[0]->GetSceneEntityId();
    EXPECT_NE(copyChildId, childId);
    EXPECT_EQ(scene->GetParentEntityId(copyChildId), copyId);
    EXPECT_NEAR(copy->GetChildren()[0]->GetTransform().GetPosition().z, 4.f, 1e-5f);

    ASSERT_TRUE(m_Session.UndoSceneEdit());
    scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(m_Session.GetEntityCount(), 1);
    EXPECT_EQ(m_Session.GetTotalEntityCount(), 2);
    EXPECT_EQ(scene->FindByEntityId(copyId), nullptr);
    ASSERT_TRUE(m_Session.RedoSceneEdit());
    scene = m_Session.GetEditScene();
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(m_Session.GetEntityCount(), 2);
    EXPECT_NE(scene->FindByEntityId(copyId), nullptr);
    EXPECT_NE(scene->FindByEntityId(copyChildId), nullptr);

    ASSERT_TRUE(m_Session.SaveProject());
    editor_bridge::EditorSession reopened;
    ASSERT_TRUE(reopened.OpenProjectRooted(
        m_Root.string(), "assets/gp01/manifest.json",
        "assets/gp01/scene.scene"));
    EXPECT_EQ(reopened.GetEntityCount(), 2);
    EXPECT_EQ(reopened.GetTotalEntityCount(), 4);
    EXPECT_NE(reopened.GetEditScene()->FindByEntityId(copyId), nullptr);
    EXPECT_NE(reopened.GetEditScene()->FindByEntityId(copyChildId), nullptr);
    EXPECT_EQ(reopened.GetEditScene()->GetParentEntityId(copyChildId), copyId);
}

TEST_F(SceneHierarchySessionTest, DuplicateFailureDoesNotEnterHistory) {
    const int32_t index = m_Session.CreateEntity("Only");
    const uint64_t id = m_Session.GetEntitySceneId(index);
    ASSERT_NE(id, 0u);
    ASSERT_FALSE(m_Session.CanUndoSceneEdit());
    uint64_t created = 0;
    EXPECT_FALSE(m_Session.DuplicateEntityById(id + 9999, true, &created));
    EXPECT_EQ(created, 0u);
    EXPECT_EQ(m_Session.GetTotalEntityCount(), 1);
    EXPECT_FALSE(m_Session.CanUndoSceneEdit());
}
} // namespace
