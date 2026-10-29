/**
 * @file PhysicsRuntime2DTest.cpp
 * @brief T-1-C GameObject 2D 物理运行时门禁
 *
 * 使用真实 Box2D World，覆盖：动态刚体步进/碰撞、Collider-only 静态、
 * RigidBody-only 无 body、Kinematic 同步、属性变更重建、对象销毁、
 * Stop/重启与固定步进。
 */

#include <gtest/gtest.h>

#include "Engine/Core/GameObject/ColliderComponent.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/RigidBodyComponent.h"
#include "Engine/Core/Physics/PhysicsRuntime2D.h"
#include "Engine/Core/Scene/Scene.h"

#include <cmath>
#include <limits>
#include <memory>
#include <string>

using namespace Engine;

namespace {

std::shared_ptr<GameObject> AddObject(Scene& scene, const char* name,
                                      float x, float y) {
    auto go = std::make_shared<GameObject>(name);
    go->GetTransform().SetPosition(x, y, 0.0f);
    scene.AddObject(go);
    return go;
}

ColliderComponent* AddBoxCollider(GameObject& go, float halfX, float halfY) {
    auto* collider = static_cast<ColliderComponent*>(
        go.AddComponentByName("Collider"));
    collider->SetShape(ColliderShape::Box);
    collider->SetHalfExtents(halfX, halfY);
    return collider;
}

RigidBodyComponent* AddRigidBody(GameObject& go, RigidBodyMotionType type) {
    auto* rb = static_cast<RigidBodyComponent*>(
        go.AddComponentByName("RigidBody"));
    rb->SetMotionType(type);
    return rb;
}

void StepFrames(PhysicsRuntime2D& runtime, int frames, float dt = 1.0f / 60.0f) {
    for (int i = 0; i < frames; ++i) {
        std::string error;
        ASSERT_TRUE(runtime.Step(dt, &error)) << error;
    }
}

} // namespace

TEST(T1C_PhysicsRuntime, DynamicBodyFallsAndRestsOnStaticGround) {
    Scene scene;
    auto ground = AddObject(scene, "Ground", 0.0f, 0.0f);
    AddBoxCollider(*ground, 5.0f, 0.5f);
    auto body = AddObject(scene, "Body", 0.0f, 3.0f);
    AddBoxCollider(*body, 0.5f, 0.5f);
    AddRigidBody(*body, RigidBodyMotionType::Dynamic);

    PhysicsRuntime2D runtime;
    std::string error;
    ASSERT_TRUE(runtime.Start(scene, &error)) << error;
    EXPECT_EQ(runtime.GetBodyCount(), 2u);

    const float startY = body->GetTransform().GetPosition().y;
    StepFrames(runtime, 120);
    const float endY = body->GetTransform().GetPosition().y;
    EXPECT_LT(endY, startY);
    EXPECT_GT(endY, 0.5f) << "dynamic body must not pass through the ground";
    EXPECT_NEAR(ground->GetTransform().GetPosition().y, 0.0f, 1e-5f);

    runtime.Stop();
    EXPECT_FALSE(runtime.IsRunning());
    EXPECT_EQ(runtime.GetBodyCount(), 0u);
}

TEST(T1C_PhysicsRuntime, ColliderOnlyRemainsStatic) {
    Scene scene;
    auto ground = AddObject(scene, "Ground", 1.0f, 2.0f);
    AddBoxCollider(*ground, 5.0f, 0.5f);

    PhysicsRuntime2D runtime;
    ASSERT_TRUE(runtime.Start(scene));
    EXPECT_EQ(runtime.GetBodyCount(), 1u);
    StepFrames(runtime, 60);
    EXPECT_NEAR(ground->GetTransform().GetPosition().x, 1.0f, 1e-5f);
    EXPECT_NEAR(ground->GetTransform().GetPosition().y, 2.0f, 1e-5f);
}

TEST(T1C_PhysicsRuntime, RigidBodyWithoutColliderCreatesNoBody) {
    Scene scene;
    auto body = AddObject(scene, "BodyOnly", 0.0f, 2.0f);
    AddRigidBody(*body, RigidBodyMotionType::Dynamic);

    PhysicsRuntime2D runtime;
    ASSERT_TRUE(runtime.Start(scene));
    EXPECT_EQ(runtime.GetBodyCount(), 0u);
    StepFrames(runtime, 10);
    EXPECT_NEAR(body->GetTransform().GetPosition().y, 2.0f, 1e-5f);
}

TEST(T1C_PhysicsRuntime, KinematicBodyFollowsTransform) {
    Scene scene;
    auto body = AddObject(scene, "Kinematic", 0.0f, 1.0f);
    AddBoxCollider(*body, 0.5f, 0.5f);
    AddRigidBody(*body, RigidBodyMotionType::Kinematic);

    PhysicsRuntime2D runtime;
    ASSERT_TRUE(runtime.Start(scene));
    body->GetTransform().SetPosition(2.0f, 1.0f, 0.0f);
    StepFrames(runtime, 1);
    EXPECT_NEAR(body->GetTransform().GetPosition().x, 2.0f, 1e-5f);
    EXPECT_NEAR(body->GetTransform().GetPosition().y, 1.0f, 1e-5f);
}

