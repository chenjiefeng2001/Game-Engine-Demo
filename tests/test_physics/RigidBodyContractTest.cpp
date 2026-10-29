/**
 * @file RigidBodyContractTest.cpp
 * @brief T-1-A RigidBody 契约门禁
 *
 * 覆盖：默认值、稳定身份、反射往返、非法值拒绝、组件生命周期、
 * 字段归属（不重复 Collider/Transform 属性）、以及三种缺失语义下
 * 不产生隐式 Physics 绑定。
 */

#include <gtest/gtest.h>

#include "Engine/Core/GameObject/RigidBodyComponent.h"
#include "Engine/Core/GameObject/ColliderComponent.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/ComponentRegistry_Go.h"
#include "Engine/Core/Physics/PhysicsColliderAdapter.h"
#include "Engine/Box2D/Box2DPhysicsWorld.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace Engine {
namespace {

RigidBodyComponent* AddRigidBody(GameObject& go) {
    return static_cast<RigidBodyComponent*>(go.AddComponentByName("RigidBody"));
}

ColliderComponent* AddCollider(GameObject& go) {
    return static_cast<ColliderComponent*>(go.AddComponentByName("Collider"));
}

std::vector<std::string> PropertyNames(const Component& comp) {
    std::vector<std::string> names;
    for (size_t i = 0; i < comp.GetPropertyCount(); ++i) {
        ComponentPropertyDesc desc;
        if (comp.GetPropertyDesc(i, &desc) && desc.name)
            names.emplace_back(desc.name);
    }
    return names;
}

} // namespace

TEST(T1A_RigidBodyContract, DefaultsAndStableIdentity) {
    EXPECT_TRUE(ComponentRegistryGo::IsRegistered("RigidBody"));
    auto created = ComponentRegistryGo::Create("RigidBody");
    ASSERT_NE(created, nullptr);

    GameObject go("rb_defaults");
    auto* rb = AddRigidBody(go);
    ASSERT_NE(rb, nullptr);
    EXPECT_STREQ(rb->GetComponentTypeName(), "RigidBody");
    EXPECT_STREQ(rb->GetTypeDisplayName(), "RigidBody");
    EXPECT_EQ(go.GetComponentByName("RigidBody"), rb);
    EXPECT_TRUE(go.HasComponentByName("RigidBody"));

    EXPECT_EQ(rb->GetMotionType(), RigidBodyMotionType::Dynamic);
    EXPECT_FLOAT_EQ(rb->GetLinearDamping(), 0.0f);
    EXPECT_FLOAT_EQ(rb->GetAngularDamping(), 0.0f);
    EXPECT_EQ(rb->GetPropertyCount(), 3u);
}

TEST(T1A_RigidBodyContract, MotionTypeNamesRoundTrip) {
    EXPECT_STREQ(RigidBodyMotionTypeToString(RigidBodyMotionType::Static), "Static");
    EXPECT_STREQ(RigidBodyMotionTypeToString(RigidBodyMotionType::Kinematic), "Kinematic");
    EXPECT_STREQ(RigidBodyMotionTypeToString(RigidBodyMotionType::Dynamic), "Dynamic");

    RigidBodyMotionType parsed = RigidBodyMotionType::Dynamic;
    ASSERT_TRUE(ParseRigidBodyMotionType("Static", &parsed));
    EXPECT_EQ(parsed, RigidBodyMotionType::Static);
    ASSERT_TRUE(ParseRigidBodyMotionType("Kinematic", &parsed));
    EXPECT_EQ(parsed, RigidBodyMotionType::Kinematic);
    ASSERT_TRUE(ParseRigidBodyMotionType("Dynamic", &parsed));
    EXPECT_EQ(parsed, RigidBodyMotionType::Dynamic);

    EXPECT_FALSE(ParseRigidBodyMotionType(nullptr, &parsed));
    EXPECT_FALSE(ParseRigidBodyMotionType("Static", nullptr));
    EXPECT_FALSE(ParseRigidBodyMotionType("static", &parsed));
    EXPECT_FALSE(ParseRigidBodyMotionType("Flying", &parsed));
}

