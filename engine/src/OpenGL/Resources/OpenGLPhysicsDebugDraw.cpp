#include "Engine/OpenGL/OpenGLPhysicsDebugDraw.h"
#include <glad/gl.h>
#include <cstring>
#include <iostream>
#include <cmath>
#include <vector>

namespace Engine {

    // ============================================================
    // 内联着色器源码
    // ============================================================

    static const char* s_DebugVertSrc = R"(
        #version 460 core
        layout(location = 0) in vec2 aPos;

        uniform mat4 u_ViewProjection;

        void main() {
            gl_Position = u_ViewProjection * vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* s_DebugFragSrc = R"(
        #version 460 core
        uniform vec4 u_Color;
        out vec4 FragColor;

        void main() {
            FragColor = u_Color;
        }
    )";

    // ============================================================
    // 构造 / 析构
    // ============================================================

    OpenGLPhysicsDebugDraw::OpenGLPhysicsDebugDraw(GladGLContext& gl)
        : m_GL(gl)
    {
        // 初始化为单位矩阵
        std::memset(m_ViewProj, 0, sizeof(m_ViewProj));
        m_ViewProj[0] = m_ViewProj[5] = m_ViewProj[10] = m_ViewProj[15] = 1.0f;

        Init();
    }

    OpenGLPhysicsDebugDraw::~OpenGLPhysicsDebugDraw() {
        if (m_ShaderID) m_GL.DeleteProgram(m_ShaderID);
        if (m_VAO)      m_GL.DeleteVertexArrays(1, &m_VAO);
        if (m_VBO)      m_GL.DeleteBuffers(1, &m_VBO);
    }

    // ============================================================
    // 初始化
    // ============================================================

    uint32 OpenGLPhysicsDebugDraw::CompileShader(GLenum type, const std::string& source) {
        uint32 id = m_GL.CreateShader(type);
        const char* src = source.c_str();
        m_GL.ShaderSource(id, 1, &src, nullptr);
        m_GL.CompileShader(id);

        int success;
        m_GL.GetShaderiv(id, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            m_GL.GetShaderInfoLog(id, 512, nullptr, infoLog);
            std::cerr << "[DebugDraw] Shader compile error:\n" << infoLog << std::endl;
            m_GL.DeleteShader(id);
            return 0;
        }
        return id;
    }

