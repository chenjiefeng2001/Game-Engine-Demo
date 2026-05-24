#pragma once

/**
 * @file Box2DPhysicsWorld.h
 * @brief Box2D 物理世界实现（公开头文件）
 *
 * 公开头文件，sandbox 测试可直接 include 此文件创建物理世界。
 * 仅前向声明 b2World，不暴露 Box2D 内部类型。
 * 完整的 Box2D 包含在实现文件中。
 */

#include "Engine/Core/Physics/IPhysicsWorld.h"
#include <memory>
#include <vector>
#include <unordered_set>

struct b2World;  // 前向声明，避免暴露 Box2D 头文件

namespace Engine {

    class Box2DPhysicsBody;
    class Box2DContactListener;
    class Box2DDebugDraw;

    class Box2DPhysicsWorld : public IPhysicsWorld {
    public:
        explicit Box2DPhysicsWorld(const Vec2& gravity = Vec2(0.0f, -9.81f));
        ~Box2DPhysicsWorld() override;

        // ── IPhysicsWorld ──

        void Step(float32 dt, int32 velocityIterations = 8,
                  int32 positionIterations = 3) override;

        std::shared_ptr<IPhysicsBody> CreateBody(const BodyDef& def) override;
        void DestroyBody(IPhysicsBody* body) override;

        std::shared_ptr<IJoint> CreateJoint(const JointDef& def) override;
        void DestroyJoint(IJoint* joint) override;

        std::vector<RayCastResult> RayCast(const Vec2& from,
                                            const Vec2& to) override;

        std::vector<IPhysicsBody*> QueryAABB(const Vec2& center,
                                               const Vec2& halfSize) override;

        void SetGravity(const Vec2& gravity) override;
        Vec2 GetGravity() const override;

        void SetContactBeginCallback(ContactCallback callback) override;
        void SetContactEndCallback(ContactCallback callback) override;
        void SetContactPreSolveCallback(ContactFilterCallback callback) override;
        void SetContactPersistCallback(ContactPersistCallback callback) override;

        void SetDebugDraw(IPhysicsDebugDraw* draw) override;
        void DebugDraw() override;
        void* GetNativeWorld() override;

        // ── 内部接口（给 Box2DContactListener 调用） ──
        void OnContactBegin(const ContactInfo& info);
        void OnContactEnd(const ContactInfo& info);
        bool OnContactPreSolve(const void* bodyA, const void* bodyB);
        void OnContactPersist(const ContactPersistData& data);

    private:
        b2World* m_World = nullptr;
        std::unique_ptr<Box2DContactListener> m_ContactListener;
        std::unique_ptr<Box2DDebugDraw> m_Box2DDebugDraw;

        // 持有所有刚体和关节的所有权
        std::unordered_set<std::shared_ptr<Box2DPhysicsBody>> m_Bodies;
        std::unordered_set<std::shared_ptr<class Box2DJoint>> m_Joints;

        // 碰撞回调
        ContactCallback         m_ContactBeginCallback;
        ContactCallback         m_ContactEndCallback;
        ContactFilterCallback   m_ContactPreSolveCallback;
        ContactPersistCallback  m_ContactPersistCallback;

        // 调试绘制（用户提供的抽象接口）
        IPhysicsDebugDraw* m_UserDebugDraw = nullptr;
    };

} // namespace Engine
