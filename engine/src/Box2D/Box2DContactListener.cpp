#include "Box2DContactListener.h"
#include "Box2DPhysicsWorld.h"
#include "Box2DPhysicsBody.h"
#include <box2d/box2d.h>

namespace Engine {

    // ============================================================
    // 工具函数
    // ============================================================

    static Vec2 FromB2(const b2Vec2& v) {
        return Vec2(v.x, v.y);
    }

    ContactInfo Box2DContactListener::MakeContactInfo(b2Contact* contact) {
        ContactInfo info;

        // 获取碰撞的两个刚体
        b2Body* bodyA = contact->GetFixtureA()->GetBody();
        b2Body* bodyB = contact->GetFixtureB()->GetBody();

        info.bodyA = reinterpret_cast<IPhysicsBody*>(bodyA->GetUserData().pointer);
        info.bodyB = reinterpret_cast<IPhysicsBody*>(bodyB->GetUserData().pointer);

        // 获取碰撞点和法线
        b2Manifold* manifold = contact->GetManifold();
        if (manifold && manifold->pointCount > 0) {
            b2WorldManifold worldManifold;
            contact->GetWorldManifold(&worldManifold);

            info.point  = FromB2(worldManifold.points[0]);
            info.normal = FromB2(worldManifold.normal);
        }

        return info;
    }

    void* Box2DContactListener::GetBodyUserData(b2Contact* contact, bool isA) {
        b2Body* body = isA
            ? contact->GetFixtureA()->GetBody()
            : contact->GetFixtureB()->GetBody();
        return body ? reinterpret_cast<void*>(body->GetUserData().pointer) : nullptr;
    }

    // ============================================================
    // b2ContactListener 回调
    // ============================================================

    void Box2DContactListener::BeginContact(b2Contact* contact) {
        if (!contact) return;

        ContactInfo info = MakeContactInfo(contact);
        m_World.OnContactBegin(info);
    }

    void Box2DContactListener::EndContact(b2Contact* contact) {
        if (!contact) return;

        ContactInfo info = MakeContactInfo(contact);
        m_World.OnContactEnd(info);
    }

    void Box2DContactListener::PreSolve(b2Contact* contact,
                                        const b2Manifold* /*oldManifold*/) {
        if (!contact) return;

        // 获取两个 body 的 userData（即 IPhysicsBody*）
        void* bodyA = GetBodyUserData(contact, true);
        void* bodyB = GetBodyUserData(contact, false);

        // 调用滤波回调决定是否允许碰撞
        if (!m_World.OnContactPreSolve(bodyA, bodyB)) {
            contact->SetEnabled(false);
        }
    }

} // namespace Engine