TEST(T1A_RigidBodyContract, ReflectionRoundTrip) {
    GameObject go("rb_reflection");
    auto* rb = AddRigidBody(go);
    ASSERT_NE(rb, nullptr);

    ComponentPropertyDesc desc;
    ASSERT_TRUE(rb->GetPropertyDesc(0, &desc));
    EXPECT_STREQ(desc.name, "motionType");
    EXPECT_EQ(desc.type, ComponentValueType::String);
    EXPECT_TRUE(desc.editable);

    ComponentPropertyValue value;
    value.type = ComponentValueType::String;
    value.stringValue = "Kinematic";
    EXPECT_TRUE(rb->SetPropertyValue(0, value));
    EXPECT_EQ(rb->GetMotionType(), RigidBodyMotionType::Kinematic);

    ComponentPropertyValue read;
    ASSERT_TRUE(rb->GetPropertyValue(0, &read));
    EXPECT_EQ(read.type, ComponentValueType::String);
    EXPECT_EQ(read.stringValue, "Kinematic");

    value.type = ComponentValueType::Float;
    value.floatValue = 0.25f;
    EXPECT_TRUE(rb->SetPropertyValue(1, value));
    EXPECT_FLOAT_EQ(rb->GetLinearDamping(), 0.25f);
    ASSERT_TRUE(rb->GetPropertyValue(1, &read));
    EXPECT_EQ(read.type, ComponentValueType::Float);
    EXPECT_FLOAT_EQ(read.floatValue, 0.25f);

    value.floatValue = 1.5f;
    EXPECT_TRUE(rb->SetPropertyValue(2, value));
    EXPECT_FLOAT_EQ(rb->GetAngularDamping(), 1.5f);
    ASSERT_TRUE(rb->GetPropertyValue(2, &read));
    EXPECT_FLOAT_EQ(read.floatValue, 1.5f);

    EXPECT_FALSE(rb->GetPropertyDesc(3, &desc));
    EXPECT_FALSE(rb->GetPropertyValue(3, &read));
    EXPECT_FALSE(rb->SetPropertyValue(3, value));
}

