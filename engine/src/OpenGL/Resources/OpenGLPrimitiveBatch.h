#pragma once

#include "Engine/Core/RHI/IPrimitiveBatch.h"
#include <glad/gl.h>
#include <vector>

namespace Engine {

    class IRenderContext;

    // ──────────────────────────────────────────────────────────────
    // OpenGL 图元批处理器
    //
    // 累积顶点 + 索引，Commit() 时以单个 DrawElements 调用提交。
    // 若顶点数超过容量自动 Flush，保证缓冲区不溢出。
    // ──────────────────────────────────────────────────────────────
    class OpenGLPrimitiveBatch : public IPrimitiveBatch {
    public:
        OpenGLPrimitiveBatch(GladGLContext& gl, uint32 capacity = 16384);
        virtual ~OpenGLPrimitiveBatch() override;

        OpenGLPrimitiveBatch(const OpenGLPrimitiveBatch&) = delete;
        OpenGLPrimitiveBatch& operator=(const OpenGLPrimitiveBatch&) = delete;

        // ── IPrimitiveBatch ──
        virtual void Begin(PrimitiveType type = PrimitiveType::Triangles) override;
        virtual void Commit() override;
        virtual void End() override;
        virtual void Clear() override;

        virtual void Vertex(const PrimitiveVertex& v) override;
        virtual void Vertex(const Vec3& pos, const Vec4& color) override;
        virtual void Vertex(const Vec3& pos, const Vec3& normal,
                            const Vec2& uv, const Vec4& color) override;
        virtual void Index(uint32 i) override;
        virtual void Triangle(const PrimitiveVertex& v0,
                              const PrimitiveVertex& v1,
                              const PrimitiveVertex& v2) override;
        virtual void Line(const Vec3& from, const Vec3& to,
                          const Vec4& color = Vec4(1,1,1,1)) override;
        virtual void Point(const Vec3& pos, const Vec4& color = Vec4(1,1,1,1)) override;

        virtual uint32 GetVertexCount() const override { return (uint32)m_Vertices.size(); }
        virtual uint32 GetIndexCount() const override { return (uint32)m_Indices.size(); }
        virtual PrimitiveType GetPrimitiveType() const override { return m_Type; }

        // ── 容量控制 ──
        void SetCapacity(uint32 maxVertices);
        uint32 GetCapacity() const { return m_MaxVertices; }

    private:
        void Flush();
        GLenum ToGLPrimitive(PrimitiveType type) const;
        void CreateGPUResources();
        void DestroyGPUResources();

        GladGLContext& m_GL;

        // ── CPU 缓冲区 ──
        std::vector<PrimitiveVertex> m_Vertices;
        std::vector<uint32>          m_Indices;
        PrimitiveType  m_Type = PrimitiveType::Triangles;
        bool           m_Began = false;
        uint32         m_MaxVertices;

        // ── GPU 资源 ──
        GLuint m_VAO = 0;
        GLuint m_VBO = 0;
        GLuint m_EBO = 0;
        bool   m_ResourcesValid = false;
    };

} // namespace Engine
