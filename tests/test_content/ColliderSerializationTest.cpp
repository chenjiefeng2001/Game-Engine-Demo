/**
 * @file ColliderSerializationTest.cpp
 * @brief F2-S1 Collider 序列化 / 冷启语义门禁
 *
 * 覆盖：八字段 Serialize、字段级宽容反序列化、缺失/非法/未知键、
 * 非对象拒绝、未知 shape 回退，以及真实场景加载时的无 DataBad 行为。
 * 不覆盖物理绑定或物理行为；那些由 F2ColliderPhysics B5/B6 承载。
 */

#include <gtest/gtest.h>

#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/GameObject/ColliderComponent.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/Scene/Scene.h"
#include "StubGraphicsFactory.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

using namespace Engine;
using namespace Engine::Content;

namespace {

const std::string kDir = "f2s1_scratch";

std::string P(const std::string& name) {
    std::filesystem::create_directories(kDir);
    return kDir + "/" + name;
}

ColliderComponent* AddCollider(Scene& scene, const char* name) {
    auto go = std::make_shared<GameObject>(name);
    auto* collider = static_cast<ColliderComponent*>(go->AddComponentByName("Collider"));
    scene.AddObject(go);
    return collider;
}

struct Instantiated {
    std::unique_ptr<Scene> scene;
    LoadResult result;
};

Instantiated Instantiate(const SceneSnapshot& snap) {
    auto scene = std::make_unique<Scene>();
    StubGraphicsFactory gfx;
    TextureManager texMgr(gfx);
    ContentRegistry registry;
    LoadResult result = InstantiateScene(snap, *scene, texMgr, registry);
    return {std::move(scene), std::move(result)};
}

void SetAllFields(ColliderComponent& c) {
    c.SetEnabled(false);
    c.SetShape(ColliderShape::Box);
    c.SetRadius(1.75f);
    c.SetHalfExtents(2.5f, 1.25f);
    c.SetIsSensor(true);
    c.SetCategory(0x0004);
    c.SetMask(0x0031);
}

void ExpectAllFields(const ColliderComponent& c) {
    EXPECT_FALSE(c.IsEnabled());
    EXPECT_EQ(c.GetShape(), ColliderShape::Box);
    EXPECT_FLOAT_EQ(c.GetRadius(), 1.75f);
    EXPECT_FLOAT_EQ(c.GetHalfExtentsX(), 2.5f);
    EXPECT_FLOAT_EQ(c.GetHalfExtentsY(), 1.25f);
    EXPECT_TRUE(c.IsSensor());
    EXPECT_EQ(c.GetCategory(), 0x0004u);
    EXPECT_EQ(c.GetMask(), 0x0031u);
}

} // namespace

TEST(F2S1_ColliderSerialization, SerializeWritesAllEightFieldsWithoutRuntimeState) {
    ColliderComponent c;
    SetAllFields(c);

    nlohmann::json json;
    c.Serialize(json);

    ASSERT_EQ(json.size(), 8u);
    EXPECT_EQ(json["enabled"].get<bool>(), false);
    EXPECT_EQ(json["shape"].get<std::string>(), "Box");
    EXPECT_FLOAT_EQ(json["radius"].get<float>(), 1.75f);
    EXPECT_FLOAT_EQ(json["halfX"].get<float>(), 2.5f);
    EXPECT_FLOAT_EQ(json["halfY"].get<float>(), 1.25f);
    EXPECT_EQ(json["isSensor"].get<bool>(), true);
    EXPECT_EQ(json["category"].get<int>(), 0x0004);
    EXPECT_EQ(json["mask"].get<int>(), 0x0031);

    EXPECT_FALSE(json.contains("BodyID"));
    EXPECT_FALSE(json.contains("bodyID"));
    EXPECT_FALSE(json.contains("runtimeBodyID"));
    EXPECT_FALSE(json.contains("ShapeRef"));
}

TEST(F2S1_ColliderSerialization, JsonRoundTripAllEightFields) {
    ColliderComponent source;
    SetAllFields(source);

    nlohmann::json json;
    source.Serialize(json);

    ColliderComponent restored;
    ASSERT_TRUE(restored.Deserialize(json));
    ExpectAllFields(restored);
}

TEST(F2S1_ColliderSerialization, MissingFieldsKeepCurrentValues) {
    ColliderComponent c;
    c.SetEnabled(false);
    c.SetShape(ColliderShape::Box);
    c.SetHalfExtents(4.0f, 3.0f);
    c.SetIsSensor(true);
    c.SetCategory(9);
    c.SetMask(7);

    ASSERT_TRUE(c.Deserialize({{"radius", 2.5}}));

    EXPECT_FALSE(c.IsEnabled());
    EXPECT_EQ(c.GetShape(), ColliderShape::Box);
    EXPECT_FLOAT_EQ(c.GetRadius(), 2.5f);
    EXPECT_FLOAT_EQ(c.GetHalfExtentsX(), 4.0f);
    EXPECT_FLOAT_EQ(c.GetHalfExtentsY(), 3.0f);
    EXPECT_TRUE(c.IsSensor());
    EXPECT_EQ(c.GetCategory(), 9u);
    EXPECT_EQ(c.GetMask(), 7u);
}

