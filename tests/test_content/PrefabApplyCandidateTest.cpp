#include <gtest/gtest.h>

#include "Engine/Core/Content/PrefabApplyCandidate.h"
#include "Engine/Core/Content/PrefabSerialization.h"
#include "Engine/Core/Content/PrefabOverrideModel.h"

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
    root.components.push_back(PrefabComponentData{
        "Mesh", {{"meshGuid", "fedcba9876543210fedcba9876543210"}}});
    root.components.push_back(PrefabComponentData{
        "Camera",
        {{"enabled", true}, {"zoom", 1.0}, {"viewport", ""}, {"bounds", ""}}});
    model.entities.push_back(root);

    PrefabEntity child;
    child.localId = 2;
    child.name = "Child";
    child.parentLocalId = 1;
    child.localTransform.position = Vec3(0.f, 4.f, 0.f);
    model.entities.push_back(child);
    return model;
}

std::unordered_map<uint64_t, uint64_t> Map() {
    return {{1, 11}, {2, 12}};
}

PrefabOverrideRecord Record(uint64_t localId, const char* component,
                            const char* field, nlohmann::json value) {
    return PrefabOverrideRecord{localId, component, field, std::move(value)};
}

} // namespace

TEST(PrefabApplyCandidate, AppliesAllowedFieldsAndPreservesIdentity) {
    const PrefabModel source = SourceModel();
    std::vector<PrefabOverrideRecord> overrides = {
        Record(1, "Transform", "position.x", 8.0),
        Record(1, "Mesh", "meshGuid", "11111111111111111111111111111111"),
        Record(1, "Camera", "zoom", 2.0),
        Record(2, "Transform", "position.y", 9.0),
    };
    auto candidate = BuildPrefabApplyCandidate(
        source, overrides, Map(), source.prefabGuid, 4);
    ASSERT_TRUE(candidate.ok) << candidate.error;
    ASSERT_EQ(candidate.model.entities.size(), source.entities.size());
    EXPECT_EQ(candidate.model.prefabGuid, source.prefabGuid);
    EXPECT_EQ(candidate.model.rootLocalId, source.rootLocalId);
    EXPECT_EQ(candidate.model.entities[0].localId, 1u);
    EXPECT_EQ(candidate.model.entities[1].localId, 2u);
    EXPECT_EQ(candidate.model.entities[0].name, "Root");
    EXPECT_EQ(candidate.model.entities[1].parentLocalId, 1u);
    EXPECT_FLOAT_EQ(candidate.model.entities[0].localTransform.position.x, 8.f);
    EXPECT_FLOAT_EQ(candidate.model.entities[1].localTransform.position.y, 9.f);
    ASSERT_EQ(candidate.model.entities[0].components.size(), 2u);
    EXPECT_EQ(candidate.model.entities[0].components[0].data["meshGuid"],
              "11111111111111111111111111111111");
    EXPECT_EQ(candidate.model.entities[0].components[1].data["zoom"], 2.0);
    EXPECT_EQ(candidate.model.entities[0].components[1].data["enabled"], true);

    const auto written = WritePrefab(candidate.model);
    ASSERT_TRUE(written.ok) << written.error;
    EXPECT_EQ(candidate.bytes, written.bytes);
    EXPECT_EQ(candidate.digest, written.digest);
}

TEST(PrefabApplyCandidate, RejectsInvalidIdentityMappingAndOverrides) {
    const PrefabModel source = SourceModel();
    auto fail = [&](const std::vector<PrefabOverrideRecord>& overrides,
                    const std::unordered_map<uint64_t, uint64_t>& map,
                    const ResourceGUID& guid, int64_t revision,
                    const std::string& fragment) {
        auto candidate = BuildPrefabApplyCandidate(
            source, overrides, map, guid, revision);
        EXPECT_FALSE(candidate.ok);
        EXPECT_TRUE(candidate.bytes.empty());
        EXPECT_TRUE(candidate.digest.empty());
        EXPECT_NE(candidate.error.find(fragment), std::string::npos)
            << "fragment=" << fragment << " error=" << candidate.error;
    };

    fail({Record(1, "Transform", "position.x", 8.0)}, Map(),
         Guid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), 1, "GUID");
    fail({Record(1, "Transform", "position.x", 8.0)}, Map(),
         source.prefabGuid, -1, "revision");
    fail({Record(1, "Transform", "position.x", 8.0)}, {{1, 11}},
         source.prefabGuid, 1, "mapping count");
    fail({Record(1, "Transform", "position.x", 8.0)},
         {{1, 11}, {2, 11}}, source.prefabGuid, 1, "Scene ID");
    fail({Record(1, "Transform", "position.x", 8.0)},
         {{1, 11}, {99, 13}}, source.prefabGuid, 1, "unmapped");
    fail({Record(2, "Skinning", "visible", true)}, Map(),
         source.prefabGuid, 1, "unsupported Prefab override field");
    fail({Record(1, "Transform", "position.x", 1.0)}, Map(),
         source.prefabGuid, 1, "base value");
    fail({Record(1, "Transform", "position.x", 8.0),
          Record(1, "Transform", "position.x", 9.0)}, Map(),
         source.prefabGuid, 1, "duplicate identity");
    fail({Record(1, "Mesh", "meshGuid", "not-a-guid")}, Map(),
         source.prefabGuid, 1, "GUID");
    fail({Record(1, "Camera", "zoom", 0.0)}, Map(),
         source.prefabGuid, 1, "greater than zero");

}

TEST(PrefabApplyCandidate, SourceAndCandidateRemainUnchangedOnFailure) {
    const PrefabModel source = SourceModel();
    auto candidate = BuildPrefabApplyCandidate(
        source, {Record(1, "Skinning", "visible", true)}, Map(),
        source.prefabGuid, 1);
    EXPECT_FALSE(candidate.ok);
    EXPECT_TRUE(candidate.model.entities.empty());
    EXPECT_TRUE(candidate.bytes.empty());
    EXPECT_TRUE(candidate.digest.empty());
    EXPECT_EQ(source.entities[0].localTransform.position.x, 1.f);
    EXPECT_EQ(source.entities[0].components[0].data["meshGuid"],
              "fedcba9876543210fedcba9876543210");
}