TEST(T1A_RigidBodyContract, InvalidValuesAreRejectedWithoutMutation) {
    GameObject go("rb_invalid");
    auto* rb = AddRigidBody(go);
    ASSERT_NE(rb, nullptr);
    ASSERT_TRUE(rb->SetMotionType(RigidBodyMotionType::Kinematic));
    ASSERT_TRUE(rb->SetLinearDamping(0.5f));
    ASSERT_TRUE(rb->SetAngularDamping(0.75f));

    // 直接 setter：未知枚举 / 负数 / NaN / 无穷 → false，原值不变
    EXPECT_FALSE(rb->SetMotionType(static_cast<RigidBodyMotionType>(99)));
    EXPECT_EQ(rb->GetMotionType(), RigidBodyMotionType::Kinematic);

    EXPECT_FALSE(rb->SetLinearDamping(-0.01f));
    EXPECT_FALSE(rb->SetLinearDamping(std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(rb->SetLinearDamping(std::numeric_limits<float>::infinity()));
    EXPECT_FLOAT_EQ(rb->GetLinearDamping(), 0.5f);

    EXPECT_FALSE(rb->SetAngularDamping(-1.0f));
    EXPECT_FALSE(rb->SetAngularDamping(std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(rb->SetAngularDamping(std::numeric_limits<float>::infinity()));
    EXPECT_FLOAT_EQ(rb->GetAngularDamping(), 0.75f);

    // 反射：类型不符 / 未知运动类型 / 非法阻尼 → false，原值不变
    ComponentPropertyValue value;
    value.type = ComponentValueType::Float;
    value.floatValue = 1.0f;
    EXPECT_FALSE(rb->SetPropertyValue(0, value));

    value.type = ComponentValueType::String;
    value.stringValue = "Flying";
    EXPECT_FALSE(rb->SetPropertyValue(0, value));
    EXPECT_EQ(rb->GetMotionType(), RigidBodyMotionType::Kinematic);

    value.type = ComponentValueType::Int;
    value.intValue = 1;
    EXPECT_FALSE(rb->SetPropertyValue(1, value));
    EXPECT_FLOAT_EQ(rb->GetLinearDamping(), 0.5f);

    value.type = ComponentValueType::Float;
    value.floatValue = -0.25f;
    EXPECT_FALSE(rb->SetPropertyValue(2, value));
    EXPECT_FLOAT_EQ(rb->GetAngularDamping(), 0.75f);
}

TEST(T1A_RigidBodyContract, LifecycleAndSingleInstance) {
    GameObject go("rb_lifecycle");
    int attached = 0;
    int detached = 0;
    const uint32 listenerId = go.AddComponentLifecycleListener(
        [&](Component& comp, bool isAttached) {
            if (dynamic_cast<RigidBodyComponent*>(&comp) == nullptr) return;
            if (isAttached) ++attached;
            else ++detached;
        });

    auto* first = AddRigidBody(go);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(attached, 1);
    EXPECT_EQ(detached, 0);

    // 单实例语义：重复 Add 返回既有实例，不触发第二次挂载
    EXPECT_EQ(AddRigidBody(go), first);
    EXPECT_EQ(attached, 1);

    EXPECT_TRUE(go.RemoveComponentByName("RigidBody"));
    EXPECT_EQ(attached, 1);
    EXPECT_EQ(detached, 1);
    EXPECT_EQ(go.GetComponentByName("RigidBody"), nullptr);

    auto* second = AddRigidBody(go);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(attached, 2);
    EXPECT_TRUE(go.RemoveComponentByName("RigidBody"));

    go.RemoveComponentLifecycleListener(listenerId);
}

TEST(T1A_RigidBodyContract, FieldOwnershipNoDuplicateStorage) {
    GameObject go("rb_ownership");
    auto* rb = AddRigidBody(go);
    auto* collider = AddCollider(go);
    ASSERT_NE(rb, nullptr);
    ASSERT_NE(collider, nullptr);

    const std::vector<std::string> names = PropertyNames(*rb);
    EXPECT_EQ(names, (std::vector<std::string>{
                         "motionType", "linearDamping", "angularDamping"}));

    // RigidBody 不重复 Collider 的几何/接触属性，也不重复 Transform 状态
    const char* forbidden[] = {
        "shape", "radius", "halfX", "halfY", "isSensor", "category", "mask",
        "friction", "restitution", "mass", "enabled",
        "position", "rotation", "x", "y"};
    for (const char* name : forbidden) {
        EXPECT_EQ(std::find(names.begin(), names.end(), name), names.end())
            << "RigidBody must not duplicate property: " << name;
    }

    // Collider 契约保持不变（8 个属性）
    EXPECT_EQ(collider->GetPropertyCount(), 8u);
    EXPECT_EQ(PropertyNames(*collider).size(), 8u);
}

TEST(T1A_RigidBodyContract, MissingSemanticsProduceNoImplicitBody) {
    // ① Collider-only：沿用 F2-B 静态碰撞区域语义
    {
        auto world = std::make_shared<Box2DPhysicsWorld>(Vec2(0.0f, -9.81f));
        PhysicsColliderAdapter adapter;
        adapter.SetWorld(world);
        auto go = std::make_shared<GameObject>("collider_only");
        auto* collider = AddCollider(*go);
        ASSERT_NE(collider, nullptr);
        adapter.ObserveGameObject(*go);
        EXPECT_EQ(adapter.GetBodyCount(), 1u);
        EXPECT_EQ(PhysicsColliderAdapter::BuildBodyDef(*collider).type,
                  BodyType::Static);
        adapter.UnobserveGameObject(*go);
    }

    // ② RigidBody-only：没有几何 → 不创建任何 body
    {
        auto world = std::make_shared<Box2DPhysicsWorld>(Vec2(0.0f, -9.81f));
        PhysicsColliderAdapter adapter;
        adapter.SetWorld(world);
        auto go = std::make_shared<GameObject>("rigidbody_only");
        auto* rb = AddRigidBody(*go);
        ASSERT_NE(rb, nullptr);
        ASSERT_TRUE(rb->SetMotionType(RigidBodyMotionType::Dynamic));
        adapter.ObserveGameObject(*go);
        EXPECT_EQ(adapter.GetBodyCount(), 0u);
        EXPECT_EQ(adapter.GetTrackedCount(), 0u);
        adapter.UnobserveGameObject(*go);
    }

    // ③ RigidBody + Collider：T-1-A 不改变 Adapter 的既有静态映射
    //    （显式动态绑定属于 T-1-C；不得由缺失/存在 RigidBody 隐式切换）
    {
        auto world = std::make_shared<Box2DPhysicsWorld>(Vec2(0.0f, -9.81f));
        PhysicsColliderAdapter adapter;
        adapter.SetWorld(world);
        auto go = std::make_shared<GameObject>("rigidbody_and_collider");
        auto* collider = AddCollider(*go);
        auto* rb = AddRigidBody(*go);
        ASSERT_NE(collider, nullptr);
        ASSERT_NE(rb, nullptr);
        ASSERT_TRUE(rb->SetMotionType(RigidBodyMotionType::Dynamic));
        adapter.ObserveGameObject(*go);
        EXPECT_EQ(adapter.GetBodyCount(), 1u);
        EXPECT_EQ(PhysicsColliderAdapter::BuildBodyDef(*collider).type,
                  BodyType::Static);
        adapter.UnobserveGameObject(*go);
    }
}

} // namespace Engine
