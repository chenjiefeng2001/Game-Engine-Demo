#include <gtest/gtest.h>

#include "Engine/Core/Content/PrefabReconciliation.h"

#include <string>
#include <unordered_map>
#include <vector>

using namespace Engine;
using namespace Engine::Content;

namespace {

ResourceGUID Guid(const char* hex) {
    return ResourceGUID::FromHex(hex);
}

PrefabEntity Entity(uint64_t id, const std::string& name,
                    std::optional<uint64_t> parent = std::nullopt) {
    PrefabEntity entity;
    entity.localId = id;
    entity.name = name;
    entity.parentLocalId = parent;
    return entity;
}

PrefabLocalEntitySnapshot LocalEntity(uint64_t id, const std::string& name,
                                      std::optional<uint64_t> parent = std::nullopt) {
    PrefabLocalEntitySnapshot entity;
    entity.localId = id;
    entity.sceneEntityId = id + 100;
    entity.name = name;
    entity.parentLocalId = parent;
    return entity;
}

PrefabModel Model(const char* guid, const std::string& name,
                  float x = 1.0f) {
    PrefabModel model;
    model.prefabGuid = Guid(guid);
    model.rootLocalId = 1;
    auto root = Entity(1, name);
    root.localTransform.position.x = x;
    root.components.push_back(PrefabComponentData{
        "Mesh", {{"meshGuid", "fedcba9876543210fedcba9876543210"}}});
    model.entities.push_back(root);
    model.entities.push_back(Entity(2, "Child", 1));
    return model;
}

PrefabSourceSnapshot Source(const PrefabModel& model, const std::string& path,
                            const std::string& digest) {
    PrefabSourceSnapshot source;
    source.prefabGuid = model.prefabGuid;
    source.projectRevision = 1;
    source.contentDigest = digest;
    source.contentPath = path;
    source.model = model;
    return source;
}

PrefabLocalInstanceSnapshot Local(const PrefabModel& model) {
    PrefabLocalInstanceSnapshot local;
    local.rootEntityId = 1;
    local.localToScene = {{1, 101}, {2, 102}};
    for (const auto& source : model.entities) {
        auto entity = LocalEntity(source.localId, source.name,
                                  source.parentLocalId);
        entity.localTransform = source.localTransform;
        for (const auto& component : source.components) {
            entity.components.push_back(
                PrefabLocalComponentSnapshot{component.type, component.data, true});
        }
        local.entities.push_back(std::move(entity));
    }
    local.syncState = PrefabSyncState::Clean;
    return local;
}

} // namespace

TEST(PrefabReconciliation, FieldAutoMergeKeepLocalAndConflict) {
    PrefabModel baseModel = Model("0123456789abcdef0123456789abcdef", "Root", 1.0f);
    PrefabModel latestModel = baseModel;
    latestModel.entities[0].localTransform.position.x = 2.0f;
    PrefabLocalInstanceSnapshot local = Local(baseModel);
    local.entities[0].localTransform.position.x = 1.0f;
    auto plan = BuildPrefabReconciliation(
        Source(baseModel, "assets/prefabs/a.0011223344556677.prefab.json",
               "0011223344556677"),
        Source(latestModel, "assets/prefabs/a.8899aabbccddeeff.prefab.json",
               "8899aabbccddeeff"), local);
    ASSERT_FALSE(plan.hasConflicts);
    ASSERT_FALSE(plan.fields.empty());
    const auto& field = plan.fields[0];
    EXPECT_EQ(field.component, "Transform");
    EXPECT_EQ(field.field, "position.x");
    EXPECT_EQ(field.decision, PrefabReconciliationDecision::TakeLatest);

    PrefabLocalInstanceSnapshot keep = local;
    keep.entities[0].localTransform.position.x = 3.0f;
    plan = BuildPrefabReconciliation(
        Source(baseModel, "assets/prefabs/a.0011223344556677.prefab.json",
               "0011223344556677"),
        Source(baseModel, "assets/prefabs/a.0011223344556677.prefab.json",
               "0011223344556677"), keep);
    EXPECT_FALSE(plan.hasConflicts);
    EXPECT_EQ(plan.fields[0].decision, PrefabReconciliationDecision::KeepLocal);

    PrefabLocalInstanceSnapshot conflict = local;
    conflict.entities[0].localTransform.position.x = 3.0f;
    plan = BuildPrefabReconciliation(
        Source(baseModel, "assets/prefabs/a.0011223344556677.prefab.json",
               "0011223344556677"),
        Source(latestModel, "assets/prefabs/a.8899aabbccddeeff.prefab.json",
               "8899aabbccddeeff"), conflict);
    EXPECT_TRUE(plan.hasConflicts);
    ASSERT_FALSE(plan.conflicts.empty());
    EXPECT_EQ(plan.conflicts[0].type, PrefabConflictType::Field);
    EXPECT_EQ(plan.conflicts[0].id, "field:1:Transform:position.x");
}

