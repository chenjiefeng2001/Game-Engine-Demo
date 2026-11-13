#include <gtest/gtest.h>

#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/Component.h"
#include "Engine/Core/GameObject/MeshComponent.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/Scene/Scene.h"
#include "StubGraphicsFactory.h"

#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>

using namespace Engine;
using namespace Engine::Content;

namespace {

std::shared_ptr<GameObject> MakeNode(const std::string& name) {
    return std::make_shared<GameObject>(name);
}

void AddRoot(Scene& scene, const std::shared_ptr<GameObject>& node) {
    scene.AddObject(node);
    ASSERT_NE(node->GetSceneEntityId(), 0u);
}

void AddChild(Scene& scene, const std::shared_ptr<GameObject>& child,
              const std::shared_ptr<GameObject>& parent) {
    std::string error;
    ASSERT_TRUE(scene.AddEntity(child, parent->GetSceneEntityId(), &error))
        << error;
}

SceneSnapshot DeserializeJson(const nlohmann::json& j) {
    SceneSnapshot snap;
    std::string err;
    EXPECT_TRUE(DeserializeSnapshot(j, snap, err)) << err;
    return snap;
}

} // namespace

TEST(SceneComposition, HierarchyRoundTripPreservesIdsParentsAndWorld) {
    Scene scene;
    auto root = MakeNode("Root");
    auto child = MakeNode("Child");
    auto grand = MakeNode("Grand");
    AddRoot(scene, root);
    AddChild(scene, child, root);
    AddChild(scene, grand, child);

    root->GetTransform().SetPosition(10.f, 0.f, 0.f);
    child->GetTransform().SetPosition(0.f, 2.f, 0.f);
    grand->GetTransform().SetPosition(0.f, 0.f, 3.f);

    ASSERT_TRUE(scene.ValidateComposition());
    EXPECT_EQ(scene.GetObjectCount(), 1u);
    EXPECT_EQ(scene.GetTotalObjectCount(), 3u);

    const SceneSnapshot captured = CaptureScene(scene, {});
    ASSERT_EQ(captured.version, 2);
    ASSERT_EQ(captured.entities.size(), 3u);
    EXPECT_EQ(captured.entities[0].entityId, root->GetSceneEntityId());
    EXPECT_EQ(captured.entities[0].parentId, 0u);
    EXPECT_EQ(captured.entities[1].entityId, child->GetSceneEntityId());
    EXPECT_EQ(captured.entities[1].parentId, root->GetSceneEntityId());
    EXPECT_EQ(captured.entities[2].parentId, child->GetSceneEntityId());

    const nlohmann::json json = SerializeSnapshot(captured);
    const SceneSnapshot loaded = DeserializeJson(json);
    ASSERT_EQ(loaded.version, 2);
    ASSERT_EQ(loaded.entities.size(), 3u);

    StubGraphicsFactory gfx;
    TextureManager textures(gfx);
    ContentRegistry registry;
    Scene restored;
    const LoadResult result = InstantiateScene(loaded, restored, textures, registry);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(restored.GetObjectCount(), 1u);
    ASSERT_EQ(restored.GetTotalObjectCount(), 3u);
    ASSERT_TRUE(restored.ValidateComposition());

    auto* restoredRoot = restored.FindByEntityId(root->GetSceneEntityId());
    auto* restoredChild = restored.FindByEntityId(child->GetSceneEntityId());
    auto* restoredGrand = restored.FindByEntityId(grand->GetSceneEntityId());
    ASSERT_NE(restoredRoot, nullptr);
    ASSERT_NE(restoredChild, nullptr);
    ASSERT_NE(restoredGrand, nullptr);
    EXPECT_EQ(restoredChild->GetParent(), restoredRoot);
    EXPECT_EQ(restoredGrand->GetParent(), restoredChild);
    const Vec3 world = restoredGrand->GetTransform().LocalToWorld(Vec3(0, 0, 0));
    EXPECT_NEAR(world.x, 10.f, 1e-4f);
    EXPECT_NEAR(world.y, 2.f, 1e-4f);
    EXPECT_NEAR(world.z, 3.f, 1e-4f);
}

