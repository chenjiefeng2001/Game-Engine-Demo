#include "OpenGLPrimitiveBatch.h"

namespace Engine {

    // ============================================================
    // 构造 / 析构
    // ============================================================

    OpenGLPrimitiveBatch::OpenGLPrimitiveBatch(GladGLContext& gl, uint32 capacity)
        : m_GL(gl)
        , m_MaxVertices(capacity)
    {
        m_Vertices.reserve(capacity);
        m_Indices.reserve(capacity * 3 / 2); // ~1.5x indices vs vertices
    }

    OpenGLPrimitiveBatch::~OpenGLPrimitiveBatch()
    {
        DestroyGPUResources();
    }

    // ============================================================
    // 生命周期
    // ============================================================

    void OpenGLPrimitiveBatch::Begin(PrimitiveType type)
    {
        if (m_Began) {
            // 自动结束上一批次
            End();
        }
        m_Type = type;
        m_Vertices.clear();
        m_Indices.clear();
        m_Began = true;
    }

    void OpenGLPrimitiveBatch::Commit()
    {
        if (!m_Began || m_Vertices.empty())
            return;

        Flush();
    }

    void OpenGLPrimitiveBatch::End()
    {
        if (!m_Began)
            return;
        Commit();
        m_Vertices.clear();
        m_Indices.clear();
        m_Began = false;
    }

    void OpenGLPrimitiveBatch::Clear()
    {
        m_Vertices.clear();
        m_Indices.clear();
    }

    // ============================================================
    // 顶点 / 索引写入
    // ============================================================

    void OpenGLPrimitiveBatch::Vertex(const PrimitiveVertex& v)
    {
        if (!m_Began) return;
        if (m_Vertices.size() >= m_MaxVertices) {
            Flush();  // 自动 Flush 防止溢出
        }
        m_Vertices.push_back(v);
    }

    void OpenGLPrimitiveBatch::Vertex(const Vec3& pos, const Vec4& color)
    {
        Vertex(PrimitiveVertex(pos, color));
    }

    void OpenGLPrimitiveBatch::Vertex(const Vec3& pos, const Vec3& normal,
                                       const Vec2& uv, const Vec4& color)
    {
        Vertex(PrimitiveVertex(pos, normal, uv, color));
    }

    void OpenGLPrimitiveBatch::Index(uint32 i)
    {
        if (!m_Began) return;
        m_Indices.push_back(i);
    }

    void OpenGLPrimitiveBatch::Triangle(const PrimitiveVertex& v0,
                                         const PrimitiveVertex& v1,
                                         const PrimitiveVertex& v2)
    {
        uint32 base = (uint32)m_Vertices.size();
        Vertex(v0); Vertex(v1); Vertex(v2);
        Index(base); Index(base + 1); Index(base + 2);
    }

    void OpenGLPrimitiveBatch::Line(const Vec3& from, const Vec3& to,
                                     const Vec4& color)
    {
        uint32 base = (uint32)m_Vertices.size();
        Vertex(from, color);
        Vertex(to, color);
        Index(base);
        Index(base + 1);
    }

    void OpenGLPrimitiveBatch::Point(const Vec3& pos, const Vec4& color)
    {
        uint32 base = (uint32)m_Vertices.size();
        Vertex(pos, color);
        Index(base);
    }

    // ============================================================
    // 容量控制
    // ============================================================

    void OpenGLPrimitiveBatch::SetCapacity(uint32 maxVertices)
    {
        m_MaxVertices = maxVertices;
        m_Vertices.reserve(maxVertices);
    }

    // ============================================================
    // GPU 提交（核心）
    // ============================================================

    void OpenGLPrimitiveBatch::Flush()
    {
        if (m_Vertices.empty() || m_Indices.empty())
            return;

        CreateGPUResources();

        // ── 上传顶点数据 ──
        m_GL.BindBuffer(GL_ARRAY_BUFFER, m_VBO);
        m_GL.BufferData(GL_ARRAY_BUFFER,
                         (GLsizeiptr)(m_Vertices.size() * sizeof(PrimitiveVertex)),
                         m_Vertices.data(),
                         GL_DYNAMIC_DRAW);

        // ── 上传索引数据 ──
        m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
        m_GL.BufferData(GL_ELEMENT_ARRAY_BUFFER,
                         (GLsizeiptr)(m_Indices.size() * sizeof(uint32)),
                         m_Indices.data(),
                         GL_DYNAMIC_DRAW);

        // ── 设置顶点属性布局 ──
        m_GL.BindVertexArray(m_VAO);

        // position (vec3)
        m_GL.EnableVertexAttribArray(0);
        m_GL.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                                  sizeof(PrimitiveVertex),
                                  (void*)offsetof(PrimitiveVertex, position));
        // normal (vec3)
        m_GL.EnableVertexAttribArray(1);
        m_GL.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                                  sizeof(PrimitiveVertex),
                                  (void*)offsetof(PrimitiveVertex, normal));
        // texCoord (vec2)
        m_GL.EnableVertexAttribArray(2);
        m_GL.VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                                  sizeof(PrimitiveVertex),
                                  (void*)offsetof(PrimitiveVertex, texCoord));
        // color (vec4)
        m_GL.EnableVertexAttribArray(3);
        m_GL.VertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE,
                                  sizeof(PrimitiveVertex),
                                  (void*)offsetof(PrimitiveVertex, color));

        // ── 发出 DrawCall ──
        m_GL.DrawElements(ToGLPrimitive(m_Type),
                           (GLsizei)m_Indices.size(),
                           GL_UNSIGNED_INT,
                           nullptr);

        // ── 清理 CPU 缓冲区以便继续累积 ──
        m_Vertices.clear();
        m_Indices.clear();

        // 解绑 VAO 避免后续意外修改
        m_GL.BindVertexArray(0);
    }

    // ============================================================
    // GPU 资源管理
    // ============================================================

    void OpenGLPrimitiveBatch::CreateGPUResources()
    {
        if (m_ResourcesValid)
            return;

        m_GL.GenVertexArrays(1, &m_VAO);
        m_GL.GenBuffers(1, &m_VBO);
        m_GL.GenBuffers(1, &m_EBO);

        m_ResourcesValid = true;
    }

    void OpenGLPrimitiveBatch::DestroyGPUResources()
    {
        if (m_VAO)  m_GL.DeleteVertexArrays(1, &m_VAO);
        if (m_VBO)  m_GL.DeleteBuffers(1, &m_VBO);
        if (m_EBO)  m_GL.DeleteBuffers(1, &m_EBO);
        m_VAO = m_VBO = m_EBO = 0;
        m_ResourcesValid = false;
    }

    // ============================================================
    // 工具
    // ============================================================

    GLenum OpenGLPrimitiveBatch::ToGLPrimitive(PrimitiveType type) const
    {
        switch (type) {
            case PrimitiveType::Triangles:     return GL_TRIANGLES;
            case PrimitiveType::Lines:         return GL_LINES;
            case PrimitiveType::Points:        return GL_POINTS;
            case PrimitiveType::TriangleStrip: return GL_TRIANGLE_STRIP;
            case PrimitiveType::LineStrip:     return GL_LINE_STRIP;
            default: return GL_TRIANGLES;
        }
    }

} // namespace Engine
