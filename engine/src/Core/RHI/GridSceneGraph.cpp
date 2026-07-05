#include "Engine/Core/RHI/GridSceneGraph.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/MeshComponent.h"
#include "Engine/Core/Renderer/Mesh.h"
#include "Engine/Core/Renderer/PerspectiveCamera.h"
#include <chrono>
#include <cstring>
#include <cmath>

namespace Engine {

    int32 GridSceneGraph::ToCellIndex(const Vec3& pos, int axis) const {
        float size = (&m_CellSize.x)[axis];
        float min  = (&m_WorldMin.x)[axis];
        int idx = (int32)((pos.x - min) / size); // 用 pos.x 但只用于单轴
        // 正确写法
        idx = (int32)(((&pos.x)[axis] - (&m_WorldMin.x)[axis]) / size);
        if (idx < 0) idx = 0;
        if (axis == 0 && idx >= m_GridW) idx = m_GridW - 1;
        if (axis == 1 && idx >= m_GridH) idx = m_GridH - 1;
        if (axis == 2 && idx >= m_GridD) idx = m_GridD - 1;
        return idx;
    }

    void GridSceneGraph::Build(std::vector<GameObject*>& objects) {
        Clear();
        if (objects.empty()) return;

        m_Objects = objects;

        // 计算世界边界
        m_WorldMin = Vec3( 1e30f,  1e30f,  1e30f);
        m_WorldMax = Vec3(-1e30f, -1e30f, -1e30f);
        for (auto* obj : objects) {
            if (!obj || !obj->IsActive()) continue;
            const Vec3& pos = obj->GetTransform().GetPosition();
            if (pos.x < m_WorldMin.x) m_WorldMin.x = pos.x;
            if (pos.y < m_WorldMin.y) m_WorldMin.y = pos.y;
            if (pos.z < m_WorldMin.z) m_WorldMin.z = pos.z;
            if (pos.x > m_WorldMax.x) m_WorldMax.x = pos.x;
            if (pos.y > m_WorldMax.y) m_WorldMax.y = pos.y;
            if (pos.z > m_WorldMax.z) m_WorldMax.z = pos.z;
        }
        // 确保最小范围
        Vec3 size(m_WorldMax.x - m_WorldMin.x, m_WorldMax.y - m_WorldMin.y, m_WorldMax.z - m_WorldMin.z);
        if (size.x < 1.0f) { size.x = 1.0f; m_WorldMax.x = m_WorldMin.x + 1.0f; }
        if (size.y < 1.0f) { size.y = 1.0f; m_WorldMax.y = m_WorldMin.y + 1.0f; }
        if (size.z < 1.0f) { size.z = 1.0f; m_WorldMax.z = m_WorldMin.z + 1.0f; }

        m_GridW = std::max(1, (int32)std::ceil(size.x / m_CellSize.x));
        m_GridH = std::max(1, (int32)std::ceil(size.y / m_CellSize.y));
        m_GridD = std::max(1, (int32)std::ceil(size.z / m_CellSize.z));

        int32 totalCells = m_GridW * m_GridH * m_GridD;
        m_Cells.resize(totalCells);

        // 初始化每个单元的包围盒
        for (int32 gz = 0; gz < m_GridD; ++gz)
            for (int32 gy = 0; gy < m_GridH; ++gy)
                for (int32 gx = 0; gx < m_GridW; ++gx) {
                    int32 idx = gz * m_GridW * m_GridH + gy * m_GridW + gx;
                    auto& cell = m_Cells[idx];
                    cell.bounds.min = Vec3(
                        m_WorldMin.x + gx * m_CellSize.x,
                        m_WorldMin.y + gy * m_CellSize.y,
                        m_WorldMin.z + gz * m_CellSize.z);
                    cell.bounds.max = Vec3(
                        cell.bounds.min.x + m_CellSize.x,
                        cell.bounds.min.y + m_CellSize.y,
                        cell.bounds.min.z + m_CellSize.z);
                }

        // 将物体分配到单元
        for (uint32 objIdx = 0; objIdx < (uint32)m_Objects.size(); ++objIdx) {
            auto* obj = m_Objects[objIdx];
            if (!obj || !obj->IsActive()) continue;
            const Vec3& pos = obj->GetTransform().GetPosition();
            int32 gx = (int32)((pos.x - m_WorldMin.x) / m_CellSize.x);
            int32 gy = (int32)((pos.y - m_WorldMin.y) / m_CellSize.y);
            int32 gz = (int32)((pos.z - m_WorldMin.z) / m_CellSize.z);
            gx = std::max(0, std::min(gx, m_GridW - 1));
            gy = std::max(0, std::min(gy, m_GridH - 1));
            gz = std::max(0, std::min(gz, m_GridD - 1));
            int32 idx = gz * m_GridW * m_GridH + gy * m_GridW + gx;
            m_Cells[idx].objectIndices.push_back(objIdx);
        }
    }

    void GridSceneGraph::Clear() {
        m_Objects.clear();
        m_Cells.clear();
        m_FrustumValid = false;
        m_LastVisible = 0;
    }

    void GridSceneGraph::SetFrustum(const PerspectiveCamera* camera) {
        if (!camera) { m_FrustumValid = false; return; }
        const float* vpData = const_cast<PerspectiveCamera*>(camera)->GetViewProjectionMatrixPtr();
        Mat4 vpMat;
        std::memcpy(vpMat.Data(), vpData, sizeof(float) * 16);
        m_Frustum.Extract(vpMat);
        m_FrustumValid = true;
    }

    void GridSceneGraph::SetFrustum(const Frustum& frustum) {
        m_Frustum = frustum;
        m_FrustumValid = true;
    }

    void GridSceneGraph::GetVisibleObjects(std::vector<GameObject*>& outObjects) const {
        outObjects.clear();
        m_LastVisible = 0;

        for (auto& cell : m_Cells) {
            // 平截头体测试单元
            if (m_FrustumValid && !m_Frustum.TestAABB(cell.bounds))
                continue;

            for (uint32 objIdx : cell.objectIndices) {
                if (objIdx < (uint32)m_Objects.size()) {
                    outObjects.push_back(m_Objects[objIdx]);
                    m_LastVisible++;
                }
            }
        }
    }

    SceneGraphStats GridSceneGraph::GetStats() const {
        SceneGraphStats s;
        s.totalObjects = (uint32)m_Objects.size();
        s.visibleCount = m_LastVisible;
        s.nodeCount = (uint32)m_Cells.size();
        s.cullRatio = m_Objects.empty() ? 1.0f :
                      (float)m_LastVisible / (float)m_Objects.size();
        return s;
    }

} // namespace Engine