TEST(SceneComposition, V1SceneLoadsAsRootsAndUpgradesOnCapture) {
    const nlohmann::json v1 = {
        {"format", "engine.scene"},
        {"version", 1},
        {"entities", {
            {{"name", "A"}, {"position", {1, 2, 3}}},
            {{"name", "B"}, {"position", {4, 5, 6}}}
        }}
    };
    SceneSnapshot loaded = DeserializeJson(v1);
    EXPECT_EQ(loaded.version, 1);
    ASSERT_EQ(loaded.entities.size(), 2u);

    StubGraphicsFactory gfx;
    TextureManager textures(gfx);
    ContentRegistry registry;
    Scene scene;
    const LoadResult result = InstantiateScene(loaded, scene, textures, registry);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(scene.GetObjectCount(), 2u);
    EXPECT_NE(scene.GetObjects()[0]->GetSceneEntityId(), 0u);
    EXPECT_NE(scene.GetObjects()[1]->GetSceneEntityId(), 0u);
    EXPECT_EQ(scene.GetObjects()[0]->GetParent(), nullptr);
    EXPECT_EQ(scene.GetObjects()[1]->GetParent(), nullptr);

    const SceneSnapshot upgraded = CaptureScene(scene, {});
    EXPECT_EQ(upgraded.version, 2);
    EXPECT_EQ(upgraded.entities[0].parentId, 0u);
    EXPECT_EQ(upgraded.entities[1].parentId, 0u);
    EXPECT_NE(upgraded.entities[0].entityId, 0u);
}

TEST(SceneComposition, V2RejectsInvalidGraphs) {
    auto base = [](const std::string& name, uint64 id, uint64 parent) {
        return nlohmann::json{
            {"name", name},
            {"id", id},
            {"parent", parent},
            {"position", {0, 0, 0}}
        };
    };
    const auto frame = [](const nlohmann::json& entities) {
        return nlohmann::json{
            {"format", "engine.scene"},
            {"version", 2},
            {"entities", entities}
        };
    };

    {
        SceneSnapshot out;
        std::string err;
        EXPECT_FALSE(DeserializeSnapshot(
            frame(nlohmann::json::array({base("A", 1, 0), base("B", 1, 0)})),
            out, err));
        EXPECT_NE(err.find("duplicate"), std::string::npos) << err;
    }
    {
        SceneSnapshot out;
        std::string err;
        EXPECT_FALSE(DeserializeSnapshot(
            frame(nlohmann::json::array({base("A", 1, 99)})), out, err));
        EXPECT_NE(err.find("parent"), std::string::npos) << err;
    }
    {
        SceneSnapshot out;
        std::string err;
        EXPECT_FALSE(DeserializeSnapshot(
            frame(nlohmann::json::array({base("A", 1, 1)})), out, err));
        EXPECT_NE(err.find("self"), std::string::npos) << err;
    }
    {
        SceneSnapshot out;
        std::string err;
        EXPECT_FALSE(DeserializeSnapshot(
            frame(nlohmann::json::array({base("A", 1, 2), base("B", 2, 1)})),
            out, err));
        EXPECT_NE(err.find("cycle"), std::string::npos) << err;
    }
}

TEST(SceneComposition, KeepWorldAndKeepLocalReparent) {
    Scene scene;
    auto first = MakeNode("First");
    auto second = MakeNode("Second");
    auto child = MakeNode("Child");
    scene.AddObject(first);
    scene.AddObject(second);
    scene.AddObject(child);
    const uint64 childId = child->GetSceneEntityId();
    const uint64 firstId = first->GetSceneEntityId();
    const uint64 secondId = second->GetSceneEntityId();

    first->GetTransform().SetPosition(10.f, 0.f, 0.f);
    second->GetTransform().SetPosition(100.f, 0.f, 0.f);
    child->GetTransform().SetPosition(5.f, 0.f, 0.f);

    std::string err;
    ASSERT_TRUE(scene.SetEntityParent(childId, firstId,
                                      ReparentMode::KeepLocal, &err)) << err;
    EXPECT_NEAR(child->GetTransform().LocalToWorld(Vec3(0, 0, 0)).x,
                15.f, 1e-4f);

    ASSERT_TRUE(scene.SetEntityParent(childId, secondId,
                                      ReparentMode::KeepWorld, &err)) << err;
    EXPECT_NEAR(child->GetTransform().GetPosition().x, -85.f, 1e-4f);
    EXPECT_NEAR(child->GetTransform().LocalToWorld(Vec3(0, 0, 0)).x,
                15.f, 1e-4f);

    ASSERT_TRUE(scene.SetEntityParent(childId, 0,
                                      ReparentMode::KeepWorld, &err)) << err;
    EXPECT_NEAR(child->GetTransform().GetPosition().x, 15.f, 1e-4f);
    EXPECT_NEAR(child->GetTransform().LocalToWorld(Vec3(0, 0, 0)).x,
                15.f, 1e-4f);
}

