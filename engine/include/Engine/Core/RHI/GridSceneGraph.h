#pragma once

#include "Engine/Core/RHI/ISceneGraph.h"
#include <vector>

namespace Engine {

    /// 均匀网格场景图 — 将空间划分为 3D 网格
    /// 适用于大量均匀分布的小物体（粒子、弹幕等）
    class GridSceneGraph : public ISceneGraph {
    public:
        GridSceneGraph() = default;
        virtual ~GridSceneGraph() override = default;

        virtual SceneGraphType GetType() const override { return SceneGraphType::Grid; }
        virtual const char* GetName() const override { return "Grid Scene Graph"; }

        virtual void Build(std::vector<GameObject*>& objects) override;
        virtual void Clear() override;
        virtual bool IsValid() const override { return !m_Cells.empty(); }

        virtual void SetFrustum(const PerspectiveCamera* camera) override;
        virtual void SetFrustum(const Frustum& frustum) override;
        virtual const Frustum& GetFrustum() const override { return m_Frustum; }
        virtual bool HasFrustum() const override { return m_FrustumValid; }

        virtual void GetVisibleObjects(std::vector<GameObject*>& outObjects) const override;
        virtual uint32 GetObjectCount() const override { return (uint32)m_Objects.size(); }

        virtual SceneGraphStats GetStats() const override;

        /// 设置网格单元大小（需在 Build 前调用）
        void SetCellSize(const Vec3& size) { m_CellSize = size; }

    private:
        struct Cell {
            AABB bounds;
            std::vector<uint32> objectIndices;
        };

        std::vector<GameObject*> m_Objects;
        std::vector<Cell> m_Cells;
        Frustum m_Frustum;
        bool m_FrustumValid = false;

        Vec3 m_CellSize = Vec3(8.0f, 8.0f, 8.0f);
        int32 m_GridW = 0, m_GridH = 0, m_GridD = 0;
        Vec3 m_WorldMin, m_WorldMax;
        mutable uint32 m_LastVisible = 0;

        int32 ToCellIndex(const Vec3& pos, int axis) const;
    };

} // namespace Engine
