#pragma once

#include "Engine/Core/Physics/IPhysicsDebugDraw.h"
#include "Engine/Rendering/DebugDraw/DebugDraw.h"

namespace Engine {
namespace Rendering {
namespace DebugDraw {

/**
 * @brief 将既有 IPhysicsDebugDraw 输出转换为通用 DebugDraw 线段。
 *
 * 不修改 IPhysicsDebugDraw / Box2D 实现。实体图元只输出轮廓；
 * 2D → 3D 映射由 physicsToWorld 决定，默认 Identity（物理 XY → 世界 XY，Z=0）。
 */
class PhysicsDebugDrawAdapter2D final : public IPhysicsDebugDraw {
public:
    explicit PhysicsDebugDrawAdapter2D(DebugDraw& service,
                                       const Mat4& physicsToWorld = Mat4{});

    PhysicsDebugDrawAdapter2D(const PhysicsDebugDrawAdapter2D&) = delete;
    PhysicsDebugDrawAdapter2D& operator=(const PhysicsDebugDrawAdapter2D&) = delete;

    void SetPhysicsToWorld(const Mat4& transform) { m_PhysicsToWorld = transform; }
    const Mat4& GetPhysicsToWorld() const { return m_PhysicsToWorld; }

    Status GetLastStatus() const { return m_LastStatus; }
    void ResetLastStatus() { m_LastStatus = Status::Ok; }

    void DrawPolygon(const Vec2* vertices, int32 vertexCount,
                     const DebugColor& color) override;
    void DrawSolidPolygon(const Vec2* vertices, int32 vertexCount,
                          const DebugColor& color) override;
    void DrawCircle(const Vec2& center, float32 radius,
                    const DebugColor& color) override;
    void DrawSolidCircle(const Vec2& center, float32 radius,
                         const Vec2& axis, const DebugColor& color) override;
    void DrawSegment(const Vec2& p1, const Vec2& p2,
                     const DebugColor& color) override;
    void DrawTransform(const Vec2& position, float32 angle) override;
    void DrawPoint(const Vec2& p, float32 size,
                   const DebugColor& color) override;

    void SetFlags(uint32 flags) override { m_Flags = flags; }
    uint32 GetFlags() const override { return m_Flags; }

private:
    Vec3 ToWorld(const Vec2& p) const;
    Vec4 ToColor(const DebugColor& color) const;
    void AddLine(const Vec3& from, const Vec3& to, const Vec4& color);
    void AddCircle(const Vec3& center, float32 radius,
                   const Vec4& color, int32 segments);

    DebugDraw& m_Service;
    Mat4 m_PhysicsToWorld;
    uint32 m_Flags = DebugDraw_All;
    Status m_LastStatus = Status::Ok;
};

} // namespace DebugDraw
} // namespace Rendering
} // namespace Engine