TEST(SceneComposition, KeepWorldRejectsSingularParentAndCycle) {
    Scene scene;
    auto parent = MakeNode("Parent");
    auto child = MakeNode("Child");
    scene.AddObject(parent);
    scene.AddObject(child);
    parent->GetTransform().SetScale(Vec3(0.f, 0.f, 0.f));
    child->GetTransform().SetPosition(3.f, 0.f, 0.f);
    std::string err;

    EXPECT_FALSE(scene.SetEntityParent(child->GetSceneEntityId(),
                                       parent->GetSceneEntityId(),
                                       ReparentMode::KeepWorld, &err));
    EXPECT_FALSE(err.empty());
    EXPECT_EQ(scene.GetParentEntityId(child->GetSceneEntityId()), 0u);

    parent->GetTransform().SetScale(Vec3(1.f, 1.f, 1.f));
    ASSERT_TRUE(scene.SetEntityParent(child->GetSceneEntityId(),
                                      parent->GetSceneEntityId(),
                                      ReparentMode::KeepWorld, &err)) << err;
    EXPECT_FALSE(scene.SetEntityParent(parent->GetSceneEntityId(),
                                       child->GetSceneEntityId(),
                                       ReparentMode::KeepWorld, &err));
    EXPECT_TRUE(scene.ValidateComposition());
}

TEST(SceneComposition, ParentChangeInvalidatesDescendantWorld) {
    Scene scene;
    auto parent = MakeNode("Parent");
    auto child = MakeNode("Child");
    auto grand = MakeNode("Grand");
    AddRoot(scene, parent);
    AddChild(scene, child, parent);
    AddChild(scene, grand, child);
    parent->GetTransform().SetPosition(10.f, 0.f, 0.f);
    child->GetTransform().SetPosition(5.f, 0.f, 0.f);
    grand->GetTransform().SetPosition(2.f, 0.f, 0.f);
    EXPECT_NEAR(grand->GetTransform().LocalToWorld(Vec3(0, 0, 0)).x,
                17.f, 1e-4f);
    parent->GetTransform().SetPosition(20.f, 0.f, 0.f);
    EXPECT_NEAR(grand->GetTransform().LocalToWorld(Vec3(0, 0, 0)).x,
                27.f, 1e-4f);
}

TEST(SceneComposition, DuplicateIdAddFailsClosed) {
    Scene scene;
    auto root = MakeNode("Root");
    AddRoot(scene, root);
    auto duplicate = MakeNode("Duplicate");
    duplicate->SetSceneEntityId(root->GetSceneEntityId());
    std::string err;
    EXPECT_FALSE(scene.AddEntity(duplicate, 0, &err));
    EXPECT_FALSE(err.empty());
    EXPECT_EQ(scene.GetTotalObjectCount(), 1u);
    EXPECT_EQ(scene.FindByEntityId(root->GetSceneEntityId()), root.get());
    EXPECT_TRUE(scene.ValidateComposition());
}

TEST(SceneComposition, DeleteSubtreeRemovesAllIds) {
    Scene scene;
    auto root = MakeNode("Root");
    auto child = MakeNode("Child");
    auto grand = MakeNode("Grand");
    AddRoot(scene, root);
    AddChild(scene, child, root);
    AddChild(scene, grand, child);
    const uint64 rootId = root->GetSceneEntityId();
    const uint64 childId = child->GetSceneEntityId();
    const uint64 grandId = grand->GetSceneEntityId();

    ASSERT_TRUE(scene.DeleteEntity(rootId));
    EXPECT_EQ(scene.GetObjectCount(), 0u);
    EXPECT_EQ(scene.GetTotalObjectCount(), 0u);
    EXPECT_EQ(scene.FindByEntityId(rootId), nullptr);
    EXPECT_EQ(scene.FindByEntityId(childId), nullptr);
    EXPECT_EQ(scene.FindByEntityId(grandId), nullptr);
    EXPECT_TRUE(scene.ValidateComposition());
}

namespace {
class UnsupportedDuplicateComponent : public Engine::Component {
public:
    const char* GetComponentTypeName() const override { return "UnsupportedG3"; }
};
}

