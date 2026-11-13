#include <gtest/gtest.h>

#include "Engine/Core/Content/PrefabSerialization.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace Engine;
using namespace Engine::Content;

namespace {

ResourceGUID Guid(const char* hex) {
    return ResourceGUID::FromHex(hex);
}

PrefabComponentData Component(const std::string& type,
                              nlohmann::json data) {
    return PrefabComponentData{type, std::move(data)};
}

PrefabModel BasicModel() {
    PrefabModel model;
    model.prefabGuid = Guid("0123456789abcdef0123456789abcdef");
    model.rootLocalId = 1;

    PrefabEntity root;
    root.localId = 1;
    root.name = "Root";
    root.localTransform.position = Vec3(1.f, 2.f, 3.f);
    root.localTransform.scale = Vec3(2.f, 2.f, 2.f);
    root.components.push_back(Component(
        "Mesh", {{"meshGuid", "fedcba9876543210fedcba9876543210"}}));
    model.entities.push_back(std::move(root));

    PrefabEntity child;
    child.localId = 2;
    child.name = "Child";
    child.parentLocalId = 1;
    child.localTransform.position = Vec3(0.f, 4.f, 0.f);
    model.entities.push_back(std::move(child));
    return model;
}

PrefabModel AllComponentsModel() {
    PrefabModel model = BasicModel();
    model.entities[0].components = {
        Component("Camera", {{"enabled", true},
                             {"zoom", 1.0},
                             {"viewport", ""},
                             {"bounds", ""}}),
        Component("Collider", {{"enabled", true},
                               {"shape", "Circle"},
                               {"radius", 0.5},
                               {"halfX", 0.5},
                               {"halfY", 0.5},
                               {"isSensor", false},
                               {"category", 1},
                               {"mask", 65535}}),
        Component("Mesh", {{"meshGuid",
                            "fedcba9876543210fedcba9876543210"}}),
        Component("RigidBody", {{"motionType", "Dynamic"},
                                {"linearDamping", 0.0},
                                {"angularDamping", 0.0}}),
        Component("Skinning", {{"visible", true}}),
    };
    return model;
}

nlohmann::json JsonOf(const PrefabModel& model) {
    auto written = WritePrefab(model);
    EXPECT_TRUE(written.ok) << written.error;
    return nlohmann::json::parse(written.bytes);
}

PrefabReadResult ReadJson(const nlohmann::json& doc) {
    return ReadPrefab(doc.dump());
}

} // namespace

TEST(PrefabSerialization, WriterIsDeterministicAndDigestIsStable) {
    const PrefabModel model = AllComponentsModel();
    auto first = WritePrefab(model);
    ASSERT_TRUE(first.ok) << first.error;
    auto second = WritePrefab(model);
    ASSERT_TRUE(second.ok) << second.error;
    EXPECT_EQ(first.bytes, second.bytes);
    EXPECT_EQ(first.digest, second.digest);
    EXPECT_EQ(first.digest.size(), 16u);
    EXPECT_EQ(first.digest, ComputePrefabDigest(first.bytes));
}

TEST(PrefabSerialization, RoundTripPreservesAllPersistedFields) {
    PrefabModel model = AllComponentsModel();
    model.entities[0].components.push_back(Component(
        "Mesh", {{"meshGuid", "11111111111111111111111111111111"}}));
    // Duplicate Mesh type is invalid; remove the earlier Mesh before writing.
    model.entities[0].components.erase(
        std::remove_if(model.entities[0].components.begin(),
                       model.entities[0].components.end(),
                       [](const PrefabComponentData& c) { return c.type == "Mesh"; }),
        model.entities[0].components.end());
    model.entities[0].components.push_back(Component(
        "Mesh", {{"meshGuid", "11111111111111111111111111111111"}}));
    std::sort(model.entities[0].components.begin(),
              model.entities[0].components.end(),
              [](const PrefabComponentData& a, const PrefabComponentData& b) {
                  return a.type < b.type;
              });

    auto written = WritePrefab(model);
    ASSERT_TRUE(written.ok) << written.error;
    auto read = ReadPrefab(written.bytes);
    ASSERT_TRUE(read.ok) << read.error;
    EXPECT_TRUE(PrefabModelsEqual(model, read.model));
    EXPECT_EQ(WritePrefab(read.model).bytes, written.bytes);
}

