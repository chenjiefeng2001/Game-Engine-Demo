#pragma once

/**
 * @file Box2DContactListener.h
 * @brief Box2D 碰撞事件辅助工具（Box2D 3.x）
 *
 * 私有头文件，仅在 Box2D 实现层使用。
 * v3 使用事件轮询（b2World_GetContactEvents）而非监听器回调。
 * 此类提供静态工具函数，将 b2ContactEvents 转换为引擎 ContactInfo。
 */

#include "Engine/Core/Physics/PhysicsDefs.h"
#include <box2d/box2d.h>

namespace Engine {

    class Box2DPhysicsWorld;

    /**
     * @brief Box2D 接触事件辅助类
     *
     * 提供静态方法将 v3 的接触事件结构转换为引擎定义的 ContactInfo。
     * 实际的事件轮询在 Box2DPhysicsWorld::Step() 中完成。
     */
    class Box2DContactListener {
    public:
        /**
         * @brief 从 b2ContactBeginTouchEvent 构建引擎 ContactInfo
         */
        static ContactInfo MakeContactInfo(const b2ContactBeginTouchEvent& event);

        /**
         * @brief 从 b2ContactEndTouchEvent 构建引擎 ContactInfo
         */
        static ContactInfo MakeContactInfoEnd(const b2ContactEndTouchEvent& event);

        /**
         * @brief 从 b2ContactHitEvent 构建引擎 ContactPersistData
         */
        static ContactPersistData MakeContactPersistData(const b2ContactHitEvent& event);

        /**
         * @brief 获取 shapeId 对应 body 上的 IPhysicsBody 指针
         */
        static IPhysicsBody* GetBodyFromShape(b2ShapeId shapeId);
    };

} // namespace Engine
