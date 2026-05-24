#include "Box2DDebugDraw.h"
#include <box2d/box2d.h>

namespace Engine {

    // ============================================================
    // 构造
    // ============================================================

    Box2DDebugDraw::Box2DDebugDraw(IPhysicsDebugDraw& target)
        : m_Target(target)
    {
    }

    // ============================================================
    // 工具
    // ============================================================

    Vec2 Box2DDebugDraw::FromB2(const b2Vec2& v) {
        return Vec2(v.x, v.y);
    }

    DebugColor Box2DDebugDraw::FromB2(const b2Color& c) {
        return DebugColor(c.r, c.g, c.b, 1.0f);
    }

    // ============================================================
    // b2Draw 接口实现
    // ============================================================

    void Box2DDebugDraw::DrawPolygon(const b2Vec2* vertices, int32 vertexCount,
                                     const b2Color& color) {
        // 将 b2Vec2[] 转换为 Vec2[]（栈上分配，最大 64 个顶点）
        const int32 maxVerts = 64;
        Vec2 buffer[maxVerts];
        int32 count = vertexCount < maxVerts ? vertexCount : maxVerts;
        for (int32 i = 0; i < count; ++i) {
            buffer[i] = FromB2(vertices[i]);
        }
        m_Target.DrawPolygon(buffer, count, FromB2(color));
    }

    void Box2DDebugDraw::DrawSolidPolygon(const b2Vec2* vertices,
                                          int32 vertexCount,
                                          const b2Color& color) {
        const int32 maxVerts = 64;
        Vec2 buffer[maxVerts];
        int32 count = vertexCount < maxVerts ? vertexCount : maxVerts;
        for (int32 i = 0; i < count; ++i) {
            buffer[i] = FromB2(vertices[i]);
        }
        m_Target.DrawSolidPolygon(buffer, count, FromB2(color));
    }

    void Box2DDebugDraw::DrawCircle(const b2Vec2& center, float32 radius,
                                    const b2Color& color) {
        m_Target.DrawCircle(FromB2(center), radius, FromB2(color));
    }

    void Box2DDebugDraw::DrawSolidCircle(const b2Vec2& center, float32 radius,
                                         const b2Vec2& axis,
                                         const b2Color& color) {
        m_Target.DrawSolidCircle(FromB2(center), radius,
                                 FromB2(axis), FromB2(color));
    }

    void Box2DDebugDraw::DrawSegment(const b2Vec2& p1, const b2Vec2& p2,
                                     const b2Color& color) {
        m_Target.DrawSegment(FromB2(p1), FromB2(p2), FromB2(color));
    }

    void Box2DDebugDraw::DrawTransform(const b2Transform& xf) {
        m_Target.DrawTransform(FromB2(xf.p), xf.q.GetAngle());
    }

    void Box2DDebugDraw::DrawPoint(const b2Vec2& p, float32 size,
                                   const b2Color& color) {
        m_Target.DrawPoint(FromB2(p), size, FromB2(color));
    }

} // namespace Engine