TEST(PrefabSerialization, WriterNormalizesEntityAndComponentOrdering) {
    PrefabModel sorted = AllComponentsModel();
    PrefabModel reversed = sorted;
    std::reverse(reversed.entities.begin(), reversed.entities.end());
    std::reverse(reversed.entities[0].components.begin(),
                 reversed.entities[0].components.end());
    std::reverse(reversed.entities[1].components.begin(),
                 reversed.entities[1].components.end());

    auto a = WritePrefab(sorted);
    auto b = WritePrefab(reversed);
    ASSERT_TRUE(a.ok) << a.error;
    ASSERT_TRUE(b.ok) << b.error;
    EXPECT_EQ(a.bytes, b.bytes);
    EXPECT_EQ(a.digest, b.digest);
}

TEST(PrefabSerialization, PreservesUint64BeyondDoublePrecision) {
    PrefabModel model;
    model.prefabGuid = Guid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    model.rootLocalId = (1ull << 63) + 7;
    PrefabEntity root;
    root.localId = model.rootLocalId;
    root.name = "HugeId";
    model.entities.push_back(root);

    auto written = WritePrefab(model);
    ASSERT_TRUE(written.ok) << written.error;
    auto read = ReadPrefab(written.bytes);
    ASSERT_TRUE(read.ok) << read.error;
    EXPECT_EQ(read.model.rootLocalId, model.rootLocalId);
    ASSERT_EQ(read.model.entities.size(), 1u);
    EXPECT_EQ(read.model.entities[0].localId, model.rootLocalId);
}

