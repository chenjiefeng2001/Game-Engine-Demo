#pragma once

/**
 * @file OpenGLPhysicsDebugDraw.h
 * @brief Box2D 调试绘制的 OpenGL 实现
 *
 * 使用 OpenGL 直接渲染碰撞体线框、半透明填充、质心、关节锚点等。
 * 需要每帧调用 SetViewProjection() 传入相机矩阵。
 *
 * 热重载支持：
 *   通过 SetFlags() / GetFlags() 在运行时切换调试绘制内容
 *   DebugDraw_Shape  : 碰撞体线框 + 半透明填充
 *   DebugDraw_Joint  : 关节锚点 + 连接线
 *   DebugDraw_AABB   : 包围盒线框
 *   DebugDraw_COM    : 质心十字标记
 */

#include "Engine/Core/Physics/IPhysicsDebugDraw.h"
#include <glad/gl.h>
#include <string>

namespace Engine {

    class OpenGLPhysicsDebugDraw : public IPhysicsDebugDraw {
    public:
        explicit OpenGLPhysicsDebugDraw(GladGLContext& gl);
        ~OpenGLPhysicsDebugDraw() override;

        // ── 每帧调用 ──
        /** 传入相机视图投影矩阵 (float32[16]) */
        void SetViewProjection(const float32* viewProjMatrix);

        // ── IPhysicsDebugDraw ──
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
        // 初始化着色器和 VAO/VBO
        void Init();

        // 提交顶点数据到 GPU 并绘制
        void SubmitLines(const Vec2* vertices, int32 count,
                         const DebugColor& color, GLenum mode);

        // 编译着色器
        uint32 CompileShader(GLenum type, const std::string& source);
        uint32 LinkProgram(uint32 vs, uint32 fs);

        GladGLContext& m_GL;

        uint32 m_Flags = DebugDraw_All;

        // 着色器
        uint32 m_ShaderID = 0;
        int32  m_UniformVP     = -1;
        int32  m_UniformColor  = -1;

        // 渲染资源
        uint32 m_VAO = 0;
        uint32 m_VBO = 0;
        int32  m_MaxVerts = 1024; 

        // 缓存的视图投影矩阵
        float32 m_ViewProj[16];
        bool    m_ViewProjDirty = true;
    };

} // namespace Engine
