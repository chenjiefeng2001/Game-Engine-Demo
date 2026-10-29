/**
 * @file RigidBodySerializationTest.cpp
 * @brief T-1-B RigidBody 序列化 / 冷启恢复门禁
 *
 * 覆盖：三字段 schema、真实 SceneSerializerV1 往返、文件冷启、
 * 缺失/空 data、非法字段的原子拒绝、未知字段忽略、多实体与 Collider 共存。
 * 不覆盖物理绑定或物理行为（T-1-C）。
 */

#include <gtest/gtest.h>

#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/GameObject/ColliderComponent.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/RigidBodyComponent.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/Scene/Scene.h"
#include "StubGraphicsFactory.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>

using namespace Engine;
using namespace Engine::Content;

namespace {

const std::string kDir = "t1b_scratch";

std::string P(const std::string& name) {
    std::filesystem::create_directories(kDir);
    return kDir + "/" + name;
}

RigidBodyComponent* AddRigidBody(Scene& scene, const char* name) {
    auto go = std::make_shared<GameObject>(name);
    auto* rb = static_cast<RigidBodyComponent*>(go->AddComponentByName("RigidBody"));
    scene.AddObject(go);
    return rb;
}

ColliderComponent* AddCollider(GameObject& go) {
    return static_cast<ColliderComponent*>(go.AddComponentByName("Collider"));
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

const nlohmann::json& RigidBodyData(const SceneSnapshot& snap,
                                    const std::string& entityName) {
    for (const auto& entity : snap.entities) {
        if (entity.name != entityName) continue;
        for (const auto& component : entity.components) {
            if (component.type == "RigidBody") return component.data;
        }
    }
    static const nlohmann::json empty = nlohmann::json::object();
    return empty;
}

void ExpectValues(const RigidBodyComponent& rb, RigidBodyMotionType motion,
                  float linear, float angular) {
    EXPECT_EQ(rb.GetMotionType(), motion);
    EXPECT_FLOAT_EQ(rb.GetLinearDamping(), linear);
    EXPECT_FLOAT_EQ(rb.GetAngularDamping(), angular);
}

} // namespace

TEST(T1B_RigidBodySerialization, SerializeWritesOnlyFrozenFields) {
    Scene scene;
    auto* rb = AddRigidBody(scene, "RB");
    ASSERT_NE(rb, nullptr);
    ASSERT_TRUE(rb->SetMotionType(RigidBodyMotionType::Kinematic));
    ASSERT_TRUE(rb->SetLinearDamping(0.25f));
    ASSERT_TRUE(rb->SetAngularDamping(0.5f));

    const SceneSnapshot snap = CaptureScene(scene, {});
    const nlohmann::json& data = RigidBodyData(snap, "RB");
    ASSERT_TRUE(data.is_object());
    ASSERT_EQ(data.size(), 3u);
    EXPECT_EQ(data["motionType"], "Kinematic");
    EXPECT_FLOAT_EQ(data["linearDamping"].get<float>(), 0.25f);
    EXPECT_FLOAT_EQ(data["angularDamping"].get<float>(), 0.5f);

    EXPECT_FALSE(data.contains("enabled"));
    EXPECT_FALSE(data.contains("bodyID"));
    EXPECT_FALSE(data.contains("runtimeBodyID"));
    EXPECT_FALSE(data.contains("world"));

    const std::string text = SerializeSnapshot(snap).dump();
    EXPECT_EQ(text.find("runtimeBodyID"), std::string::npos);
    EXPECT_EQ(text.find("bodyID"), std::string::npos);
}

TEST(T1B_RigidBodySerialization, JsonRoundTrip) {
    RigidBodyComponent rb;
    const nlohmann::json input = {
        {"motionType", "Static"},
        {"linearDamping", 0.25},
        {"angularDamping", 1.5},
    };
    ASSERT_TRUE(rb.Deserialize(input));
    ExpectValues(rb, RigidBodyMotionType::Static, 0.25f, 1.5f);

    nlohmann::json output;
    rb.Serialize(output);
    EXPECT_EQ(output, input);
}

TEST(T1B_RigidBodySerialization, MissingFieldsKeepCurrentValues) {
    RigidBodyComponent rb;
    ASSERT_TRUE(rb.Deserialize(nlohmann::json::object()));
    ExpectValues(rb, RigidBodyMotionType::Dynamic, 0.0f, 0.0f);

    ASSERT_TRUE(rb.Deserialize({{"linearDamping", 0.4}}));
    ExpectValues(rb, RigidBodyMotionType::Dynamic, 0.4f, 0.0f);

    ASSERT_TRUE(rb.SetMotionType(RigidBodyMotionType::Static));
    ASSERT_TRUE(rb.Deserialize({{"angularDamping", 0.2}}));
    ExpectValues(rb, RigidBodyMotionType::Static, 0.4f, 0.2f);
}

TEST(T1B_RigidBodySerialization, InvalidKnownFieldsAreAtomic) {
    RigidBodyComponent rb;
    ASSERT_TRUE(rb.SetMotionType(RigidBodyMotionType::Kinematic));
    ASSERT_TRUE(rb.SetLinearDamping(0.5f));
    ASSERT_TRUE(rb.SetAngularDamping(0.75f));

    const auto expect_unchanged = [&]() {
        ExpectValues(rb, RigidBodyMotionType::Kinematic, 0.5f, 0.75f);
    };

    EXPECT_FALSE(rb.Deserialize({
        {"motionType", "Flying"}, {"linearDamping", 0.1}}));
    expect_unchanged();

    EXPECT_FALSE(rb.Deserialize({
        {"motionType", "Static"}, {"linearDamping", -1.0}}));
    expect_unchanged();

    EXPECT_FALSE(rb.Deserialize({
        {"motionType", "Static"}, {"angularDamping", 1e300}}));
    expect_unchanged();

    EXPECT_FALSE(rb.Deserialize({{"motionType", 123}}));
    expect_unchanged();

    EXPECT_FALSE(rb.Deserialize({{"linearDamping", "0.5"}}));
    expect_unchanged();

    EXPECT_FALSE(rb.Deserialize({
        {"linearDamping", std::numeric_limits<double>::quiet_NaN()}}));
    expect_unchanged();

    EXPECT_FALSE(rb.Deserialize({
        {"angularDamping", std::numeric_limits<double>::infinity()}}));
    expect_unchanged();
}

TEST(T1B_RigidBodySerialization, UnknownKeysAreIgnored) {
    RigidBodyComponent rb;
    ASSERT_TRUE(rb.Deserialize({
        {"motionType", "Static"},
        {"futureField", 42},
        {"nested", {{"x", 1}}},
    }));
    ExpectValues(rb, RigidBodyMotionType::Static, 0.0f, 0.0f);

    nlohmann::json output;
    rb.Serialize(output);
    EXPECT_EQ(output.size(), 3u);
    EXPECT_FALSE(output.contains("futureField"));
}

TEST(T1B_RigidBodySerialization, NonObjectDataIsRejected) {
    RigidBodyComponent rb;
    EXPECT_FALSE(rb.Deserialize(nlohmann::json::array()));
    EXPECT_FALSE(rb.Deserialize(nlohmann::json()));
    ExpectValues(rb, RigidBodyMotionType::Dynamic, 0.0f, 0.0f);
}

TEST(T1B_RigidBodySerialization, SaveLoadColdStartRestoresFields) {
    Scene scene;
    auto* a = AddRigidBody(scene, "A");
    auto* b = AddRigidBody(scene, "B");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_TRUE(a->SetMotionType(RigidBodyMotionType::Static));
    ASSERT_TRUE(a->SetLinearDamping(0.1f));
    ASSERT_TRUE(a->SetAngularDamping(0.2f));
    ASSERT_TRUE(b->SetMotionType(RigidBodyMotionType::Kinematic));
    ASSERT_TRUE(b->SetLinearDamping(0.3f));
    ASSERT_TRUE(b->SetAngularDamping(0.4f));

    auto* collider = AddCollider(*b->GetOwner());
    ASSERT_NE(collider, nullptr);
    collider->SetShape(ColliderShape::Box);
    collider->SetHalfExtents(2.0f, 1.0f);

    const std::string path = P("cold_start.scene");
    const SceneSnapshot saved = CaptureScene(scene, {});
    ASSERT_TRUE(SaveSnapshotToFile(saved, path));

    SceneSnapshot loaded;
    std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile(path, loaded, err)) << err;
    Instantiated inst = Instantiate(loaded);
    ASSERT_TRUE(inst.result.ok);
    ASSERT_TRUE(inst.result.warnings.empty());
    ASSERT_EQ(inst.scene->GetObjectCount(), 2u);

