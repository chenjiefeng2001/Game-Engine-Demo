#pragma once

#include "Engine/Core/Physics/IPhysicsDebugDraw3D.h"
#include <glad/gl.h>
#include <vector>

namespace Engine {

/**
 * @brief OpenGL 3D 物理调试绘制实现
 *
 * 使用 IPhysicsDebugDraw3D 接口绘制碰撞形状线框。
 * 每帧调用 Clear() + 各绘制方法 + Flush()。
 */
class OpenGLPhysicsDebugDraw3D : public IPhysicsDebugDraw3D {
public:
    OpenGLPhysicsDebugDraw3D(GladGLContext& gl);
    virtual ~OpenGLPhysicsDebugDraw3D() override;

    // IPhysicsDebugDraw3D
    virtual void DrawLine(const Vec3& from, const Vec3& to, const Vec4& color) override;
    virtual void DrawTriangle(const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec4& color, bool wireframe) override;
    virtual void DrawSphere(const Vec3& center, float radius, const Vec4& color, int32 segments) override;
    virtual void DrawBox(const Vec3& center, const Vec3& halfExtents, const Vec4& color, bool wireframe) override;
    virtual void DrawCapsule(const Vec3& center, float radius, float height, const Vec4& color, bool wireframe) override;
    virtual void DrawCylinder(const Vec3& center, float radius, float height, const Vec4& color, bool wireframe) override;
    virtual void DrawText3D(const Vec3& position, const char* text, const Vec4& color) override;
    virtual void DrawAxes(const Mat4& transform, float length) override;
    virtual void Clear() override;
    virtual void Flush() override;

    /** 设置视图投影矩阵 */
    void SetViewProjection(const float32* vp) { std::memcpy(m_ViewProj, vp, sizeof(m_ViewProj)); }

private:
    struct LineVertex { Vec3 pos; Vec4 color; };
    void SubmitLines();

    GladGLContext& m_GL;

    uint32 m_VAO = 0, m_VBO = 0;
    uint32 m_ShaderID = 0;
    int32 m_UniformVP = -1;

    float32 m_ViewProj[16];

    std::vector<LineVertex> m_Lines;
};

} // namespace Engine