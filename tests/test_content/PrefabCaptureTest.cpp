#include <gtest/gtest.h>

#include "Engine/Animation/SkinningComponent.h"
#include "Engine/Core/Content/PrefabAsset.h"
#include "Engine/Core/GameObject/CameraComponent.h"
#include "Engine/Core/GameObject/ColliderComponent.h"
#include "Engine/Core/GameObject/Component.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/MeshComponent.h"
#include "Engine/Core/GameObject/RigidBodyComponent.h"
#include "Engine/Core/Scene/Scene.h"

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>

using namespace Engine;
using namespace Engine::Content;

namespace {

ResourceGUID Guid(const char* hex) {
    return ResourceGUID::FromHex(hex);
}

std::shared_ptr<GameObject> Node(const std::string& name) {
    return std::make_shared<GameObject>(name);
}

void AddChild(Scene& scene, const std::shared_ptr<GameObject>& child,
              const std::shared_ptr<GameObject>& parent) {
    std::string error;
    ASSERT_TRUE(scene.AddEntity(child, parent->GetSceneEntityId(), &error))
        << error;
}

PrefabCaptureOptions Options() {
    PrefabCaptureOptions options;
    options.prefabGuid = Guid("0123456789abcdef0123456789abcdef");
    return options;
}

class UnsupportedPrefabComponent : public Component {
public:
    const char* GetComponentTypeName() const override { return "UnsupportedG4"; }
};

class ExtraFieldMeshComponent : public Component {
public:
    const char* GetComponentTypeName() const override { return "Mesh"; }
    void Serialize(nlohmann::json& json) const override {
        json["meshGuid"] = "0123456789abcdef0123456789abcdef";
        json["runtimeId"] = 7;
    }
};

} // namespace

TEST(PrefabCapture, SingleEntityPreservesIdentityNameAndTransform) {
    Scene scene;
    auto root = Node("Root");
    root->GetTransform().SetPosition(1.f, 2.f, 3.f);
    root->GetTransform().SetScale(Engine::Vec3(2.f, 3.f, 4.f));
    scene.AddObject(root);

    auto options = Options();
    auto result = CapturePrefabFromScene(scene, root->GetSceneEntityId(), options);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(ValidatePrefabModel(result.model).ok);

    ASSERT_EQ(result.model.entities.size(), 1u);
    const auto& entity = result.model.entities[0];
    EXPECT_EQ(entity.localId, 1u);
    EXPECT_EQ(result.model.rootLocalId, 1u);
    EXPECT_FALSE(entity.parentLocalId.has_value());
    EXPECT_EQ(entity.name, "Root");
    EXPECT_FLOAT_EQ(entity.localTransform.position.x, 1.f);
    EXPECT_FLOAT_EQ(entity.localTransform.position.y, 2.f);
    EXPECT_FLOAT_EQ(entity.localTransform.position.z, 3.f);
    EXPECT_FLOAT_EQ(entity.localTransform.scale.z, 4.f);
    EXPECT_EQ(result.localIds.sceneToLocal.at(root->GetSceneEntityId()), 1u);
    EXPECT_EQ(result.localIds.localToScene.at(1u), root->GetSceneEntityId());
}

TEST(PrefabCapture, MultiLevelSubtreeUsesDfsIdsAndParentRemap) {
    Scene scene;
    auto root = Node("Root");
    auto childA = Node("ChildA");
    auto grand = Node("Grand");
    auto childB = Node("ChildB");
    scene.AddObject(root);
    AddChild(scene, childA, root);
    AddChild(scene, grand, childA);
    AddChild(scene, childB, root);
    grand->GetTransform().SetPosition(5.f, 6.f, 7.f);

    auto result = CapturePrefabFromScene(scene, root->GetSceneEntityId(), Options());
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.model.entities.size(), 4u);

    EXPECT_EQ(result.localIds.sceneToLocal.at(root->GetSceneEntityId()), 1u);
    EXPECT_EQ(result.localIds.sceneToLocal.at(childA->GetSceneEntityId()), 2u);
    EXPECT_EQ(result.localIds.sceneToLocal.at(grand->GetSceneEntityId()), 3u);
    EXPECT_EQ(result.localIds.sceneToLocal.at(childB->GetSceneEntityId()), 4u);

    ASSERT_EQ(result.model.entities[0].localId, 1u);
    ASSERT_EQ(result.model.entities[1].localId, 2u);
    ASSERT_EQ(result.model.entities[2].localId, 3u);
    ASSERT_EQ(result.model.entities[3].localId, 4u);
    EXPECT_FALSE(result.model.entities[0].parentLocalId.has_value());
    EXPECT_EQ(*result.model.entities[1].parentLocalId, 1u);
    EXPECT_EQ(*result.model.entities[2].parentLocalId, 2u);
    EXPECT_EQ(*result.model.entities[3].parentLocalId, 1u);
    EXPECT_FLOAT_EQ(result.model.entities[2].localTransform.position.x, 5.f);
    EXPECT_TRUE(ValidatePrefabModel(result.model).ok);
}

