#include "Box2DDebugDraw.h"
#include <box2d/box2d.h>

namespace Engine {

    // ============================================================
    // Create — 填充 b2DebugDraw 结构体
    // ============================================================

    b2DebugDraw Box2DDebugDraw::Create(IPhysicsDebugDraw& target) {
        b2DebugDraw draw = b2DefaultDebugDraw();
        draw.context = &target;

        draw.DrawPolygonFcn     = &DrawPolygonImpl;
        draw.DrawSolidPolygonFcn = &DrawSolidPolygonImpl;
        draw.DrawCircleFcn      = &DrawCircleImpl;
        draw.DrawSolidCircleFcn = &DrawSolidCircleImpl;
        draw.DrawSolidCapsuleFcn = &DrawSolidCapsuleImpl;
        draw.DrawLineFcn        = &DrawLineImpl;
        draw.DrawTransformFcn   = &DrawTransformImpl;
        draw.DrawPointFcn       = &DrawPointImpl;

        draw.drawShapes  = true;
        draw.drawJoints  = true;
        draw.drawBounds  = false;
        draw.drawMass    = false;

        return draw;
    }

    // ============================================================
    // 工具函数
    // ============================================================

    Vec2 Box2DDebugDraw::FromB2(b2Vec2 v) {
        return Vec2(v.x, v.y);
    }

    DebugColor Box2DDebugDraw::FromB2Color(b2HexColor color) {
        // b2HexColor 是 uint32_t ARGB 格式
        float32 r = static_cast<float32>((color >> 16) & 0xFF) / 255.0f;
        float32 g = static_cast<float32>((color >> 8) & 0xFF) / 255.0f;
        float32 b = static_cast<float32>(color & 0xFF) / 255.0f;
        return DebugColor(r, g, b, 1.0f);
    }

    // ============================================================
    // 静态回调实现
    // ============================================================

    void Box2DDebugDraw::DrawPolygonImpl(const b2Vec2* vertices, int vertexCount,
                                         b2HexColor color, void* context) {
        auto* target = static_cast<IPhysicsDebugDraw*>(context);
        if (!target) return;

        const int32 maxVerts = 64;
        Vec2 buffer[maxVerts];
        int32 count = vertexCount < maxVerts ? vertexCount : maxVerts;
        for (int32 i = 0; i < count; ++i) {
            buffer[i] = FromB2(vertices[i]);
        }
        target->DrawPolygon(buffer, count, FromB2Color(color));
    }

    void Box2DDebugDraw::DrawSolidPolygonImpl(b2Transform transform,
                                              const b2Vec2* vertices, int vertexCount,
                                              float radius, b2HexColor color,
                                              void* context) {
        auto* target = static_cast<IPhysicsDebugDraw*>(context);
        if (!target) return;

        const int32 maxVerts = 64;
        Vec2 buffer[maxVerts];
        int32 count = vertexCount < maxVerts ? vertexCount : maxVerts;
        for (int32 i = 0; i < count; ++i) {
            buffer[i] = FromB2(vertices[i]);
        }
        target->DrawSolidPolygon(buffer, count, FromB2Color(color));
    }

    void Box2DDebugDraw::DrawCircleImpl(b2Vec2 center, float radius,
                                        b2HexColor color, void* context) {
        auto* target = static_cast<IPhysicsDebugDraw*>(context);
        if (!target) return;
        target->DrawCircle(FromB2(center), radius, FromB2Color(color));
    }

    void Box2DDebugDraw::DrawSolidCircleImpl(b2Transform transform, float radius,
                                              b2HexColor color, void* context) {
        auto* target = static_cast<IPhysicsDebugDraw*>(context);
        if (!target) return;

        target->DrawSolidCircle(FromB2(transform.p), radius,
                                Vec2(1.0f, 0.0f), FromB2Color(color));
    }

    void Box2DDebugDraw::DrawSolidCapsuleImpl(b2Vec2 p1, b2Vec2 p2, float radius,
                                               b2HexColor color, void* context) {
        auto* target = static_cast<IPhysicsDebugDraw*>(context);
        if (!target) return;
        // 引擎接口没有 DrawSolidCapsule，退化为两个圆 + 线段
        target->DrawCircle(FromB2(p1), radius, FromB2Color(color));
        target->DrawCircle(FromB2(p2), radius, FromB2Color(color));
        target->DrawSegment(FromB2(p1), FromB2(p2), FromB2Color(color));
    }

    void Box2DDebugDraw::DrawLineImpl(b2Vec2 p1, b2Vec2 p2,
                                      b2HexColor color, void* context) {
        auto* target = static_cast<IPhysicsDebugDraw*>(context);
        if (!target) return;
        target->DrawSegment(FromB2(p1), FromB2(p2), FromB2Color(color));
    }

    void Box2DDebugDraw::DrawTransformImpl(b2Transform transform, void* context) {
        auto* target = static_cast<IPhysicsDebugDraw*>(context);
        if (!target) return;
        target->DrawTransform(FromB2(transform.p),
                              b2Rot_GetAngle(transform.q));
    }

    void Box2DDebugDraw::DrawPointImpl(b2Vec2 p, float size,
                                       b2HexColor color, void* context) {
        auto* target = static_cast<IPhysicsDebugDraw*>(context);
        if (!target) return;
        target->DrawPoint(FromB2(p), size, FromB2Color(color));
    }

} // namespace Engine
