#pragma once

/**
 * @file SkinnedMesh.h
 * @brief 蒙皮网格 — 带骨骼绑定数据的网格
 *
 * SkinnedMesh 继承/包含 Mesh 的几何数据，并扩展了
 * 每个顶点的骨骼索引和权重信息，用于 GPU 蒙皮。
 *
 * 每个顶点最多受 4 根骨骼影响，权重之和为 1.0。
 * 布局与 GPU 着色器中的顶点属性匹配：
 *   layout(location = 0) = position  (vec3)
 *   layout(location = 1) = normal    (vec3)
 *   layout(location = 2) = texCoord  (vec2)
 *   layout(location = 3) = tangent   (vec3)
 *   layout(location = 4) = boneIndices (ivec4)
 *   layout(location = 5) = boneWeights (vec4)
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <vector>
#include <cstdint>

namespace Engine {

    // ============================================================
    // 蒙皮顶点（扩展 Vertex3D，加入骨骼绑定数据）
    // ============================================================
    struct SkinnedVertex {
        Vec3    position;       ///< 顶点位置（模型空间，绑定姿势）
        Vec3    normal;         ///< 法线
        Vec2    texCoord;       ///< 纹理坐标
        Vec3    tangent;        ///< 切线

        // ── 蒙皮数据 ──
        uint32  boneIndices[4] = {0, 0, 0, 0};  ///< 影响的骨骼索引
        float32 boneWeights[4] = {0.0f, 0.0f, 0.0f, 0.0f}; ///< 对应权重

        SkinnedVertex() = default;

        SkinnedVertex(const Vec3& pos, const Vec3& nml, const Vec2& uv)
            : position(pos), normal(nml), texCoord(uv), tangent(0, 0, 0) {}

        /** 添加骨骼影响 */
        void AddBoneInfluence(uint32 boneIndex, float32 weight) {
            for (int i = 0; i < 4; ++i) {
                if (boneWeights[i] == 0.0f) {
                    boneIndices[i] = boneIndex;
                    boneWeights[i] = weight;
                    return;
                }
            }
        }

        /** 归一化权重（确保和为 1.0） */
        void NormalizeWeights() {
            float32 sum = 0.0f;
            for (int i = 0; i < 4; ++i) sum += boneWeights[i];
            if (sum > 0.0f) {
                for (int i = 0; i < 4; ++i) boneWeights[i] /= sum;
            }
        }
    };

    // ============================================================
    // 蒙皮网格
    // ============================================================
    class SkinnedMesh {
    public:
        SkinnedMesh() = default;

        SkinnedMesh(const std::vector<SkinnedVertex>& vertices,
                    const std::vector<uint32>& indices)
            : m_Vertices(vertices), m_Indices(indices) {}

        // ── 数据访问 ──
        const std::vector<SkinnedVertex>& GetVertices() const noexcept { return m_Vertices; }
        const std::vector<uint32>&        GetIndices()  const noexcept { return m_Indices; }

        std::vector<SkinnedVertex>& GetVertices() noexcept { return m_Vertices; }
        std::vector<uint32>&        GetIndices()  noexcept { return m_Indices; }

        void SetVertices(const std::vector<SkinnedVertex>& verts) { m_Vertices = verts; }
        void SetIndices(const std::vector<uint32>& indices)       { m_Indices  = indices; }

        size_t GetVertexCount() const noexcept { return m_Vertices.size(); }
        size_t GetIndexCount()  const noexcept { return m_Indices.size(); }
        bool   IsValid()        const noexcept { return !m_Vertices.empty() && !m_Indices.empty(); }

        // ── 骨骼映射 ──
        /**
         * @brief 设置骨骼名称到索引的映射
         *
         * 当 Skeleton 的骨骼索引与 SkinnedMesh 中引用的骨骼索引不一致时，
         * 需要此映射将顶点中的 boneIndex 重映射到 Skeleton 中的实际索引。
         *
         * mapping[b] = s 表示 SkinnedMesh 的第 b 号骨骼对应 Skeleton 的第 s 号骨骼。
         */
        void SetBoneMapping(const std::vector<int32>& mapping) { m_BoneMapping = mapping; }
        const std::vector<int32>& GetBoneMapping() const noexcept { return m_BoneMapping; }

        /** 重映射顶点中的骨骼索引 */
        void RemapBoneIndices(const std::vector<int32>& mapping);

    private:
        std::vector<SkinnedVertex> m_Vertices;
        std::vector<uint32>        m_Indices;
        std::vector<int32>         m_BoneMapping;  ///< SkinnedMesh 骨骼索引 → Skeleton 骨骼索引
    };

} // namespace Engine