TEST(PrefabReconciliation, EntityNameParentAndComponentStructure) {
    PrefabModel base = Model("0123456789abcdef0123456789abcdef", "Root");
    PrefabModel latest = base;
    latest.entities[0].name = "RenamedRoot";
    base.entities.push_back(Entity(4, "Sibling"));
    latest.entities[1].parentLocalId = 3;
    latest.entities[0].components.push_back(PrefabComponentData{
        "Camera", {{"enabled", true}, {"zoom", 2.0}, {"viewport", ""}, {"bounds", ""}}});
    latest.entities.push_back(Entity(3, "NewChild", 1));
    PrefabLocalInstanceSnapshot local = Local(base);
    local.entities[0].name = "LocalRoot";
    local.entities[1].parentLocalId = 4;
    auto plan = BuildPrefabReconciliation(
        Source(base, "assets/prefabs/a.0011223344556677.prefab.json",
               "0011223344556677"),
        Source(latest, "assets/prefabs/a.8899aabbccddeeff.prefab.json",
               "8899aabbccddeeff"), local);

    EXPECT_TRUE(plan.hasConflicts);
    bool nameConflict = false;
    bool parentConflict = false;
    for (const auto& conflict : plan.conflicts) {
        if (conflict.type == PrefabConflictType::EntityName) nameConflict = true;
        if (conflict.type == PrefabConflictType::EntityParent) parentConflict = true;
    }
    EXPECT_TRUE(nameConflict);
    EXPECT_TRUE(parentConflict);
    bool addedEntity = false;
    bool addedComponent = false;
    for (const auto& change : plan.structural) {
        if (change.kind == PrefabStructuralChangeKind::AddEntity &&
            change.localId == 3 && change.name == "NewChild")
            addedEntity = true;
        if (change.kind == PrefabStructuralChangeKind::AddComponent &&
            change.localId == 1 && change.component == "Camera")
            addedComponent = true;
    }
    EXPECT_TRUE(addedEntity);
    EXPECT_TRUE(addedComponent);
}

TEST(PrefabReconciliation, DeterministicAndNoMutation) {
    PrefabModel base = Model("0123456789abcdef0123456789abcdef", "Root");
    PrefabModel latest = base;
    latest.entities[0].localTransform.position.x = 3.0f;
    PrefabLocalInstanceSnapshot local = Local(base);
    local.entities[0].localTransform.position.x = 2.0f;
    const PrefabModel baseCopy = base;
    const PrefabModel latestCopy = latest;
    const PrefabLocalInstanceSnapshot localCopy = local;

    auto first = BuildPrefabReconciliation(
        Source(base, "assets/prefabs/a.0011223344556677.prefab.json",
               "0011223344556677"),
        Source(latest, "assets/prefabs/a.8899aabbccddeeff.prefab.json",
               "8899aabbccddeeff"), local);
    auto second = BuildPrefabReconciliation(
        Source(base, "assets/prefabs/a.0011223344556677.prefab.json",
               "0011223344556677"),
        Source(latest, "assets/prefabs/a.8899aabbccddeeff.prefab.json",
               "8899aabbccddeeff"), local);
    ASSERT_EQ(first.fields.size(), second.fields.size());
    ASSERT_EQ(first.conflicts.size(), second.conflicts.size());
    EXPECT_EQ(first.fields[0].field, second.fields[0].field);
    EXPECT_EQ(first.fields[0].decision, second.fields[0].decision);
    EXPECT_EQ(first.conflicts[0].id, second.conflicts[0].id);
    EXPECT_EQ(base.entities[0].localTransform.position.x, baseCopy.entities[0].localTransform.position.x);
    EXPECT_EQ(latest.entities[0].localTransform.position.x, latestCopy.entities[0].localTransform.position.x);
    EXPECT_EQ(local.entities[0].localTransform.position.x, localCopy.entities[0].localTransform.position.x);
}

