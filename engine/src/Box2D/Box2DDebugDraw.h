#pragma once

/**
 * @file Box2DDebugDraw.h
 * @brief Box2D 调试绘制桥接 — 将 b2Draw 事件转换为 IPhysicsDebugDraw 调用
 *
 * 私有头文件，仅在 Box2D 实现层使用。
 * 继承 b2Draw 并转发到引擎抽象的 IPhysicsDebugDraw 接口。
 */

#include "Engine/Core/Physics/IPhysicsDebugDraw.h"
#include <box2d/box2d.h>

namespace Engine {

    class Box2DDebugDraw : public b2Draw {
    public:
        explicit Box2DDebugDraw(IPhysicsDebugDraw& target);

        void DrawPolygon(const b2Vec2* vertices, int32 vertexCount,
                         const b2Color& color) override;
        void DrawSolidPolygon(const b2Vec2* vertices, int32 vertexCount,
                              const b2Color& color) override;
        void DrawCircle(const b2Vec2& center, float32 radius,
                        const b2Color& color) override;
        void DrawSolidCircle(const b2Vec2& center, float32 radius,
                             const b2Vec2& axis, const b2Color& color) override;
        void DrawSegment(const b2Vec2& p1, const b2Vec2& p2,
                         const b2Color& color) override;
        void DrawTransform(const b2Transform& xf) override;
        void DrawPoint(const b2Vec2& p, float32 size,
                       const b2Color& color) override;

    private:
        IPhysicsDebugDraw& m_Target;

        static Vec2      FromB2(const b2Vec2& v);
        static DebugColor FromB2(const b2Color& c);
    };

} // namespace Engine
