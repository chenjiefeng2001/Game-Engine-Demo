#pragma once

/**
 * @file BVHSceneGraph.h
 * @brief BVH 场景图 — 基于包围盒层次结构的空间管理
 *
 * 实现 ISceneGraph 接口。
 * 将物体递归分割到 BVH 树中，支持快速平截头体剔除。
 * 适用于 50~10000 个物体的通用 3D 场景。
 */

#include "Engine/Core/RHI/ISceneGraph.h"
#include <memory>
#include <vector>
#include <cstdint>

namespace Engine {

    class GameObject;
    class PerspectiveCamera;

    // ── BVH 节点 ──
    struct BVHNode {
        AABB     bounds;
        uint32   startIndex = 0;    // 物体起始索引（叶子节点）
        uint32   endIndex   = 0;    // 物体结束索引（叶子节点）
        uint32   leftChild  = UINT32_MAX;
        uint32   rightChild = UINT32_MAX;
        bool     isLeaf() const { return leftChild == UINT32_MAX; }
    };

    // ── BVH 场景图 ──
    class BVHSceneGraph : public ISceneGraph {
    public:
        BVHSceneGraph() = default;
        virtual ~BVHSceneGraph() override = default;

        // ISceneGraph
        virtual SceneGraphType GetType() const override { return SceneGraphType::BVH; }
        virtual const char* GetName() const override { return "BVH Scene Graph"; }

        virtual void Build(std::vector<GameObject*>& objects) override;
        virtual void Clear() override;
        virtual bool IsValid() const override { return !m_Nodes.empty(); }

        virtual void SetFrustum(const PerspectiveCamera* camera) override;
        virtual void SetFrustum(const Frustum& frustum) override { m_Frustum = frustum; m_FrustumValid = true; }
        virtual const Frustum& GetFrustum() const override { return m_Frustum; }
        virtual bool HasFrustum() const override { return m_FrustumValid; }

        virtual void GetVisibleObjects(std::vector<GameObject*>& outObjects) const override;
        virtual uint32 GetObjectCount() const override { return (uint32)m_Objects.size(); }

        virtual SceneGraphStats GetStats() const override;

        // BVH 专用配置
        void SetMaxLeafSize(uint32 size) { m_MaxLeafSize = size; }

    private:
        uint32 BuildRecursive(uint32 start, uint32 end, uint32 depth);
        void   Traverse(uint32 nodeIndex, std::vector<GameObject*>& outObjects) const;
        void   SortByAxis(uint32 start, uint32 end, int axis);

        std::vector<BVHNode>      m_Nodes;
        std::vector<GameObject*>  m_Objects;
        std::vector<AABB>         m_ObjectBounds;

        Frustum   m_Frustum;
        bool      m_FrustumValid = false;
        mutable uint32 m_LastVisibleCount = 0;
        uint32    m_MaxLeafSize = 4;
        static constexpr uint32 kMaxDepth = 32;
    };

} // namespace Engine
