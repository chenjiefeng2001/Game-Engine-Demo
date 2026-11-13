#include <gtest/gtest.h>

#include "Engine/Core/Content/PrefabOverrideModel.h"
#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/Scene/Scene.h"
#include "StubGraphicsFactory.h"

#include <string>
#include <unordered_map>
#include <vector>

using namespace Engine;
using namespace Engine::Content;

namespace {

ResourceGUID Guid(const char* hex) {
    return ResourceGUID::FromHex(hex);
}

PrefabModel SourceModel() {
    PrefabModel model;
    model.prefabGuid = Guid("0123456789abcdef0123456789abcdef");
    model.rootLocalId = 1;

    PrefabEntity root;
    root.localId = 1;
    root.name = "Root";
    root.localTransform.position = Vec3(1.f, 2.f, 3.f);
    model.entities.push_back(root);

    PrefabEntity child;
    child.localId = 2;
    child.name = "Child";
    child.parentLocalId = 1;
    child.components.push_back(PrefabComponentData{
        "Mesh",
        {{"meshGuid", "fedcba9876543210fedcba9876543210"}}});
    model.entities.push_back(child);
    return model;
}

PrefabOverrideRecord Record(uint64_t localId, const char* component,
                            const char* field, nlohmann::json value) {
    return PrefabOverrideRecord{localId, component, field, std::move(value)};
}

} // namespace

TEST(PrefabOverride, FieldAllowlistAndValueValidation) {
    PrefabOverrideFieldSpec spec;
    EXPECT_TRUE(ResolvePrefabOverrideField("Transform", "position.x", &spec));
    EXPECT_EQ(spec.type, PrefabOverrideValueType::Float);
    EXPECT_TRUE(ResolvePrefabOverrideField("Transform", "rotation.w", &spec));
    EXPECT_TRUE(ResolvePrefabOverrideField("Mesh", "meshGuid", &spec));
    EXPECT_TRUE(ResolvePrefabOverrideField("Camera", "enabled", &spec));
    EXPECT_TRUE(ResolvePrefabOverrideField("Collider", "shape", &spec));
    EXPECT_TRUE(ResolvePrefabOverrideField("RigidBody", "motionType", &spec));
    EXPECT_FALSE(ResolvePrefabOverrideField("Skinning", "visible", &spec));
    EXPECT_FALSE(ResolvePrefabOverrideField("Mesh", "visible", &spec));
    EXPECT_FALSE(ResolvePrefabOverrideField("Material", "guid", &spec));

    std::string error;
    EXPECT_TRUE(ValidatePrefabOverrideValue(
        "Transform", "position.x", 3.5, &error));
    EXPECT_FALSE(ValidatePrefabOverrideValue(
        "Transform", "position.x", "bad", &error));
    EXPECT_FALSE(ValidatePrefabOverrideValue(
        "Camera", "zoom", 0.0, &error));
    EXPECT_FALSE(ValidatePrefabOverrideValue(
        "Collider", "radius", -1.0, &error));
    EXPECT_FALSE(ValidatePrefabOverrideValue(
        "Collider", "shape", "Triangle", &error));
    EXPECT_FALSE(ValidatePrefabOverrideValue(
        "RigidBody", "motionType", "Flying", &error));
    EXPECT_FALSE(ValidatePrefabOverrideValue(
        "Mesh", "meshGuid", "not-a-guid", &error));
    EXPECT_TRUE(ValidatePrefabOverrideValue(
        "Mesh", "meshGuid", "", &error));
}

TEST(PrefabOverride, AddUpdateRemoveAndBaseComparison) {
    const PrefabModel source = SourceModel();
    std::vector<PrefabOverrideRecord> overrides;

    std::string error;
    ASSERT_TRUE(AddOrUpdatePrefabOverride(
        source, &overrides,
        Record(1, "Transform", "position.x", 4.0), &error)) << error;
    ASSERT_EQ(overrides.size(), 1u);
    EXPECT_EQ(overrides[0].value.get<double>(), 4.0);

    ASSERT_TRUE(AddOrUpdatePrefabOverride(
        source, &overrides,
        Record(1, "Transform", "position.x", 5.0), &error)) << error;
    ASSERT_EQ(overrides.size(), 1u);
    EXPECT_EQ(overrides[0].value.get<double>(), 5.0);

    ASSERT_TRUE(AddOrUpdatePrefabOverride(
        source, &overrides,
        Record(2, "Mesh", "meshGuid",
               "11111111111111111111111111111111"), &error)) << error;
    ASSERT_EQ(overrides.size(), 2u);

    ASSERT_TRUE(AddOrUpdatePrefabOverride(
        source, &overrides,
        Record(1, "Transform", "position.x", 1.0), &error)) << error;
    ASSERT_EQ(overrides.size(), 1u);

    bool matches = false;
    ASSERT_TRUE(PrefabOverrideMatchesBase(
        source, Record(2, "Mesh", "meshGuid",
                       "fedcba9876543210fedcba9876543210"),
        &matches, &error)) << error;
    EXPECT_TRUE(matches);
    EXPECT_TRUE(RemovePrefabOverride(&overrides, 2, "Mesh", "meshGuid"));
    EXPECT_TRUE(overrides.empty());

    EXPECT_FALSE(AddOrUpdatePrefabOverride(
        source, &overrides,
        Record(2, "Skinning", "visible", true), &error));
    EXPECT_FALSE(AddOrUpdatePrefabOverride(
        source, &overrides,
        Record(999, "Transform", "position.x", 1.0), &error));
}

