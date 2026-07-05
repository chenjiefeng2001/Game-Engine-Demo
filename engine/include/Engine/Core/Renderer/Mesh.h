#pragma once

#include "Engine/Core/RHI/MathTypes.h"
#include <vector>
#include <cstdint>

namespace Engine {

    /**
     * @brief 顶点格式（GPU 友好的 interleaved 布局）
     *
     * 与 3D shader 中的 vertex attribute 布局匹配：
     *   layout(location = 0) = position (Vec3)
     *   layout(location = 1) = normal   (Vec3)
     *   layout(location = 2) = texCoord (Vec2)
     */
    struct Vertex3D {
        Vec3 position;
        Vec3 normal;
        Vec2 texCoord;
        Vec3 tangent = {0, 0, 0};   // 切线（用于法线贴图）
    };

    /**
     * @brief 3D 网格 — CPU 端纯数据容器
     *
     * 存储顶点和索引数据。GPU 资源（VBO/IBO/VAO）由 MeshRenderer 或
     * 上层渲染管线在渲染时按需创建。
     */
    class Mesh {
    public:
        Mesh() = default;
        Mesh(const std::vector<Vertex3D>& vertices, const std::vector<uint32>& indices);

        // ── 设置器 ──
        void SetVertices(const std::vector<Vertex3D>& vertices) { m_Vertices = vertices; }
        void SetIndices(const std::vector<uint32>& indices)     { m_Indices  = indices; }

        // ── 访问器 ──
        const std::vector<Vertex3D>& GetVertices() const { return m_Vertices; }
        const std::vector<uint32>&   GetIndices()  const { return m_Indices;  }
        size_t GetVertexCount() const { return m_Vertices.size(); }
        size_t GetIndexCount()  const { return m_Indices.size(); }
        bool   IsValid()        const { return !m_Vertices.empty() && !m_Indices.empty(); }

        // ── 法线重算 ──
        void RecalculateNormals();

        // ════════════════════════════════════════════
        // 预置几何体工厂
        // ════════════════════════════════════════════
        static Mesh CreateCube(float size = 1.0f);
        static Mesh CreateSphere(float radius = 1.0f, int32 segments = 32);
        static Mesh CreatePlane(float width = 1.0f, float height = 1.0f);
        static Mesh CreateCylinder(float radius = 1.0f, float height = 2.0f, int32 segments = 32);

    private:
        std::vector<Vertex3D> m_Vertices;
        std::vector<uint32>   m_Indices;
    };

} // namespace Engine
