#include "Engine/Core/RHI/PotentiallyVisibleSet.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/MeshComponent.h"
#include "Engine/Core/Renderer/Mesh.h"
#include "Engine/Core/Log.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Engine {

    namespace {
        Logger s_Log("PVS");
    }

    // ============================================================
    // 构建入口
    // ============================================================

    void PotentiallyVisibleSet::Build(const std::vector<GameObject*>& objects,
                                      const Vec3& worldMin,
                                      const Vec3& worldMax,
                                      const Vec3& cellSize,
                                      const PVSConfig& config)
    {
        Clear();
        m_Config = config;
        m_AllObjects = objects;
        m_WorldMin = worldMin;
        m_WorldMax = worldMax;
        m_CellSize = cellSize;

        // 计算网格维度（确保至少 1x1x1）
        float sx = worldMax.x - worldMin.x;
        float sy = worldMax.y - worldMin.y;
        float sz = worldMax.z - worldMin.z;
        if (sx <= 0 || sy <= 0 || sz <= 0) {
            s_Log.Warn("Invalid world bounds, PVS disabled");
            return;
        }

        m_GridWidth  = std::max(1u, (uint32)std::ceil(sx / cellSize.x));
        m_GridHeight = std::max(1u, (uint32)std::ceil(sy / cellSize.y));
        m_GridDepth  = std::max(1u, (uint32)std::ceil(sz / cellSize.z));

        uint32 totalCells = m_GridWidth * m_GridHeight * m_GridDepth;
        m_Cells.resize(totalCells);

        s_Log.Info("PVS grid: {}x{}x{} = {} cells",
                   m_GridWidth, m_GridHeight, m_GridDepth, totalCells);

        // ── 初始化所有单元 ──
        for (uint32 gz = 0; gz < m_GridDepth; ++gz) {
            for (uint32 gy = 0; gy < m_GridHeight; ++gy) {
                for (uint32 gx = 0; gx < m_GridWidth; ++gx) {
                    uint32 idx = ToLinearIndex(gx, gy, gz);
                    auto& cell = m_Cells[idx];
                    cell.center.x = worldMin.x + (gx + 0.5f) * cellSize.x;
                    cell.center.y = worldMin.y + (gy + 0.5f) * cellSize.y;
                    cell.center.z = worldMin.z + (gz + 0.5f) * cellSize.z;
                    cell.halfSize = Vec3(cellSize.x * 0.5f, cellSize.y * 0.5f, cellSize.z * 0.5f);
                    cell.type = CellType::Empty;
                }
            }
        }

        // ── 将物体分配到单元 ──
        AssignObjectsToCells(objects);

        // ── 分类单元（Empty / Solid / Partial / Open） ──
        ClassifyCells();

        // ── 预计算可见性 ──
        ComputeVisibility();

        s_Log.Info("PVS build complete. {} solid cells, {} open/partial cells",
                   std::count_if(m_Cells.begin(), m_Cells.end(),
                       [](const PVSCell& c) { return c.type == CellType::Solid; }),
                   std::count_if(m_Cells.begin(), m_Cells.end(),
                       [](const PVSCell& c) { return c.type != CellType::Empty && c.type != CellType::Solid; }));
    }

    void PotentiallyVisibleSet::Clear()
    {
        m_Cells.clear();
        m_AllObjects.clear();
        m_CachedVisibleCells.clear();
        m_LastCellIndex = UINT32_MAX;
        m_GridWidth = m_GridHeight = m_GridDepth = 0;
    }

    // ============================================================
    // 将物体分配到单元
    // ============================================================

    void PotentiallyVisibleSet::AssignObjectsToCells(const std::vector<GameObject*>& objects)
    {
        for (uint32 objIdx = 0; objIdx < (uint32)objects.size(); ++objIdx) {
            auto* obj = objects[objIdx];
            if (!obj || !obj->IsActive()) continue;

            auto* meshComp = obj->GetComponent<MeshComponent>();
            if (!meshComp || !meshComp->m_Visible || !meshComp->HasMesh())
                continue;

            // 用物体的世界坐标决定它落在哪个单元
            const Vec3& pos = obj->GetTransform().GetPosition();
            int32 gx = (int32)((pos.x - m_WorldMin.x) / m_CellSize.x);
            int32 gy = (int32)((pos.y - m_WorldMin.y) / m_CellSize.y);
            int32 gz = (int32)((pos.z - m_WorldMin.z) / m_CellSize.z);

            // 夹紧到有效范围
            gx = std::max(0, std::min(gx, (int32)m_GridWidth  - 1));
            gy = std::max(0, std::min(gy, (int32)m_GridHeight - 1));
            gz = std::max(0, std::min(gz, (int32)m_GridDepth  - 1));

            uint32 idx = ToLinearIndex((uint32)gx, (uint32)gy, (uint32)gz);
            m_Cells[idx].objectIndices.push_back(objIdx);
        }
    }

    // ============================================================
    // 分类单元
    // ============================================================

    void PotentiallyVisibleSet::ClassifyCells()
    {
        for (auto& cell : m_Cells) {
            if (cell.objectIndices.empty()) {
                cell.type = CellType::Empty;
                continue;
            }

            // 简单启发式：如果单元内物体多且"大"，标记为 Solid
            // 实际项目中会使用更精确的几何分析
            float occupancy = (float)cell.objectIndices.size() / 8.0f; // 经验阈值
            if (occupancy >= m_Config.solidThreshold) {
                cell.type = CellType::Solid;
            } else if (occupancy >= 0.25f) {
                cell.type = CellType::Partial;
            } else {
                cell.type = CellType::Open;
            }
        }
    }

    // ============================================================
    // 预计算可见性
    // ============================================================

    void PotentiallyVisibleSet::ComputeVisibility()
    {
        uint32 total = (uint32)m_Cells.size();

        for (uint32 i = 0; i < total; ++i) {
            auto& cell = m_Cells[i];

            // Solid 单元只能看到自己（被墙壁填充）
            if (cell.type == CellType::Solid) {
                cell.visibleCells.push_back(i);
                continue;
            }
            // Empty 单元也标记为只能看到自己（优化：可改为看到邻居）
            if (cell.type == CellType::Empty) {
                cell.visibleCells.push_back(i);
                continue;
            }

            // Open / Partial 单元：检测到其他所有单元的可见性
            for (uint32 j = 0; j < total; ++j) {
                if (i == j) {
                    cell.visibleCells.push_back(j);
                    continue;
                }

                const auto& other = m_Cells[j];
                // 不需要看到 Empty 单元
                if (other.type == CellType::Empty)
                    continue;

                // 检查从 cell.i 到 cell.j 的视线是否被阻挡
                if (!IsLineBlocked(cell.center, other.center)) {
                    cell.visibleCells.push_back(j);
                }
            }
        }

        // 统计平均可见单元数
        float avgVisible = 0;
        uint32 validCount = 0;
        for (auto& cell : m_Cells) {
            if (cell.type != CellType::Empty && cell.type != CellType::Solid) {
                avgVisible += (float)cell.visibleCells.size();
                validCount++;
            }
        }
        if (validCount > 0) {
            avgVisible /= validCount;
            s_Log.Info("Average visible cells per cell: {:.1f} / {} ({:.1f}%)",
                       avgVisible, total, avgVisible / total * 100.0f);
        }
    }

    // ============================================================
    // 视线遮挡检测（Ray vs AABB 遍历）
    // ============================================================

    bool PotentiallyVisibleSet::IsLineBlocked(const Vec3& from, const Vec3& to) const
    {
        // 先检查起点和终点所在的 Solid 单元
        uint32 fromIdx = GetCellIndex(from);
        uint32 toIdx   = GetCellIndex(to);
        if (fromIdx < m_Cells.size() && m_Cells[fromIdx].type == CellType::Solid)
            return true;
        if (toIdx < m_Cells.size() && m_Cells[toIdx].type == CellType::Solid)
            return true;

        // 使用 3D DDA 算法遍历射线穿过的单元
        glm::vec3 rayOrigin(from.x, from.y, from.z);
        glm::vec3 rayDir(
            to.x - from.x,
            to.y - from.y,
            to.z - from.z
        );
        float maxDist = glm::length(rayDir);
        if (maxDist < 0.001f) return false;
        rayDir /= maxDist;

        // 当前网格坐标
        int32 gx = (int32)((from.x - m_WorldMin.x) / m_CellSize.x);
        int32 gy = (int32)((from.y - m_WorldMin.y) / m_CellSize.y);
        int32 gz = (int32)((from.z - m_WorldMin.z) / m_CellSize.z);

        // 步进方向
        int32 stepX = (rayDir.x > 0) ? 1 : (rayDir.x < 0 ? -1 : 0);
        int32 stepY = (rayDir.y > 0) ? 1 : (rayDir.y < 0 ? -1 : 0);
        int32 stepZ = (rayDir.z > 0) ? 1 : (rayDir.z < 0 ? -1 : 0);

        // 到下一个单元边界的 t 值
        float tDeltaX = (stepX != 0) ? (m_CellSize.x / std::abs(rayDir.x)) : std::numeric_limits<float>::max();
        float tDeltaY = (stepY != 0) ? (m_CellSize.y / std::abs(rayDir.y)) : std::numeric_limits<float>::max();
        float tDeltaZ = (stepZ != 0) ? (m_CellSize.z / std::abs(rayDir.z)) : std::numeric_limits<float>::max();

        // 从射线起点到下一个单元边界的 t 值
        float tMaxX = (stepX > 0)
            ? ((gx + 1) * m_CellSize.x + m_WorldMin.x - from.x) / rayDir.x
            : (stepX < 0)
                ? (gx * m_CellSize.x + m_WorldMin.x - from.x) / rayDir.x
                : std::numeric_limits<float>::max();
        float tMaxY = (stepY > 0)
            ? ((gy + 1) * m_CellSize.y + m_WorldMin.y - from.y) / rayDir.y
            : (stepY < 0)
                ? (gy * m_CellSize.y + m_WorldMin.y - from.y) / rayDir.y
                : std::numeric_limits<float>::max();
        float tMaxZ = (stepZ > 0)
            ? ((gz + 1) * m_CellSize.z + m_WorldMin.z - from.z) / rayDir.z
            : (stepZ < 0)
                ? (gz * m_CellSize.z + m_WorldMin.z - from.z) / rayDir.z
                : std::numeric_limits<float>::max();

        // 遍历射线路径上的单元
        float t = 0.0f;
        const float epsilon = 0.001f;

        while (t < maxDist + epsilon) {
            // 检查当前单元
            if (gx >= 0 && gx < (int32)m_GridWidth &&
                gy >= 0 && gy < (int32)m_GridHeight &&
                gz >= 0 && gz < (int32)m_GridDepth)
            {
                uint32 idx = ToLinearIndex((uint32)gx, (uint32)gy, (uint32)gz);
                if (idx < m_Cells.size() && m_Cells[idx].type == CellType::Solid) {
                    return true;  // 视线被 Solid 单元阻挡
                }
            }

            // 如果已经到达终点单元，停止
            if (gx == (int32)((to.x - m_WorldMin.x) / m_CellSize.x) &&
                gy == (int32)((to.y - m_WorldMin.y) / m_CellSize.y) &&
                gz == (int32)((to.z - m_WorldMin.z) / m_CellSize.z))
            {
                break;
            }

            // 沿最小 t 值方向前进
            if (tMaxX < tMaxY) {
                if (tMaxX < tMaxZ) {
                    gx += stepX;
                    t = tMaxX;
                    tMaxX += tDeltaX;
                } else {
                    gz += stepZ;
                    t = tMaxZ;
                    tMaxZ += tDeltaZ;
                }
            } else {
                if (tMaxY < tMaxZ) {
                    gy += stepY;
                    t = tMaxY;
                    tMaxY += tDeltaY;
                } else {
                    gz += stepZ;
                    t = tMaxZ;
                    tMaxZ += tDeltaZ;
                }
            }

            // 安全检查：超出网格范围
            if (gx < 0 || gx >= (int32)m_GridWidth ||
                gy < 0 || gy >= (int32)m_GridHeight ||
                gz < 0 || gz >= (int32)m_GridDepth)
            {
                break;
            }
        }

        return false;  // 视线未被阻挡
    }

    // ============================================================
    // Ray vs AABB 检测（备用方法）
    // ============================================================

    bool PotentiallyVisibleSet::RayAABBTest(const Vec3& origin, const Vec3& dir,
                                             const Vec3& aabbMin, const Vec3& aabbMax) const
    {
        glm::vec3 o(origin.x, origin.y, origin.z);
        glm::vec3 d(dir.x, dir.y, dir.z);
        glm::vec3 min(aabbMin.x, aabbMin.y, aabbMin.z);
        glm::vec3 max(aabbMax.x, aabbMax.y, aabbMax.z);

        float tMin = 0.0f, tMax = std::numeric_limits<float>::max();

        for (int i = 0; i < 3; ++i) {
            if (std::abs(d[i]) < 0.0001f) {
                if (o[i] < min[i] || o[i] > max[i]) return false;
            } else {
                float t1 = (min[i] - o[i]) / d[i];
                float t2 = (max[i] - o[i]) / d[i];
                if (t1 > t2) std::swap(t1, t2);
                tMin = std::max(tMin, t1);
                tMax = std::min(tMax, t2);
                if (tMin > tMax) return false;
            }
        }
        return true;
    }

    // ============================================================
    // 获取单元索引
    // ============================================================

    uint32 PotentiallyVisibleSet::GetCellIndex(const Vec3& position) const
    {
        int32 gx = (int32)((position.x - m_WorldMin.x) / m_CellSize.x);
        int32 gy = (int32)((position.y - m_WorldMin.y) / m_CellSize.y);
        int32 gz = (int32)((position.z - m_WorldMin.z) / m_CellSize.z);

        gx = std::max(0, std::min(gx, (int32)m_GridWidth  - 1));
        gy = std::max(0, std::min(gy, (int32)m_GridHeight - 1));
        gz = std::max(0, std::min(gz, (int32)m_GridDepth  - 1));

        return ToLinearIndex((uint32)gx, (uint32)gy, (uint32)gz);
    }

    // ============================================================
    // 获取可见单元列表（带缓存）
    // ============================================================

    const std::vector<uint32>& PotentiallyVisibleSet::GetVisibleCells(const Vec3& position) const
    {
        if (!m_Config.enableCulling || m_Cells.empty()) {
            static std::vector<uint32> s_all;
            if (s_all.empty()) {
                s_all.resize(m_Cells.size());
                for (uint32 i = 0; i < (uint32)m_Cells.size(); ++i)
                    s_all[i] = i;
            }
            return s_all;
        }

        uint32 cellIdx = GetCellIndex(position);

        // 缓存：同一单元内直接返回
        if (cellIdx == m_LastCellIndex && !m_CachedVisibleCells.empty()) {
            return m_CachedVisibleCells;
        }

        m_LastCellIndex = cellIdx;
        m_CachedVisibleCells = m_Cells[cellIdx].visibleCells;
        return m_CachedVisibleCells;
    }

    // ============================================================
    // 获取可见物体
    // ============================================================

    void PotentiallyVisibleSet::GetVisibleObjects(const Vec3& position,
                                                   std::vector<GameObject*>& outObjects) const
    {
        outObjects.clear();

        if (m_Cells.empty() || !m_Config.enableCulling) {
            // PVS 未启用或未构建：返回所有物体
            outObjects = m_AllObjects;
            return;
        }

        const auto& visibleCells = GetVisibleCells(position);

        // 收集所有可见单元内的物体
        for (uint32 cellIdx : visibleCells) {
            if (cellIdx >= m_Cells.size()) continue;
            const auto& cell = m_Cells[cellIdx];
            for (uint32 objIdx : cell.objectIndices) {
                if (objIdx < m_AllObjects.size()) {
                    outObjects.push_back(m_AllObjects[objIdx]);
                }
            }
        }
    }

} // namespace Engine
