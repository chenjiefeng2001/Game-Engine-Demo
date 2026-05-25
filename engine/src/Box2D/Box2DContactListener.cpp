#include "Box2DContactListener.h"
#include "Box2DPhysicsWorld.h"
#include "Box2DPhysicsBody.h"
#include <box2d/box2d.h>

namespace Engine {

    // ============================================================
    // 工具函数
    // ============================================================

    static Vec2 FromB2(b2Vec2 v) {
        return Vec2(v.x, v.y);
    }

    // ============================================================
    // MakeContactInfo — 从 BeginTouch 事件构建
    // ============================================================

    ContactInfo Box2DContactListener::MakeContactInfo(const b2ContactBeginTouchEvent& event) {
        ContactInfo info;

        info.bodyA = GetBodyFromShape(event.shapeIdA);
        info.bodyB = GetBodyFromShape(event.shapeIdB);

        info.point  = Vec2(0.0f, 0.0f);
        info.normal = Vec2(0.0f, 1.0f);

        return info;
    }

    // ============================================================
    // MakeContactInfoEnd — 从 EndTouch 事件构建
    // ============================================================

    ContactInfo Box2DContactListener::MakeContactInfoEnd(const b2ContactEndTouchEvent& event) {
        ContactInfo info;

        info.bodyA = GetBodyFromShape(event.shapeIdA);
        info.bodyB = GetBodyFromShape(event.shapeIdB);

        info.point  = Vec2(0.0f, 0.0f);
        info.normal = Vec2(0.0f, 1.0f);

        return info;
    }

    // ============================================================
    // MakeContactPersistData — 从 HitEvent 构建（含冲量）
    // ============================================================

    ContactPersistData Box2DContactListener::MakeContactPersistData(const b2ContactHitEvent& event) {
        ContactPersistData data;

        data.bodyA = GetBodyFromShape(event.shapeIdA);
        data.bodyB = GetBodyFromShape(event.shapeIdB);

        data.point  = FromB2(event.point);
        data.normal = FromB2(event.normal);

        data.pointCount = 1;
        data.impulses[0] = event.approachSpeed;  // 近似
        data.impulse = event.approachSpeed;

        return data;
    }

    // ============================================================
    // GetBodyFromShape — 从 shapeId 回溯 IPhysicsBody*
    // ============================================================

    IPhysicsBody* Box2DContactListener::GetBodyFromShape(b2ShapeId shapeId) {
        if (!b2Shape_IsValid(shapeId)) return nullptr;
        b2BodyId bodyId = b2Shape_GetBody(shapeId);
        if (!b2Body_IsValid(bodyId)) return nullptr;
        return static_cast<IPhysicsBody*>(b2Body_GetUserData(bodyId));
    }

} // namespace Engine