TEST(SceneComposition, DuplicateSubtreeAllocatesIdsAndRemapsParents) {
    Scene scene;
    auto root = MakeNode("Root");
    auto child = MakeNode("Child");
    auto grand = MakeNode("Grand");
    AddRoot(scene, root);
    AddChild(scene, child, root);
    AddChild(scene, grand, child);
    child->GetTransform().SetPosition(4.f, 5.f, 6.f);
    grand->GetTransform().SetPosition(1.f, 2.f, 3.f);

    const Engine::ResourceGUID meshGuid =
        Engine::ResourceGUID::FromHex("0123456789abcdef0123456789abcdef");
    auto* mesh = child->AddComponent<Engine::MeshComponent>();
    ASSERT_NE(mesh, nullptr);
    mesh->SetMeshGuid(meshGuid);

    uint64 newRootId = 0;
    std::unordered_map<uint64, uint64> idMap;
    std::string error;
    ASSERT_TRUE(scene.DuplicateEntity(child->GetSceneEntityId(), true,
                                      &newRootId, &idMap, &error)) << error;

    ASSERT_NE(newRootId, 0u);
    EXPECT_NE(newRootId, child->GetSceneEntityId());
    EXPECT_EQ(idMap.size(), 2u);
    EXPECT_EQ(idMap[child->GetSceneEntityId()], newRootId);
    auto* newChild = scene.FindByEntityId(newRootId);
    ASSERT_NE(newChild, nullptr);
    EXPECT_EQ(newChild->GetParent(), root.get());
    EXPECT_EQ(newChild->GetName(), "Child_Copy");
    EXPECT_EQ(newChild->GetTransform().GetPosition().x, 4.f);
    ASSERT_EQ(newChild->GetChildren().size(), 1u);
    auto* newGrand = newChild->GetChildren()[0].get();
    EXPECT_EQ(newGrand->GetParent(), newChild);
    EXPECT_NE(newGrand->GetSceneEntityId(), grand->GetSceneEntityId());
    EXPECT_EQ(idMap[grand->GetSceneEntityId()], newGrand->GetSceneEntityId());
    EXPECT_EQ(newGrand->GetTransform().GetPosition().z, 3.f);

    auto* clonedMesh = newChild->GetComponent<Engine::MeshComponent>();
    ASSERT_NE(clonedMesh, nullptr);
    EXPECT_NE(clonedMesh, mesh);
    EXPECT_EQ(clonedMesh->GetMeshGuid(), meshGuid);
    EXPECT_EQ(clonedMesh->GetRuntimeStatus(), Engine::MeshRuntimeStatus::NotRequested);

    EXPECT_EQ(scene.GetTotalObjectCount(), 5u);
    EXPECT_TRUE(scene.ValidateComposition());
}

TEST(SceneComposition, DuplicateSingleEntityDoesNotCopyChildren) {
    Scene scene;
    auto root = MakeNode("Root");
    auto child = MakeNode("Child");
    auto grand = MakeNode("Grand");
    AddRoot(scene, root);
    AddChild(scene, child, root);
    AddChild(scene, grand, child);

    uint64 newRootId = 0;
    ASSERT_TRUE(scene.DuplicateEntity(root->GetSceneEntityId(), false,
                                      &newRootId));
    auto* copy = scene.FindByEntityId(newRootId);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->GetParent(), nullptr);
    EXPECT_TRUE(copy->GetChildren().empty());
    EXPECT_EQ(scene.GetObjectCount(), 2u);
    EXPECT_EQ(scene.GetTotalObjectCount(), 4u);
}

TEST(SceneComposition, DuplicateUnsupportedComponentFailsAtomically) {
    Scene scene;
    auto root = MakeNode("Root");
    AddRoot(scene, root);
    root->AddComponent<UnsupportedDuplicateComponent>();
    const size_t before = scene.GetTotalObjectCount();
    const uint64 sourceId = root->GetSceneEntityId();

    uint64 newRootId = 0;
    std::string error;
    EXPECT_FALSE(scene.DuplicateEntity(sourceId, true, &newRootId, nullptr, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("unsupported"), std::string::npos) << error;
    EXPECT_EQ(scene.GetTotalObjectCount(), before);
    EXPECT_EQ(scene.FindByEntityId(sourceId), root.get());
    EXPECT_TRUE(scene.ValidateComposition());
}