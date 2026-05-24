#pragma once

/**
 * @file Box2DContactListener.h
 * @brief Box2D 碰撞事件监听器 — 将 b2ContactListener 事件桥接到引擎回调
 *
 * 私有头文件，仅在 Box2D 实现层使用。
 */

#include "Engine/Core/Physics/PhysicsDefs.h"
#include <box2d/box2d.h>

namespace Engine {

    class Box2DPhysicsWorld;

    /**
     * @brief Box2D 接触监听器
     *
     * 将 b2ContactListener 的 BeginContact / EndContact 事件
     * 转换为引擎定义的 ContactCallback，并传递给 Box2DPhysicsWorld。
     */
    class Box2DContactListener : public b2ContactListener {
    public:
        explicit Box2DContactListener(Box2DPhysicsWorld& world)
            : m_World(world) {}

        void BeginContact(b2Contact* contact) override;
        void EndContact(b2Contact* contact) override;
        void PreSolve(b2Contact* contact, const b2Manifold* oldManifold) override;

        void PostSolve(b2Contact* contact, const b2ContactImpulse* impulse) override;

    private:
        Box2DPhysicsWorld& m_World;

        static ContactInfo MakeContactInfo(b2Contact* contact);
        static ContactPersistData MakeContactPersistData(b2Contact* contact, const b2ContactImpulse* impulse);
        static void* GetBodyUserData(b2Contact* contact, bool isA);
    };

} // namespace Engine