TEST(F2S1_ColliderSerialization, InvalidKnownFieldsAreIgnoredFieldLocally) {
    ColliderComponent c;
    c.SetShape(ColliderShape::Box);
    c.SetRadius(1.0f);
    c.SetHalfExtents(2.0f, 3.0f);
    c.SetCategory(4);
    c.SetMask(8);

    ASSERT_TRUE(c.Deserialize({
        {"enabled", "not-bool"},
        {"shape", "Triangle"},
        {"radius", "oops"},
        {"halfX", 5.0},
        {"halfY", false},
        {"isSensor", 1},
        {"category", 0xFFFFF},
        {"mask", 12},
    }));

    // Invalid fields keep current values; valid fields are applied in the same call.
    EXPECT_TRUE(c.IsEnabled());
    EXPECT_EQ(c.GetShape(), ColliderShape::Box);
    EXPECT_FLOAT_EQ(c.GetRadius(), 1.0f);
    EXPECT_FLOAT_EQ(c.GetHalfExtentsX(), 5.0f);
    EXPECT_FLOAT_EQ(c.GetHalfExtentsY(), 3.0f);
    EXPECT_FALSE(c.IsSensor());
    EXPECT_EQ(c.GetCategory(), 0xFFFFu);   // uint16 narrowing
    EXPECT_EQ(c.GetMask(), 12u);
}

TEST(F2S1_ColliderSerialization, UnknownShapeKeepsCurrentShape) {
    ColliderComponent box;
    box.SetShape(ColliderShape::Box);
    ASSERT_TRUE(box.Deserialize({{"shape", "Triangle"}}));
    EXPECT_EQ(box.GetShape(), ColliderShape::Box);

    ColliderComponent fresh;
    ASSERT_TRUE(fresh.Deserialize({{"shape", "Triangle"}}));
    EXPECT_EQ(fresh.GetShape(), ColliderShape::Circle);
}

TEST(F2S1_ColliderSerialization, UnknownKeysAreIgnored) {
    ColliderComponent c;
    ASSERT_TRUE(c.Deserialize({
        {"shape", "Box"},
        {"futureField", 42},
        {"nested", {{"x", 1}}},
    }));
    EXPECT_EQ(c.GetShape(), ColliderShape::Box);

    nlohmann::json output;
    c.Serialize(output);
    EXPECT_EQ(output.size(), 8u);
    EXPECT_FALSE(output.contains("futureField"));
    EXPECT_FALSE(output.contains("nested"));
}

TEST(F2S1_ColliderSerialization, NonObjectDataIsRejected) {
    ColliderComponent c;
    c.SetShape(ColliderShape::Box);
    c.SetRadius(2.0f);

    EXPECT_FALSE(c.Deserialize(nlohmann::json::array()));
    EXPECT_FALSE(c.Deserialize(nlohmann::json()));
    EXPECT_EQ(c.GetShape(), ColliderShape::Box);
    EXPECT_FLOAT_EQ(c.GetRadius(), 2.0f);
}

TEST(F2S1_ColliderSerialization, InvalidKnownFieldDoesNotProduceDataBad) {
    SceneSnapshot snap;
    SerializedEntity entity;
    entity.name = "BadCollider";
    SerializedComponent component;
    component.type = "Collider";
    component.data = {{"shape", "Triangle"}, {"radius", "oops"}, {"halfX", 2.0}};
    entity.components.push_back(component);
    snap.entities.push_back(entity);

    Instantiated inst = Instantiate(snap);
    ASSERT_TRUE(inst.result.ok);
    EXPECT_TRUE(inst.result.warnings.empty());
    ASSERT_EQ(inst.scene->GetObjectCount(), 1u);

    auto* collider = static_cast<ColliderComponent*>(
        inst.scene->GetObjects()[0]->GetComponentByName("Collider"));
    ASSERT_NE(collider, nullptr);
    EXPECT_EQ(collider->GetShape(), ColliderShape::Circle);
    EXPECT_FLOAT_EQ(collider->GetRadius(), 0.5f);
    EXPECT_FLOAT_EQ(collider->GetHalfExtentsX(), 2.0f);
}

TEST(F2S1_ColliderSerialization, InvalidShapeSceneLoadKeepsDefaultCircle) {
    Scene scene;
    auto* collider = AddCollider(scene, "BadShape");
    ASSERT_NE(collider, nullptr);
    collider->SetShape(ColliderShape::Box);
    collider->SetHalfExtents(2.0f, 1.0f);

    const std::string path = P("invalid_shape.scene");
    ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(scene, {}), path));

    std::string text;
    {
        std::ifstream f(path, std::ios::binary);
        ASSERT_TRUE(f.good());
        text.assign(std::istreambuf_iterator<char>(f),
                    std::istreambuf_iterator<char>());
    }
    const std::string needle = "\"shape\": \"Box\"";
    const auto pos = text.find(needle);
    ASSERT_NE(pos, std::string::npos);
    text.replace(pos, needle.size(), "\"shape\": \"Triangle\"");
    {
        std::ofstream f(path, std::ios::trunc);
        ASSERT_TRUE(f.good());
        f << text;
    }

    SceneSnapshot loaded;
    std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile(path, loaded, err)) << err;
    Instantiated inst = Instantiate(loaded);
    ASSERT_TRUE(inst.result.ok);
    EXPECT_TRUE(inst.result.warnings.empty());
    ASSERT_EQ(inst.scene->GetObjectCount(), 1u);

    auto* restored = static_cast<ColliderComponent*>(
        inst.scene->GetObjects()[0]->GetComponentByName("Collider"));
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->GetShape(), ColliderShape::Circle);
    EXPECT_FLOAT_EQ(restored->GetRadius(), 0.5f);
    EXPECT_FLOAT_EQ(restored->GetHalfExtentsX(), 2.0f);
    EXPECT_FLOAT_EQ(restored->GetHalfExtentsY(), 1.0f);
}