    auto* loadedA = static_cast<RigidBodyComponent*>(
        inst.scene->GetObjects()[0]->GetComponentByName("RigidBody"));
    auto* loadedB = static_cast<RigidBodyComponent*>(
        inst.scene->GetObjects()[1]->GetComponentByName("RigidBody"));
    ASSERT_NE(loadedA, nullptr);
    ASSERT_NE(loadedB, nullptr);
    ExpectValues(*loadedA, RigidBodyMotionType::Static, 0.1f, 0.2f);
    ExpectValues(*loadedB, RigidBodyMotionType::Kinematic, 0.3f, 0.4f);

    auto* loadedCollider = static_cast<ColliderComponent*>(
        inst.scene->GetObjects()[1]->GetComponentByName("Collider"));
    ASSERT_NE(loadedCollider, nullptr);
    EXPECT_EQ(loadedCollider->GetShape(), ColliderShape::Box);
    EXPECT_FLOAT_EQ(loadedCollider->GetHalfExtentsX(), 2.0f);
    EXPECT_FLOAT_EQ(loadedCollider->GetHalfExtentsY(), 1.0f);
    EXPECT_EQ(loadedCollider->GetPropertyCount(), 8u);
}

TEST(T1B_RigidBodySerialization, T1AEmptyDataLoadsDefaults) {
    SceneSnapshot snap;
    SerializedEntity entity;
    entity.name = "Legacy";
    SerializedComponent component;
    component.type = "RigidBody";
    component.data = nlohmann::json::object();
    entity.components.push_back(component);
    snap.entities.push_back(entity);

    Instantiated inst = Instantiate(snap);
    ASSERT_TRUE(inst.result.ok);
    EXPECT_TRUE(inst.result.warnings.empty());
    auto* rb = static_cast<RigidBodyComponent*>(
        inst.scene->GetObjects()[0]->GetComponentByName("RigidBody"));
    ASSERT_NE(rb, nullptr);
    ExpectValues(*rb, RigidBodyMotionType::Dynamic, 0.0f, 0.0f);
}