    uint32 OpenGLPhysicsDebugDraw::LinkProgram(uint32 vs, uint32 fs) {
        uint32 program = m_GL.CreateProgram();
        m_GL.AttachShader(program, vs);
        m_GL.AttachShader(program, fs);
        m_GL.LinkProgram(program);

        int success;
        m_GL.GetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            m_GL.GetProgramInfoLog(program, 512, nullptr, infoLog);
            std::cerr << "[DebugDraw] Program link error:\n" << infoLog << std::endl;
            m_GL.DeleteProgram(program);
            return 0;
        }
        return program;
    }

    void OpenGLPhysicsDebugDraw::Init() {
        // ── 编译着色器 ──
        uint32 vs = CompileShader(GL_VERTEX_SHADER, s_DebugVertSrc);
        uint32 fs = CompileShader(GL_FRAGMENT_SHADER, s_DebugFragSrc);
        if (!vs || !fs) {
            std::cerr << "[DebugDraw] Failed to compile shaders!" << std::endl;
            return;
        }

        m_ShaderID = LinkProgram(vs, fs);
        m_GL.DeleteShader(vs);
        m_GL.DeleteShader(fs);

        if (!m_ShaderID) {
            std::cerr << "[DebugDraw] Failed to link shader program!" << std::endl;
            return;
        }

        // 获取 uniform 位置
        m_UniformVP    = m_GL.GetUniformLocation(m_ShaderID, "u_ViewProjection");
        m_UniformColor = m_GL.GetUniformLocation(m_ShaderID, "u_Color");

        // ── 创建 VAO / VBO ──
        m_GL.GenVertexArrays(1, &m_VAO);
        m_GL.GenBuffers(1, &m_VBO);

        m_GL.BindVertexArray(m_VAO);
        m_GL.BindBuffer(GL_ARRAY_BUFFER, m_VBO);

        // 分配初始大小（动态增长）
        m_GL.BufferData(GL_ARRAY_BUFFER, m_MaxVerts * sizeof(Vec2), nullptr, GL_DYNAMIC_DRAW);

        // 属性：vec2 位置
        m_GL.EnableVertexAttribArray(0);
        m_GL.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vec2), (void*)0);

        m_GL.BindBuffer(GL_ARRAY_BUFFER, 0);
        m_GL.BindVertexArray(0);

        std::cout << "[DebugDraw] OpenGL debug draw initialized." << std::endl;
    }

    // ============================================================
    // 视图投影设置
    // ============================================================

    void OpenGLPhysicsDebugDraw::SetViewProjection(const float32* viewProjMatrix) {
        std::memcpy(m_ViewProj, viewProjMatrix, sizeof(m_ViewProj));
        m_ViewProjDirty = true;
    }

    // ============================================================
    // 核心绘制函数
    // ============================================================

    void OpenGLPhysicsDebugDraw::SubmitLines(const Vec2* vertices, int32 count,
                                              const DebugColor& color, GLenum mode) {
        if (!m_ShaderID || count < 1) return;

        // 使用着色器
        m_GL.UseProgram(m_ShaderID);

        // 设置 uniform
        m_GL.UniformMatrix4fv(m_UniformVP, 1, GL_FALSE, m_ViewProj);
        m_GL.Uniform4f(m_UniformColor, color.r, color.g, color.b, color.a);

        // 开启混合（支持透明度）
        m_GL.Enable(GL_BLEND);
        m_GL.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // 上传顶点数据
        m_GL.BindVertexArray(m_VAO);
        m_GL.BindBuffer(GL_ARRAY_BUFFER, m_VBO);

        // 如果顶点数超过当前缓冲区大小，重新分配
        if (count > m_MaxVerts) {
            m_MaxVerts = count + 256;
            m_GL.BufferData(GL_ARRAY_BUFFER, m_MaxVerts * sizeof(Vec2), nullptr, GL_DYNAMIC_DRAW);
        }

        m_GL.BufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(Vec2), vertices);

        // 绘制
        m_GL.DrawArrays(mode, 0, count);

        // 清理状态
        m_GL.BindBuffer(GL_ARRAY_BUFFER, 0);
        m_GL.BindVertexArray(0);
        m_GL.UseProgram(0);
    }

    // ============================================================
    // 绘制多边形（线框）
    // ============================================================

    void OpenGLPhysicsDebugDraw::DrawPolygon(const Vec2* vertices, int32 vertexCount,
                                              const DebugColor& color) {
        if (vertexCount < 2) return;
        SubmitLines(vertices, vertexCount, color, GL_LINE_LOOP);
    }

    // ============================================================
    // 绘制实心多边形（半透明填充 + 线框）
    // ============================================================

    void OpenGLPhysicsDebugDraw::DrawSolidPolygon(const Vec2* vertices, int32 vertexCount,
                                                   const DebugColor& color) {
        if (vertexCount < 3) {
            if (vertexCount == 2) DrawPolygon(vertices, 2, color);
            return;
        }

     
        DebugColor fillColor = color;
        fillColor.a *= 0.3f;  

        std::vector<Vec2> triFan;
        triFan.reserve(vertexCount);

        for (int32 i = 0; i < vertexCount; ++i) {
            triFan.push_back(vertices[i]);
        }
        SubmitLines(triFan.data(), vertexCount, fillColor, GL_TRIANGLE_FAN);


        DebugColor wireColor = color;
        wireColor.a = 1.0f;
        SubmitLines(vertices, vertexCount, wireColor, GL_LINE_LOOP);
    }

    // ============================================================
    // 绘制圆形（线框）
    // ============================================================

    void OpenGLPhysicsDebugDraw::DrawCircle(const Vec2& center, float32 radius,
                                             const DebugColor& color) {
        const int32 segments = 24;
        Vec2 verts[segments];
        for (int32 i = 0; i < segments; ++i) {
            float32 theta = 2.0f * 3.14159265f * float32(i) / float32(segments);
            verts[i].x = center.x + radius * std::cos(theta);
            verts[i].y = center.y + radius * std::sin(theta);
        }
        SubmitLines(verts, segments, color, GL_LINE_LOOP);
    }

    // ============================================================
    // 绘制实心圆形（半透明填充 + 线框 + 轴线）
    // ============================================================

    void OpenGLPhysicsDebugDraw::DrawSolidCircle(const Vec2& center, float32 radius,
                                                  const Vec2& axis,
                                                  const DebugColor& color) {
        const int32 segments = 24;
        Vec2 verts[segments];
        for (int32 i = 0; i < segments; ++i) {
            float32 theta = 2.0f * 3.14159265f * float32(i) / float32(segments);
            verts[i].x = center.x + radius * std::cos(theta);
            verts[i].y = center.y + radius * std::sin(theta);
        }

        DebugColor fillColor = color;
        fillColor.a *= 0.3f;
        SubmitLines(verts, segments, fillColor, GL_TRIANGLE_FAN);

        DebugColor wireColor = color;
        wireColor.a = 1.0f;
        SubmitLines(verts, segments, wireColor, GL_LINE_LOOP);

        DrawSegment(center, Vec2(center.x + axis.x * radius, center.y + axis.y * radius), wireColor);
    }

    // ============================================================
    // 绘制线段
    // ============================================================

    void OpenGLPhysicsDebugDraw::DrawSegment(const Vec2& p1, const Vec2& p2,
                                              const DebugColor& color) {
        Vec2 verts[2] = { p1, p2 };
        SubmitLines(verts, 2, color, GL_LINES);
    }

    // ============================================================
    // 绘制变换（坐标轴指示）
    // ============================================================

    void OpenGLPhysicsDebugDraw::DrawTransform(const Vec2& position, float32 angle) {
        const float32 axisLen = 0.5f;
        Vec2 dirX(std::cos(angle), std::sin(angle));
        Vec2 dirY(-dirX.y, dirX.x);  // 垂直向量

        Vec2 endX(position.x + dirX.x * axisLen, position.y + dirX.y * axisLen);
        DrawSegment(position, endX, DebugColor(1.0f, 0.0f, 0.0f, 1.0f));

        Vec2 endY(position.x + dirY.x * axisLen, position.y + dirY.y * axisLen);
        DrawSegment(position, endY, DebugColor(0.0f, 1.0f, 0.0f, 1.0f));

        DrawPoint(position, 4.0f, DebugColor(1.0f, 1.0f, 1.0f, 1.0f));
    }

    // ============================================================
    // 绘制点
    // ============================================================

    void OpenGLPhysicsDebugDraw::DrawPoint(const Vec2& p, float32 size,
                                            const DebugColor& color) {
        // 用一个很小的十字线来表示点
        float32 half = size * 0.01f;  // 转换成世界坐标大小
        Vec2 verts[4] = {
            Vec2(p.x - half, p.y),
            Vec2(p.x + half, p.y),
            Vec2(p.x, p.y - half),
            Vec2(p.x, p.y + half)
        };

        m_GL.UseProgram(m_ShaderID);
        m_GL.UniformMatrix4fv(m_UniformVP, 1, GL_FALSE, m_ViewProj);
        m_GL.Uniform4f(m_UniformColor, color.r, color.g, color.b, color.a);

        m_GL.BindVertexArray(m_VAO);
        m_GL.BindBuffer(GL_ARRAY_BUFFER, m_VBO);

        // 确保缓冲区足够大
        if (4 > m_MaxVerts) {
            m_MaxVerts = 4 + 256;
            m_GL.BufferData(GL_ARRAY_BUFFER, m_MaxVerts * sizeof(Vec2), nullptr, GL_DYNAMIC_DRAW);
        }

        m_GL.BufferSubData(GL_ARRAY_BUFFER, 0, 4 * sizeof(Vec2), verts);

        // 画两条线（十字）
        m_GL.DrawArrays(GL_LINES, 0, 2);
        m_GL.DrawArrays(GL_LINES, 2, 2);

        m_GL.BindBuffer(GL_ARRAY_BUFFER, 0);
        m_GL.BindVertexArray(0);
        m_GL.UseProgram(0);
    }

} // namespace Engine
