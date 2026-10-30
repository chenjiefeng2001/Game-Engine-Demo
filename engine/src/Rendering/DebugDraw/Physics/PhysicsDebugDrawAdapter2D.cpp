#include "Engine/Rendering/DebugDraw/Physics/PhysicsDebugDrawAdapter2D.h"

#include <cmath>

namespace Engine {
namespace Rendering {
namespace DebugDraw {

namespace {
constexpr float32 kPi2 = 6.28318530718f;
}

PhysicsDebugDrawAdapter2D::PhysicsDebugDrawAdapter2D(
    DebugDraw& service, const Mat4& physicsToWorld)
    : m_Service(service), m_PhysicsToWorld(physicsToWorld) {
}

Vec3 PhysicsDebugDrawAdapter2D::ToWorld(const Vec2& p) const {
    const float32* m = m_PhysicsToWorld.data;
    return Vec3(m[0] * p.x + m[4] * p.y + m[12],
                m[1] * p.x + m[5] * p.y + m[13],
                m[2] * p.x + m[6] * p.y + m[14]);
}

Vec4 PhysicsDebugDrawAdapter2D::ToColor(const DebugColor& color) const {
    return Vec4(color.r, color.g, color.b, color.a);
}

void PhysicsDebugDrawAdapter2D::AddLine(const Vec3& from, const Vec3& to,
                                        const Vec4& color) {
    const Status status = m_Service.Line(from, to, color);
    if (status != Status::Ok) m_LastStatus = status;
}

void PhysicsDebugDrawAdapter2D::AddCircle(const Vec3& center, float32 radius,
                                          const Vec4& color, int32 segments) {
    if (segments < 3) return;
    Vec3 previous = center + Vec3(radius, 0.0f, 0.0f);
    for (int32 i = 1; i <= segments; ++i) {
        const float32 angle = kPi2 * static_cast<float32>(i) /
                              static_cast<float32>(segments);
        const Vec3 current = center + Vec3(std::cos(angle) * radius,
                                           std::sin(angle) * radius, 0.0f);
        AddLine(previous, current, color);
        previous = current;
    }
}

void PhysicsDebugDrawAdapter2D::DrawPolygon(const Vec2* vertices,
                                            int32 vertexCount,
                                            const DebugColor& color) {
    if (!(m_Flags & DebugDraw_Shape) || !vertices || vertexCount < 2) return;
    const Vec4 c = ToColor(color);
    Vec3 previous = ToWorld(vertices[vertexCount - 1]);
    for (int32 i = 0; i < vertexCount; ++i) {
        const Vec3 current = ToWorld(vertices[i]);
        AddLine(previous, current, c);
        previous = current;
    }
}

void PhysicsDebugDrawAdapter2D::DrawSolidPolygon(const Vec2* vertices,
                                                 int32 vertexCount,
                                                 const DebugColor& color) {
    // v1 不提供填充；实体图元只输出轮廓。
    DrawPolygon(vertices, vertexCount, color);
}

void PhysicsDebugDrawAdapter2D::DrawCircle(const Vec2& center, float32 radius,
                                           const DebugColor& color) {
    if (!(m_Flags & DebugDraw_Shape) || radius <= 0.0f) return;
    AddCircle(ToWorld(center), radius, ToColor(color), 24);
}

void PhysicsDebugDrawAdapter2D::DrawSolidCircle(const Vec2& center,
                                                float32 radius,
                                                const Vec2& axis,
                                                const DebugColor& color) {
    DrawCircle(center, radius, color);
    if (!(m_Flags & DebugDraw_Shape) || radius <= 0.0f) return;
    const Vec3 worldCenter = ToWorld(center);
    const Vec3 worldAxis = ToWorld(Vec2(center.x + axis.x * radius,
                                        center.y + axis.y * radius));
    AddLine(worldCenter, worldAxis, ToColor(color));
}

void PhysicsDebugDrawAdapter2D::DrawSegment(const Vec2& p1, const Vec2& p2,
                                            const DebugColor& color) {
    constexpr uint32 kLineFlags = DebugDraw_Shape | DebugDraw_Joint;
    if (!(m_Flags & kLineFlags)) return;
    AddLine(ToWorld(p1), ToWorld(p2), ToColor(color));
}

void PhysicsDebugDrawAdapter2D::DrawTransform(const Vec2& position,
                                              float32 angle) {
    if (!(m_Flags & DebugDraw_Joint)) return;
    const Vec3 origin = ToWorld(position);
    const float32 c = std::cos(angle);
    const float32 s = std::sin(angle);
    const float32 length = 0.5f;
    AddLine(origin, ToWorld(Vec2(position.x + c * length,
                                 position.y + s * length)),
            Vec4(1.0f, 0.0f, 0.0f, 1.0f));
    AddLine(origin, ToWorld(Vec2(position.x - s * length,
                                 position.y + c * length)),
            Vec4(0.0f, 1.0f, 0.0f, 1.0f));
}

void PhysicsDebugDrawAdapter2D::DrawPoint(const Vec2& p, float32 size,
                                          const DebugColor& color) {
    if (!(m_Flags & DebugDraw_COM)) return;
    const float32 half = size * 0.01f;
    const Vec3 center = ToWorld(p);
    const Vec4 c = ToColor(color);
    AddLine(ToWorld(Vec2(p.x - half, p.y)), ToWorld(Vec2(p.x + half, p.y)), c);
    AddLine(ToWorld(Vec2(p.x, p.y - half)), ToWorld(Vec2(p.x, p.y + half)), c);
}

} // namespace DebugDraw
} // namespace Rendering
} // namespace Engine
