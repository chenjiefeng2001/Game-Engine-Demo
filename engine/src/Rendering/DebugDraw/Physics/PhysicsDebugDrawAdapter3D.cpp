#include "Engine/Rendering/DebugDraw/Physics/PhysicsDebugDrawAdapter3D.h"

#include <cmath>

namespace Engine {
namespace Rendering {
namespace DebugDraw {

namespace {
constexpr float32 kPi2 = 6.28318530718f;

Vec3 NormalizeOr(const Vec3& v, const Vec3& fallback) {
    const float32 length = Vec3::Length(v);
    if (length <= 1.0e-6f) return fallback;
    return Vec3(v.x / length, v.y / length, v.z / length);
}
}

PhysicsDebugDrawAdapter3D::PhysicsDebugDrawAdapter3D(DebugDraw& service)
    : m_Service(service) {
}

void PhysicsDebugDrawAdapter3D::AddLine(const Vec3& from, const Vec3& to,
                                       const Vec4& color) {
    const Status status = m_Service.Line(from, to, color);
    if (status != Status::Ok) m_LastStatus = status;
}

void PhysicsDebugDrawAdapter3D::AddRing(const Vec3& center, float32 radius,
                                        const Vec4& color, int32 segments) {
    if (segments < 3) return;
    Vec3 previous = center + Vec3(radius, 0.0f, 0.0f);
    for (int32 i = 1; i <= segments; ++i) {
        const float32 angle = kPi2 * static_cast<float32>(i) /
                              static_cast<float32>(segments);
        const Vec3 current = center + Vec3(std::cos(angle) * radius, 0.0f,
                                           std::sin(angle) * radius);
        AddLine(previous, current, color);
        previous = current;
    }
}

void PhysicsDebugDrawAdapter3D::DrawLine(const Vec3& from, const Vec3& to,
                                         const Vec4& color) {
    AddLine(from, to, color);
}

void PhysicsDebugDrawAdapter3D::DrawTriangle(const Vec3& v0, const Vec3& v1,
                                             const Vec3& v2,
                                             const Vec4& color, bool) {
    AddLine(v0, v1, color);
    AddLine(v1, v2, color);
    AddLine(v2, v0, color);
}

void PhysicsDebugDrawAdapter3D::DrawSphere(const Vec3& center, float radius,
                                           const Vec4& color, int32) {
    const Status status = m_Service.WireSphere(center, radius, color);
    if (status != Status::Ok) m_LastStatus = status;
}

void PhysicsDebugDrawAdapter3D::DrawBox(const Vec3& center,
                                        const Vec3& halfExtents,
                                        const Vec4& color, bool) {
    const Status status = m_Service.WireBox(center, halfExtents,
                                            Quat::Identity(), color);
    if (status != Status::Ok) m_LastStatus = status;
}

void PhysicsDebugDrawAdapter3D::DrawCapsule(const Vec3& center, float radius,
                                            float height, const Vec4& color,
                                            bool) {
    const float32 halfHeight = height * 0.5f;
    AddRing(center + Vec3(0.0f, halfHeight, 0.0f), radius, color, 16);
    AddRing(center - Vec3(0.0f, halfHeight, 0.0f), radius, color, 16);
    for (int32 i = 0; i < 4; ++i) {
        const float32 angle = kPi2 * static_cast<float32>(i) / 4.0f;
        const Vec3 offset(std::cos(angle) * radius, 0.0f,
                          std::sin(angle) * radius);
        AddLine(center + offset - Vec3(0.0f, halfHeight, 0.0f),
                center + offset + Vec3(0.0f, halfHeight, 0.0f), color);
    }
}

void PhysicsDebugDrawAdapter3D::DrawCylinder(const Vec3& center, float radius,
                                             float height, const Vec4& color,
                                             bool) {
    const float32 halfHeight = height * 0.5f;
    AddRing(center + Vec3(0.0f, halfHeight, 0.0f), radius, color, 16);
    AddRing(center - Vec3(0.0f, halfHeight, 0.0f), radius, color, 16);
    for (int32 i = 0; i < 8; ++i) {
        const float32 angle = kPi2 * static_cast<float32>(i) / 8.0f;
        const Vec3 offset(std::cos(angle) * radius, 0.0f,
                          std::sin(angle) * radius);
        AddLine(center + offset - Vec3(0.0f, halfHeight, 0.0f),
                center + offset + Vec3(0.0f, halfHeight, 0.0f), color);
    }
}

void PhysicsDebugDrawAdapter3D::DrawText3D(const Vec3&, const char*,
                                           const Vec4&) {
    // T-5 v1 不提供 3D 文本；不得在此伪造文本渲染能力。
}

void PhysicsDebugDrawAdapter3D::DrawAxes(const Mat4& transform, float length) {
    const float32* m = transform.data;
    const Vec3 origin(m[12], m[13], m[14]);
    const Vec3 x = NormalizeOr(Vec3(m[0], m[1], m[2]), Vec3(1, 0, 0)) * length;
    const Vec3 y = NormalizeOr(Vec3(m[4], m[5], m[6]), Vec3(0, 1, 0)) * length;
    const Vec3 z = NormalizeOr(Vec3(m[8], m[9], m[10]), Vec3(0, 0, 1)) * length;
    AddLine(origin, origin + x, Vec4(1.0f, 0.0f, 0.0f, 1.0f));
    AddLine(origin, origin + y, Vec4(0.0f, 1.0f, 0.0f, 1.0f));
    AddLine(origin, origin + z, Vec4(0.0f, 0.0f, 1.0f, 1.0f));
}

void PhysicsDebugDrawAdapter3D::Clear() {
    // 通用服务的帧生命周期由调用方控制；这里不得清空当前帧。
}

void PhysicsDebugDrawAdapter3D::Flush() {
    // 实际提交由 DebugDraw::EndFrame() 完成。
}

} // namespace DebugDraw
} // namespace Rendering
} // namespace Engine