TEST(T1B_RigidBodySerialization, InvalidDataWarnsAndKeepsDefaults) {
    SceneSnapshot snap;
    SerializedEntity entity;
    entity.name = "BadData";
    SerializedComponent component;
    component.type = "RigidBody";
    component.data = {{"motionType", "Flying"}, {"linearDamping", 0.4}};
    entity.components.push_back(component);
    snap.entities.push_back(entity);

    Instantiated inst = Instantiate(snap);
    ASSERT_TRUE(inst.result.ok);
    ASSERT_EQ(inst.result.warnings.size(), 1u);
    EXPECT_NE(inst.result.warnings[0].find("DataBad"), std::string::npos);
    auto* rb = static_cast<RigidBodyComponent*>(
        inst.scene->GetObjects()[0]->GetComponentByName("RigidBody"));
    ASSERT_NE(rb, nullptr);
    ExpectValues(*rb, RigidBodyMotionType::Dynamic, 0.0f, 0.0f);
}

TEST(T1B_RigidBodySerialization, RepeatedSaveLoadIsStable) {
    Scene scene;
    auto* rb = AddRigidBody(scene, "Stable");
    ASSERT_NE(rb, nullptr);
    ASSERT_TRUE(rb->SetMotionType(RigidBodyMotionType::Static));
    ASSERT_TRUE(rb->SetLinearDamping(0.2f));
    ASSERT_TRUE(rb->SetAngularDamping(0.3f));

    std::string path = P("repeat.scene");
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(scene, {}), path));
        SceneSnapshot loaded;
        std::string err;
        ASSERT_TRUE(LoadSnapshotFromFile(path, loaded, err)) << err;
        Instantiated inst = Instantiate(loaded);
        ASSERT_TRUE(inst.result.ok);
        ASSERT_EQ(inst.scene->GetObjectCount(), 1u);
        auto* restored = static_cast<RigidBodyComponent*>(
            inst.scene->GetObjects()[0]->GetComponentByName("RigidBody"));
        ASSERT_NE(restored, nullptr);
        ExpectValues(*restored, RigidBodyMotionType::Static, 0.2f, 0.3f);
    }
}