TEST(PrefabReconciliation, OverrideRebaseKeepsLocalAndRemovesMatchingLatest) {
    PrefabModel base = Model("0123456789abcdef0123456789abcdef", "Root", 1.0f);
    PrefabLocalInstanceSnapshot local = Local(base);
    local.entities[0].localTransform.position.x = 3.0f;
    local.overrides.push_back(
        PrefabOverrideRecord{1, "Transform", "position.x", 3.0});

    auto plan = BuildPrefabReconciliation(
        Source(base, "assets/prefabs/a.0011223344556677.prefab.json",
               "0011223344556677"),
        Source(base, "assets/prefabs/a.0011223344556677.prefab.json",
               "0011223344556677"), local);
    EXPECT_FALSE(plan.hasConflicts);
    ASSERT_EQ(plan.overrides.size(), 1u);
    EXPECT_EQ(plan.overrides[0].decision, PrefabReconciliationDecision::KeepLocal);
    EXPECT_EQ(plan.overrides[0].latestBase, 1.0);

    PrefabModel latest = base;
    latest.entities[0].localTransform.position.x = 2.0f;
    local.entities[0].localTransform.position.x = 2.0f;
    local.overrides[0].value = 2.0;
    plan = BuildPrefabReconciliation(
        Source(base, "assets/prefabs/a.0011223344556677.prefab.json",
               "0011223344556677"),
        Source(latest, "assets/prefabs/a.8899aabbccddeeff.prefab.json",
               "8899aabbccddeeff"), local);
    EXPECT_FALSE(plan.hasConflicts);
    ASSERT_EQ(plan.overrides.size(), 1u);
    EXPECT_EQ(plan.overrides[0].decision, PrefabReconciliationDecision::TakeLatest);
    EXPECT_EQ(plan.overrides[0].latestBase, 2.0);

    local.entities[0].localTransform.position.x = 3.0f;
    local.overrides[0].value = 3.0;
    plan = BuildPrefabReconciliation(
        Source(base, "assets/prefabs/a.0011223344556677.prefab.json",
               "0011223344556677"),
        Source(latest, "assets/prefabs/a.8899aabbccddeeff.prefab.json",
               "8899aabbccddeeff"), local);
    EXPECT_TRUE(plan.hasConflicts);
    ASSERT_EQ(plan.overrides.size(), 1u);
    EXPECT_EQ(plan.overrides[0].decision, PrefabReconciliationDecision::Conflict);
    EXPECT_EQ(plan.overrides[0].latestBase, 2.0);
}

TEST(PrefabReconciliation, ComponentSetAndMissingEntityFailClosed) {
    PrefabModel base = Model("0123456789abcdef0123456789abcdef", "Root");
    PrefabModel latest = base;
    latest.entities[0].components.push_back(PrefabComponentData{
        "Camera", {{"enabled", true}, {"zoom", 2.0}, {"viewport", ""}, {"bounds", ""}}});
    PrefabLocalInstanceSnapshot local = Local(base);
    local.entities[0].components.push_back(PrefabLocalComponentSnapshot{
        "Camera", {{"enabled", false}, {"zoom", 3.0}, {"viewport", ""}, {"bounds", ""}},
        true});
    auto plan = BuildPrefabReconciliation(
        Source(base, "assets/prefabs/a.0011223344556677.prefab.json",
               "0011223344556677"),
        Source(latest, "assets/prefabs/a.8899aabbccddeeff.prefab.json",
               "8899aabbccddeeff"), local);
    EXPECT_TRUE(plan.hasConflicts);
    EXPECT_EQ(plan.conflicts[0].id, "component:1:Camera");
    EXPECT_EQ(plan.conflicts[0].type, PrefabConflictType::ComponentSet);

    PrefabLocalInstanceSnapshot missing = Local(base);
    missing.entities.pop_back();
    missing.localToScene.erase(2);
    plan = BuildPrefabReconciliation(
        Source(base, "assets/prefabs/a.0011223344556677.prefab.json",
               "0011223344556677"),
        Source(base, "assets/prefabs/a.0011223344556677.prefab.json",
               "0011223344556677"), missing);
    EXPECT_TRUE(plan.hasConflicts);
    EXPECT_EQ(plan.conflicts[0].id, "entity:missing:2");
    EXPECT_EQ(plan.conflicts[0].type, PrefabConflictType::EntityMissing);
}

