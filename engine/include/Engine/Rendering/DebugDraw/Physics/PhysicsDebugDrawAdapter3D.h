#pragma once

#include "Engine/Core/Physics/IPhysicsDebugDraw3D.h"
#include "Engine/Rendering/DebugDraw/DebugDraw.h"

namespace Engine {
namespace Rendering {
namespace DebugDraw {

/**
 * @brief 将既有 IPhysicsDebugDraw3D 输出转换为通用 DebugDraw 线段。
 *
 * 只转换已有线段/可表达为轮廓的输出；不实现 Jolt DrawGeometry 的几何提取。
 * Clear()/Flush() 不触发通用服务渲染：通用服务的帧生命周期由调用方控制。
 */
class PhysicsDebugDrawAdapter3D final : public IPhysicsDebugDraw3D {
public:
    explicit PhysicsDebugDrawAdapter3D(DebugDraw& service);

    PhysicsDebugDrawAdapter3D(const PhysicsDebugDrawAdapter3D&) = delete;
    PhysicsDebugDrawAdapter3D& operator=(const PhysicsDebugDrawAdapter3D&) = delete;

    Status GetLastStatus() const { return m_LastStatus; }
    void ResetLastStatus() { m_LastStatus = Status::Ok; }

    void DrawLine(const Vec3& from, const Vec3& to,
                  const Vec4& color) override;
    void DrawTriangle(const Vec3& v0, const Vec3& v1, const Vec3& v2,
                      const Vec4& color, bool wireframe = false) override;
    void DrawSphere(const Vec3& center, float radius, const Vec4& color,
                    int32 segments = 16) override;
    void DrawBox(const Vec3& center, const Vec3& halfExtents,
                 const Vec4& color, bool wireframe = true) override;
    void DrawCapsule(const Vec3& center, float radius, float height,
                     const Vec4& color, bool wireframe = true) override;
    void DrawCylinder(const Vec3& center, float radius, float height,
                      const Vec4& color, bool wireframe = true) override;
    void DrawText3D(const Vec3& position, const char* text,
                    const Vec4& color) override;
    void DrawAxes(const Mat4& transform, float length = 0.5f) override;
    void Clear() override;
    void Flush() override;

private:
    void AddLine(const Vec3& from, const Vec3& to, const Vec4& color);
    void AddRing(const Vec3& center, float radius, const Vec4& color,
                 int32 segments);

    DebugDraw& m_Service;
    Status m_LastStatus = Status::Ok;
};

} // namespace DebugDraw
} // namespace Rendering
} // namespace Engine
