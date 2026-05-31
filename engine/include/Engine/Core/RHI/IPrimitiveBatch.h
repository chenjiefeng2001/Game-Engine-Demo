#pragma once

/**
 * @file IPrimitiveBatch.h
 * @brief 图元批处理接口 — 将大量顶点/索引批量提交到 GPU
 *
 * 功能：
 *   - 累积顶点和索引到 CPU 缓冲区
 *   - Commit() 一次性上传 GPU 并发出单个 DrawCall
 *   - 支持 Triangles / Lines 等多种图元类型
 *   - 支持 Debug 绘制（坐标轴、网格、包围盒等）
 *
 * 使用示例：
 * @code
 *   batch->Begin(PrimitiveType::Triangles);
 *   for (auto& mesh : meshes) {
 *       batch->Vertex(pos, normal, uv, color);
 *       batch->Index(i0); batch->Index(i1); batch->Index(i2);
 *   }
 *   batch->Commit();  // 单次 DrawCall
 *   batch->End();
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <memory>

namespace Engine {

    // ──────────────────────────────────────────
    // 图元类型
    // ──────────────────────────────────────────
    enum class PrimitiveType : uint8 {
        Triangles     = 0,  // GL_TRIANGLES
        Lines         = 1,  // GL_LINES
        Points        = 2,  // GL_POINTS
        TriangleStrip = 3,  // GL_TRIANGLE_STRIP
        LineStrip     = 4,  // GL_LINE_STRIP
    };

    // ──────────────────────────────────────────
    // 图元顶点格式（与 GPU 布局匹配）
    // ──────────────────────────────────────────
    struct PrimitiveVertex {
        Vec3 position;
        Vec3 normal;
        Vec2 texCoord;
        Vec4 color;

        PrimitiveVertex() = default;
        PrimitiveVertex(const Vec3& pos, const Vec4& col = Vec4(1,1,1,1))
            : position(pos), normal(0,0,1), texCoord(0,0), color(col) {}
        PrimitiveVertex(const Vec3& pos, const Vec3& norm, const Vec2& uv, const Vec4& col)
            : position(pos), normal(norm), texCoord(uv), color(col) {}
    };

    // ──────────────────────────────────────────
    // 图元批处理接口
    // ──────────────────────────────────────────
    class IPrimitiveBatch {
    public:
        virtual ~IPrimitiveBatch() = default;

        // ── 生命周期 ──

        /** 开始收集图元 */
        virtual void Begin(PrimitiveType type = PrimitiveType::Triangles) = 0;

        /** 提交所有累积图元到 GPU 并发出 DrawCall */
        virtual void Commit() = 0;

        /** 提交并重置状态 */
        virtual void End() = 0;

        /** 清空缓冲区（不提交） */
        virtual void Clear() = 0;

        // ── 顶点 / 索引写入 ──

        /** 添加一个顶点 */
        virtual void Vertex(const PrimitiveVertex& v) = 0;

        /** 快捷：位置+颜色 */
        virtual void Vertex(const Vec3& pos, const Vec4& color) = 0;

        /** 快捷：位置+法线+UV+颜色 */
        virtual void Vertex(const Vec3& pos, const Vec3& normal,
                            const Vec2& uv, const Vec4& color) = 0;

        /** 添加一个索引 */
        virtual void Index(uint32 i) = 0;

        /** 添加一个三角形（3 顶点 + 3 索引） */
        virtual void Triangle(const PrimitiveVertex& v0,
                              const PrimitiveVertex& v1,
                              const PrimitiveVertex& v2) = 0;

        /** 添加一条线段（2 顶点 + 2 索引，用于 Line 类型） */
        virtual void Line(const Vec3& from, const Vec3& to,
                          const Vec4& color = Vec4(1,1,1,1)) = 0;

        /** 添加一个点 */
        virtual void Point(const Vec3& pos, const Vec4& color = Vec4(1,1,1,1)) = 0;

        // ── 统计 ──

        virtual uint32 GetVertexCount() const = 0;
        virtual uint32 GetIndexCount() const = 0;
        virtual PrimitiveType GetPrimitiveType() const = 0;
    };

} // namespace Engine