TEST(PrefabSerialization, DigestChangesForEverySemanticField) {
    const PrefabModel base = AllComponentsModel();
    auto original = WritePrefab(base);
    ASSERT_TRUE(original.ok) << original.error;

    auto checkChanged = [&](PrefabModel changed) {
        auto result = WritePrefab(changed);
        ASSERT_TRUE(result.ok) << result.error;
        EXPECT_NE(result.bytes, original.bytes);
        EXPECT_NE(result.digest, original.digest);
    };

    PrefabModel name = base;
    name.entities[0].name = "Renamed";
    checkChanged(name);

    PrefabModel parent = base;
    parent.entities.push_back(PrefabEntity{3, "Third", 1, {}, {}});
    parent.entities[1].parentLocalId = 3;
    checkChanged(parent);

    PrefabModel transform = base;
    transform.entities[1].localTransform.position.x = 9.f;
    checkChanged(transform);

    PrefabModel component = base;
    component.entities[0].components[0].data["enabled"] = false;
    checkChanged(component);

    PrefabModel mesh = base;
    for (auto& item : mesh.entities[0].components)
        if (item.type == "Mesh")
            item.data["meshGuid"] = "22222222222222222222222222222222";
    checkChanged(mesh);

    PrefabModel guid = base;
    guid.prefabGuid = Guid("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    checkChanged(guid);
}

TEST(PrefabSerialization, ReaderRejectsInvalidJsonAndTrailingData) {
    EXPECT_FALSE(ReadPrefab("").ok);
    EXPECT_FALSE(ReadPrefab("{}").ok);
    EXPECT_FALSE(ReadPrefab("not json").ok);

    auto written = WritePrefab(AllComponentsModel());
    ASSERT_TRUE(written.ok) << written.error;
    EXPECT_FALSE(ReadPrefab(written.bytes.substr(0, written.bytes.size() - 5)).ok);
    EXPECT_FALSE(ReadPrefab(written.bytes + " trailing").ok);
}

TEST(PrefabSerialization, ReaderRejectsDuplicateObjectKeys) {
    auto written = WritePrefab(BasicModel());
    ASSERT_TRUE(written.ok) << written.error;
    std::string duplicate = written.bytes;
    const auto versionPos = duplicate.find("\"formatVersion\": 1");
    ASSERT_NE(versionPos, std::string::npos);
    duplicate.insert(versionPos, "\"formatVersion\": 1,\n  ");
    EXPECT_FALSE(ReadPrefab(duplicate).ok);

    duplicate = written.bytes;
    const auto namePos = duplicate.find("\"name\": \"Root\"");
    ASSERT_NE(namePos, std::string::npos);
    duplicate.insert(namePos, "\"name\": \"RootCopy\",\n          ");
    EXPECT_FALSE(ReadPrefab(duplicate).ok);
}

TEST(PrefabSerialization, ReaderRejectsUnknownAndMissingFields) {
    auto doc = JsonOf(BasicModel());
    doc["entities"][0]["unknown"] = 1;
    EXPECT_FALSE(ReadJson(doc).ok);

    doc = JsonOf(BasicModel());
    doc.erase("rootLocalId");
    EXPECT_FALSE(ReadJson(doc).ok);

    doc = JsonOf(BasicModel());
    doc["entities"][0].erase("name");
    EXPECT_FALSE(ReadJson(doc).ok);

    doc = JsonOf(BasicModel());
    doc["entities"][0]["components"][0]["data"]["unknown"] = 1;
    EXPECT_FALSE(ReadJson(doc).ok);
}

TEST(PrefabSerialization, ReaderRejectsBadGuidsAndVersions) {
    auto doc = JsonOf(BasicModel());
    doc["formatVersion"] = 2;
    EXPECT_FALSE(ReadJson(doc).ok);

    doc = JsonOf(BasicModel());
    doc["prefabGuid"] = "00000000000000000000000000000000";
    EXPECT_FALSE(ReadJson(doc).ok);

    doc = JsonOf(BasicModel());
    doc["prefabGuid"] = "ABCDEFABCDEFABCDEFABCDEFABCDEFAB";
    EXPECT_FALSE(ReadJson(doc).ok);

    auto written = WritePrefab(BasicModel());
    ASSERT_TRUE(written.ok) << written.error;
    const ResourceGUID expected = Guid("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    EXPECT_FALSE(ReadPrefab(written.bytes, &expected).ok);
}

TEST(PrefabSerialization, ReaderRejectsHierarchyAndTransformViolations) {
    auto doc = JsonOf(BasicModel());
    doc["entities"][1]["parentLocalId"] = nullptr;
    EXPECT_FALSE(ReadJson(doc).ok);  // multiple roots

    doc = JsonOf(BasicModel());
    doc["entities"][1]["parentLocalId"] = 999;
    EXPECT_FALSE(ReadJson(doc).ok);  // missing parent

    doc = JsonOf(BasicModel());
    doc["entities"][1]["parentLocalId"] = 2;
    EXPECT_FALSE(ReadJson(doc).ok);  // self parent

    doc = JsonOf(BasicModel());
    doc["entities"][0]["parentLocalId"] = 2;
    doc["entities"][1]["parentLocalId"] = 1;
    EXPECT_FALSE(ReadJson(doc).ok);  // cycle / no root

    doc = JsonOf(BasicModel());
    doc["entities"][1]["localId"] = 0;
    EXPECT_FALSE(ReadJson(doc).ok);

    doc = JsonOf(BasicModel());
    doc["entities"][1]["localId"] = -1;
    EXPECT_FALSE(ReadJson(doc).ok);

    doc = JsonOf(BasicModel());
    doc["entities"][1]["localId"] = 1.5;
    EXPECT_FALSE(ReadJson(doc).ok);

    doc = JsonOf(BasicModel());
    doc["entities"][0]["localTransform"]["position"] = {1, 2};
    EXPECT_FALSE(ReadJson(doc).ok);

    doc = JsonOf(BasicModel());
    doc["entities"][0]["localTransform"]["scale"][0] = "NaN";
    EXPECT_FALSE(ReadJson(doc).ok);
}

TEST(PrefabSerialization, ReaderRejectsInvalidComponentSchemas) {
    auto doc = JsonOf(AllComponentsModel());
    doc["entities"][0]["components"][0]["type"] = "Unknown";
    EXPECT_FALSE(ReadJson(doc).ok);

    doc = JsonOf(AllComponentsModel());
    doc["entities"][0]["components"][0]["data"]["zoom"] = 0;
    EXPECT_FALSE(ReadJson(doc).ok);  // Camera

    doc = JsonOf(AllComponentsModel());
    doc["entities"][0]["components"][1]["data"]["shape"] = "Triangle";
    EXPECT_FALSE(ReadJson(doc).ok);  // Collider

    doc = JsonOf(AllComponentsModel());
    doc["entities"][0]["components"][3]["data"]["motionType"] = "Flying";
    EXPECT_FALSE(ReadJson(doc).ok);  // RigidBody

    doc = JsonOf(AllComponentsModel());
    doc["entities"][0]["components"][4]["data"]["visible"] = "yes";
    EXPECT_FALSE(ReadJson(doc).ok);  // Skinning

    auto duplicate = AllComponentsModel();
    duplicate.entities[0].components.push_back(
        Component("Camera", {{"enabled", true},
                             {"zoom", 1.0},
                             {"viewport", ""},
                             {"bounds", ""}}));
    EXPECT_FALSE(WritePrefab(duplicate).ok);
}

TEST(PrefabSerialization, ReaderRejectsNestedSkinningViolations) {
    PrefabModel model = BasicModel();
    nlohmann::json skin;
    skin["visible"] = true;
    nlohmann::json bone;
    bone["name"] = "root";
    bone["parent"] = -1;
    bone["bind"] = nlohmann::json::array({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
    skin["skeleton"] = nlohmann::json::array({bone});
    skin["skinnedMesh"] = {{"boneMapping", nlohmann::json::array()},
                           {"vertices", nlohmann::json::array()},
                           {"indices", nlohmann::json::array()}};
    skin["animation"] = {{"duration", 0}, {"localTime", 0}, {"playing", false},
                         {"timeScale", 1}, {"tracks", nlohmann::json::array()}};
    model.entities[0].components = {Component("Skinning", skin)};
    auto written = WritePrefab(model);
    ASSERT_TRUE(written.ok) << written.error;
    auto doc = nlohmann::json::parse(written.bytes);
    doc["entities"][0]["components"][0]["data"]["skeleton"][0]["bind"] = {1, 2};
    EXPECT_FALSE(ReadJson(doc).ok);

    doc = nlohmann::json::parse(written.bytes);
    doc["entities"][0]["components"][0]["data"]["skeleton"][0]["extra"] = 1;
    EXPECT_FALSE(ReadJson(doc).ok);

    doc = nlohmann::json::parse(written.bytes);
    doc["entities"][0]["components"][0]["data"]["animation"]["tracks"] = {
        {{"type", 0}, {"keys", nlohmann::json::array()}}};
    EXPECT_FALSE(ReadJson(doc).ok);
}

TEST(PrefabSerialization, WriterRejectsInvalidModelsWithoutBytes) {
    PrefabModel model = BasicModel();
    model.prefabGuid = ResourceGUID::Null;
    auto result = WritePrefab(model);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.bytes.empty());
    EXPECT_TRUE(result.digest.empty());

    model = BasicModel();
    model.entities[1].localId = 1;
    EXPECT_FALSE(WritePrefab(model).ok);
}

TEST(PrefabSerialization, ReaderReturnsEmptyModelOnFailure) {
    auto result = ReadPrefab("not json");
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.model.entities.empty());
    EXPECT_FALSE(result.error.empty());
}