TEST(PrefabCapture, CapturesAllowlistedComponentsAndMeshGuid) {
    Scene scene;
    auto root = Node("Root");
    scene.AddObject(root);
    auto* mesh = root->AddComponent<MeshComponent>();
    ASSERT_NE(mesh, nullptr);
    const ResourceGUID meshGuid = Guid("fedcba9876543210fedcba9876543210");
    mesh->SetMeshGuid(meshGuid);
    ASSERT_NE(root->AddComponent<CameraComponent>(), nullptr);
    ASSERT_NE(root->AddComponent<ColliderComponent>(), nullptr);
    ASSERT_NE(root->AddComponent<RigidBodyComponent>(), nullptr);
    ASSERT_NE(root->AddComponent<SkinningComponent>(), nullptr);

    auto result = CapturePrefabFromScene(scene, root->GetSceneEntityId(), Options());
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.model.entities.size(), 1u);
    const auto& components = result.model.entities[0].components;
    ASSERT_EQ(components.size(), 5u);
    EXPECT_EQ(components[0].type, "Camera");
    EXPECT_EQ(components[1].type, "Collider");
    EXPECT_EQ(components[2].type, "Mesh");
    EXPECT_EQ(components[3].type, "RigidBody");
    EXPECT_EQ(components[4].type, "Skinning");
    EXPECT_EQ(components[2].data["meshGuid"].get<std::string>(),
              meshGuid.ToHex());
    EXPECT_TRUE(ValidatePrefabModel(result.model).ok);
}

TEST(PrefabCapture, RejectsUnsupportedComponentWithoutPartialModel) {
    Scene scene;
    auto root = Node("Root");
    scene.AddObject(root);
    root->AddComponent<UnsupportedPrefabComponent>();

    auto result = CapturePrefabFromScene(scene, root->GetSceneEntityId(), Options());
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.model.entities.empty());
    EXPECT_TRUE(result.localIds.sceneToLocal.empty());
    EXPECT_TRUE(result.localIds.localToScene.empty());
}

class SecondMeshComponent : public Component {
public:
    const char* GetComponentTypeName() const override { return "Mesh"; }
    void Serialize(nlohmann::json& json) const override {
        json["meshGuid"] = "0123456789abcdef0123456789abcdef";
    }
};
TEST(PrefabCapture, RejectsUnknownComponentField) {
    Scene scene;
    auto root = Node("Root");
    scene.AddObject(root);
    root->AddComponent<ExtraFieldMeshComponent>();

    auto result = CapturePrefabFromScene(scene, root->GetSceneEntityId(), Options());
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("unknown field"), std::string::npos)
        << result.error;
    EXPECT_TRUE(result.model.entities.empty());
}

TEST(PrefabCapture, RejectsDuplicateComponentType) {
    Scene scene;
    auto root = Node("Root");
    scene.AddObject(root);
    root->AddComponent<MeshComponent>();
    root->AddComponent<SecondMeshComponent>();

    auto result = CapturePrefabFromScene(scene, root->GetSceneEntityId(), Options());
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("duplicate component"), std::string::npos)
        << result.error;
    EXPECT_TRUE(result.model.entities.empty());
}

TEST(PrefabCapture, ValidationRejectsMultipleRootsAndMissingParent) {
    Scene scene;
    auto root = Node("Root");
    auto child = Node("Child");
    scene.AddObject(root);
    AddChild(scene, child, root);
    auto result = CapturePrefabFromScene(scene, root->GetSceneEntityId(), Options());
    ASSERT_TRUE(result.ok) << result.error;

    auto multipleRoots = result.model;
    multipleRoots.entities[1].parentLocalId = std::nullopt;
    auto validation = ValidatePrefabModel(multipleRoots);
    EXPECT_FALSE(validation.ok);
    EXPECT_NE(validation.error.find("exactly one root"), std::string::npos)
        << validation.error;

    auto missingParent = result.model;
    missingParent.entities[1].parentLocalId = 999u;
    validation = ValidatePrefabModel(missingParent);
    EXPECT_FALSE(validation.ok);
    EXPECT_NE(validation.error.find("does not exist"), std::string::npos)
        << validation.error;
}
TEST(PrefabCapture, RejectsLegacyScriptBinding) {
    Scene scene;
    auto root = Node("Root");
    scene.AddObject(root);

    std::unordered_map<uint64_t, PrefabLegacyBinding> bindings;
    bindings[root->GetSceneEntityId()].scriptGuid =
        Guid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    auto options = Options();
    options.legacyBindings = &bindings;

    auto result = CapturePrefabFromScene(scene, root->GetSceneEntityId(), options);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("legacy"), std::string::npos) << result.error;
    EXPECT_TRUE(result.model.entities.empty());
}

TEST(PrefabCapture, RejectsNonFiniteTransform) {
    Scene scene;
    auto root = Node("Root");
    scene.AddObject(root);
    root->GetTransform().SetPosition(
        std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f);

    auto result = CapturePrefabFromScene(scene, root->GetSceneEntityId(), Options());
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("non-finite"), std::string::npos)
        << result.error;
    EXPECT_TRUE(result.model.entities.empty());
}

TEST(PrefabCapture, ValidationRejectsDuplicateLocalId) {
    Scene scene;
    auto root = Node("Root");
    scene.AddObject(root);
    auto result = CapturePrefabFromScene(scene, root->GetSceneEntityId(), Options());
    ASSERT_TRUE(result.ok) << result.error;

    result.model.entities.push_back(result.model.entities[0]);
    auto validation = ValidatePrefabModel(result.model);
    EXPECT_FALSE(validation.ok);
    EXPECT_NE(validation.error.find("duplicate"), std::string::npos)
        << validation.error;
}

TEST(PrefabCapture, ChildSubtreeRootDropsExternalParent) {
    Scene scene;
    auto root = Node("Root");
    auto child = Node("Child");
    auto grand = Node("Grand");
    scene.AddObject(root);
    AddChild(scene, child, root);
    AddChild(scene, grand, child);

    auto result = CapturePrefabFromScene(scene, child->GetSceneEntityId(), Options());
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.model.entities.size(), 2u);
    EXPECT_EQ(result.model.rootLocalId, 1u);
    EXPECT_FALSE(result.model.entities[0].parentLocalId.has_value());
    EXPECT_EQ(*result.model.entities[1].parentLocalId, 1u);
    EXPECT_EQ(result.model.entities[0].name, "Child");
}