TEST(PrefabOverride, SceneV2PersistsSourceIdentityAndOverrides) {
    Scene scene;
    auto root = std::make_shared<GameObject>("Root");
    auto child = std::make_shared<GameObject>("Child");
    scene.AddObject(root);
    std::string addError;
    ASSERT_TRUE(scene.AddEntity(child, root->GetSceneEntityId(), &addError))
        << addError;

    PrefabInstanceMetadata metadata;
    metadata.prefabGuid = Guid("0123456789abcdef0123456789abcdef");
    metadata.sourceRevision = 2;
    metadata.sourceDigest = "0123456789abcdef";
    metadata.sourcePath = "assets/prefabs/test.0123456789abcdef.prefab.json";
    metadata.syncState = PrefabSyncState::Overrides;
    metadata.localToScene[1] = root->GetSceneEntityId();
    metadata.localToScene[2] = child->GetSceneEntityId();
    metadata.overrides.push_back(
        Record(1, "Transform", "position.x", 9.0));
    ASSERT_TRUE(scene.SetPrefabInstanceMetadata(
        root->GetSceneEntityId(), metadata, &addError)) << addError;

    const auto captured = CaptureSceneById(scene, {});
    ASSERT_EQ(captured.entities.size(), 2u);
    const auto& serialized = captured.entities[0].prefabInstance;
    ASSERT_TRUE(serialized.has_value());
    EXPECT_EQ(serialized->sourceDigest, metadata.sourceDigest);
    EXPECT_EQ(serialized->sourcePath, metadata.sourcePath);
    EXPECT_EQ(serialized->overrides.size(), 1u);

    const auto json = SerializeSnapshot(captured);
    SceneSnapshot loaded;
    std::string error;
    ASSERT_TRUE(DeserializeSnapshot(json, loaded, error)) << error;
    ASSERT_TRUE(loaded.entities[0].prefabInstance.has_value());
    EXPECT_EQ(loaded.entities[0].prefabInstance->overrides.size(), 1u);

    StubGraphicsFactory gfx;
    TextureManager textures(gfx);
    ContentRegistry registry;
    Scene reopened;
    const auto result = InstantiateScene(loaded, reopened, textures, registry);
    ASSERT_TRUE(result.ok);
    PrefabInstanceMetadata restored;
    ASSERT_TRUE(reopened.GetPrefabInstanceMetadata(
        root->GetSceneEntityId(), &restored));
    EXPECT_EQ(restored.sourceDigest, metadata.sourceDigest);
    EXPECT_EQ(restored.sourceRevision, 2);
    ASSERT_EQ(restored.overrides.size(), 1u);
    EXPECT_EQ(restored.overrides[0].field, "position.x");
    EXPECT_EQ(restored.syncState, PrefabSyncState::Overrides);
}

TEST(PrefabOverride, LegacyG5MetadataIsExplicitlyMarked) {
    Scene scene;
    auto root = std::make_shared<GameObject>("LegacyRoot");
    scene.AddObject(root);

    PrefabInstanceMetadata metadata;
    metadata.prefabGuid = Guid("0123456789abcdef0123456789abcdef");
    metadata.sourceRevision = 1;
    metadata.legacySourceIdentity = true;
    metadata.localToScene[1] = root->GetSceneEntityId();
    std::string error;
    ASSERT_TRUE(scene.SetPrefabInstanceMetadata(
        root->GetSceneEntityId(), metadata, &error)) << error;

    const auto captured = CaptureSceneById(scene, {});
    ASSERT_TRUE(captured.entities[0].prefabInstance.has_value());
    EXPECT_TRUE(captured.entities[0].prefabInstance->legacySourceIdentity);
    EXPECT_TRUE(captured.entities[0].prefabInstance->sourceDigest.empty());
    EXPECT_TRUE(captured.entities[0].prefabInstance->sourcePath.empty());
}