TEST(PrefabReconciliation, ConflictStateRoundTripRejectsUnknownType) {
    PrefabReconciliationState state;
    state.latestSourceDigest = "8899aabbccddeeff";
    state.latestSourcePath = "assets/prefabs/a.8899aabbccddeeff.prefab.json";
    PrefabReconciliationConflict conflict;
    conflict.id = "field:1:Transform:position.x";
    conflict.type = PrefabConflictType::Field;
    conflict.localId = 1;
    conflict.component = "Transform";
    conflict.field = "position.x";
    conflict.base = 1.0;
    conflict.latest = 2.0;
    conflict.local = 3.0;
    conflict.detail = "both changed";
    state.conflicts.push_back(conflict);
    std::string error;
    EXPECT_TRUE(ValidatePrefabReconciliationState(state, &error)) << error;

    auto json = PrefabReconciliationStateToJson(state);
    PrefabReconciliationState restored;
    EXPECT_TRUE(PrefabReconciliationStateFromJson(json, &restored, &error))
        << error;
    ASSERT_EQ(restored.conflicts.size(), 1u);
    EXPECT_EQ(restored.conflicts[0].id, conflict.id);
    EXPECT_EQ(restored.conflicts[0].type, PrefabConflictType::Field);
    EXPECT_EQ(restored.conflicts[0].component, "Transform");
    EXPECT_EQ(restored.conflicts[0].field, "position.x");
    EXPECT_EQ(restored.latestSourceDigest, state.latestSourceDigest);

    json["conflicts"][0]["type"] = "future_conflict";
    PrefabReconciliationState rejected;
    EXPECT_FALSE(PrefabReconciliationStateFromJson(json, &rejected, &error));
    EXPECT_FALSE(error.empty());
}

TEST(PrefabReconciliation, EntityDeletionAndExtraLocalFailClosed) {
    PrefabModel base = Model("0123456789abcdef0123456789abcdef", "Root");
    PrefabModel latest = base;
    latest.entities.pop_back();
    PrefabLocalInstanceSnapshot local = Local(base);
    auto plan = BuildPrefabReconciliation(
        Source(base, "assets/prefabs/a.0011223344556677.prefab.json",
               "0011223344556677"),
        Source(latest, "assets/prefabs/a.8899aabbccddeeff.prefab.json",
               "8899aabbccddeeff"), local);
    EXPECT_FALSE(plan.hasConflicts);
    bool deleted = false;
    for (const auto& change : plan.structural)
        if (change.kind == PrefabStructuralChangeKind::DeleteEntity &&
            change.localId == 2) deleted = true;
    EXPECT_TRUE(deleted);

    local.overrides.push_back(
        PrefabOverrideRecord{2, "Transform", "position.x", 2.0});
    plan = BuildPrefabReconciliation(
        Source(base, "assets/prefabs/a.0011223344556677.prefab.json",
               "0011223344556677"),
        Source(latest, "assets/prefabs/a.8899aabbccddeeff.prefab.json",
               "8899aabbccddeeff"), local);
    EXPECT_TRUE(plan.hasConflicts);
    EXPECT_EQ(plan.conflicts[0].type, PrefabConflictType::EntityDelete);
}