TEST(T1C_PhysicsRuntime, MotionTypeMutationRebuildsBody) {
    Scene scene;
    auto ground = AddObject(scene, "Ground", 0.0f, 0.0f);
    AddBoxCollider(*ground, 5.0f, 0.5f);
    auto body = AddObject(scene, "Body", 0.0f, 3.0f);
    AddBoxCollider(*body, 0.5f, 0.5f);
    auto* rb = AddRigidBody(*body, RigidBodyMotionType::Dynamic);

    PhysicsRuntime2D runtime;
    ASSERT_TRUE(runtime.Start(scene));
    ASSERT_TRUE(rb->SetMotionType(RigidBodyMotionType::Static));
    StepFrames(runtime, 60);
    EXPECT_NEAR(body->GetTransform().GetPosition().y, 3.0f, 1e-5f)
        << "Static mutation must rebuild the body and stop falling";
}

TEST(T1C_PhysicsRuntime, RemoveRigidBodyRevertsToStatic) {
    Scene scene;
    auto body = AddObject(scene, "Body", 0.0f, 3.0f);
    AddBoxCollider(*body, 0.5f, 0.5f);
    AddRigidBody(*body, RigidBodyMotionType::Dynamic);

    PhysicsRuntime2D runtime;
    ASSERT_TRUE(runtime.Start(scene));
    ASSERT_TRUE(body->RemoveComponentByName("RigidBody"));
    StepFrames(runtime, 60);
    EXPECT_NEAR(body->GetTransform().GetPosition().y, 3.0f, 1e-5f);
}

TEST(T1C_PhysicsRuntime, DestroyedObjectIsUnboundWithoutCrash) {
    Scene scene;
    auto ground = AddObject(scene, "Ground", 0.0f, 0.0f);
    AddBoxCollider(*ground, 5.0f, 0.5f);
    auto body = AddObject(scene, "Body", 0.0f, 3.0f);
    AddBoxCollider(*body, 0.5f, 0.5f);
    AddRigidBody(*body, RigidBodyMotionType::Dynamic);

    PhysicsRuntime2D runtime;
    ASSERT_TRUE(runtime.Start(scene));
    EXPECT_EQ(runtime.GetBodyCount(), 2u);
    ASSERT_TRUE(scene.RemoveObject(body.get()));
    body.reset();
    StepFrames(runtime, 10);
    EXPECT_EQ(runtime.GetBodyCount(), 1u);
}

TEST(T1C_PhysicsRuntime, StopAndRestartRebuildsRuntimeState) {
    Scene scene;
    auto body = AddObject(scene, "Body", 0.0f, 3.0f);
    AddBoxCollider(*body, 0.5f, 0.5f);
    AddRigidBody(*body, RigidBodyMotionType::Dynamic);

    PhysicsRuntime2D runtime;
    ASSERT_TRUE(runtime.Start(scene));
    StepFrames(runtime, 30);
    runtime.Stop();
    EXPECT_EQ(runtime.GetBodyCount(), 0u);

    ASSERT_TRUE(runtime.Start(scene));
    EXPECT_EQ(runtime.GetBodyCount(), 1u);
    StepFrames(runtime, 1);
    runtime.Stop();
    EXPECT_EQ(runtime.GetBodyCount(), 0u);
}

TEST(T1C_PhysicsRuntime, FixedStepIsIndependentOfFrameDelta) {
    auto run = [](float frameDt, int frames) {
        Scene scene;
        auto ground = AddObject(scene, "Ground", 0.0f, 0.0f);
        AddBoxCollider(*ground, 5.0f, 0.5f);
        auto body = AddObject(scene, "Body", 0.0f, 3.0f);
        AddBoxCollider(*body, 0.5f, 0.5f);
        AddRigidBody(*body, RigidBodyMotionType::Dynamic);
        PhysicsRuntime2D runtime;
        EXPECT_TRUE(runtime.Start(scene));
        for (int i = 0; i < frames; ++i) {
            std::string error;
            EXPECT_TRUE(runtime.Step(frameDt, &error)) << error;
        }
        return body->GetTransform().GetPosition().y;
    };

    const float a = run(1.0f / 60.0f, 60);
    const float b = run(1.0f / 120.0f, 120);
    EXPECT_NEAR(a, b, 2e-3f)
        << "same elapsed time must produce the same fixed-step result";
}

TEST(T1C_PhysicsRuntime, InvalidDeltaFailsCleanlyAndRuntimeRecovers) {
    Scene scene;
    auto body = AddObject(scene, "Body", 0.0f, 3.0f);
    AddBoxCollider(*body, 0.5f, 0.5f);
    AddRigidBody(*body, RigidBodyMotionType::Dynamic);

    PhysicsRuntime2D runtime;
    ASSERT_TRUE(runtime.Start(scene));
    std::string error;
    EXPECT_FALSE(runtime.Step(-1.0f, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(runtime.Step(std::numeric_limits<float>::quiet_NaN(), &error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(runtime.Step(1.0f / 60.0f, &error)) << error;
    EXPECT_EQ(runtime.GetBodyCount(), 1u);
}
