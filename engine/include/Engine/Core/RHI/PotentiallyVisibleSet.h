#pragma once

/**
 * @file PotentiallyVisibleSet.h
 * @brief 潜在可见集 (PVS) — 预计算空间可见性，加速渲染剔除
 *
 * 原理：
 *   1. 将场景划分为均匀 3D 网格单元 (Cell)
 *   2. 预计算每个单元能「看到」哪些其他单元
 *   3. 运行时根据相机所在单元，只渲染可见单元内的物体
 *
 * 单元类型：
 *   - Empty:  无物体，不参与遮挡
 *   - Solid:  完全填充（如墙壁），射线无法穿过
 *   - Partial: 部分填充，需要具体的射线检测
 *   - Open:   开放空间，射线自由通过
 *
 * 使用示例：
 * @code
 *   PotentiallyVisibleSet pvs;
 *   pvs.Build(objects, Vec3(-20,-10,-20), Vec3(20,10,20), Vec3(4,4,4));
 *
 *   // 渲染时
 *   std::vector<GameObject*> visible;
 *   pvs.GetVisibleObjects(cameraPosition, visible);
 *   meshRenderer.Render(visible);
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <vector>
#include <cstdint>

namespace Engine {

    class GameObject;

    // ──────────────────────────────────────────
    // PVS 单元类型
    // ──────────────────────────────────────────
    enum class CellType : uint8 {
        Empty   = 0,  // 无物体
        Open    = 1,  // 开放空间，射线自由通过
        Partial = 2,  // 部分填充，需射线检测
        Solid   = 3,  // 完全填充（墙体），射线阻挡
    };

    // ──────────────────────────────────────────
    // 单个 PVS 单元
    // ──────────────────────────────────────────
    struct PVSCell {
        Vec3     center;               // 世界坐标中心
        Vec3     halfSize;             // 半边长
        CellType type = CellType::Empty;

        // 从此单元可见的其它单元索引
        std::vector<uint32> visibleCells;

        // 落在此单元内的物体索引（指向外部物体数组）
        std::vector<uint32> objectIndices;
    };

    // ──────────────────────────────────────────
    // PVS 配置参数
    // ──────────────────────────────────────────
    struct PVSConfig {
        Vec3   cellSize      = Vec3(4.0f, 4.0f, 4.0f);  // 单元大小
        float  solidThreshold = 0.5f;   // 单元内物体包围盒体积占比 ≥ 此值 → Solid
        bool   enableCulling = true;    // 启用/禁用 PVS 剔除
        bool   debugDraw     = false;   // 绘制单元边框
    };

    // ──────────────────────────────────────────
    // PVS 主类
    // ──────────────────────────────────────────
    class PotentiallyVisibleSet {
    public:
        PotentiallyVisibleSet() = default;
        ~PotentiallyVisibleSet() = default;

        PotentiallyVisibleSet(const PotentiallyVisibleSet&) = delete;
        PotentiallyVisibleSet& operator=(const PotentiallyVisibleSet&) = delete;

        // ── 构建 ──
        /**
         * @brief 从场景物体列表构建 PVS
         * @param objects  场景中所有 GameObject 指针
         * @param worldMin 世界坐标最小值（栅格边界）
         * @param worldMax 世界坐标最大值
         * @param cellSize 每个单元的大小
         * @param config   可选配置
         */
        void Build(const std::vector<GameObject*>& objects,
                   const Vec3& worldMin,
                   const Vec3& worldMax,
                   const Vec3& cellSize,
                   const PVSConfig& config = PVSConfig{});

        /// 清空 PVS 数据
        void Clear();

        /// PVS 是否已构建
        bool IsValid() const { return !m_Cells.empty(); }

        // ── 运行时查询 ──

        /// 获取相机位置所在的单元索引
        uint32 GetCellIndex(const Vec3& position) const;

        /// 获取从指定位置可见的单元索引列表
        const std::vector<uint32>& GetVisibleCells(const Vec3& position) const;

        /**
         * @brief 获取从指定位置可见的物体列表
         * @param position   相机位置（世界坐标）
         * @param outObjects [输出] 可见物体指针
         */
        void GetVisibleObjects(const Vec3& position,
                               std::vector<GameObject*>& outObjects) const;

        /// 获取所有物体（不剔除，用于调试对比）
        const std::vector<GameObject*>& GetAllObjects() const { return m_AllObjects; }

        // ── 配置 ──
        void SetConfig(const PVSConfig& config) { m_Config = config; }
        const PVSConfig& GetConfig() const { return m_Config; }

        // ── 调试 ──
        uint32 GetCellCount() const { return (uint32)m_Cells.size(); }
        uint32 GetVisibleCellCount(const Vec3& pos) const {
            return (uint32)GetVisibleCells(pos).size();
        }
        const PVSCell& GetCell(uint32 index) const { return m_Cells[index]; }

    private:
        // ── 内部构建 ──
        void AssignObjectsToCells(const std::vector<GameObject*>& objects);
        void ClassifyCells();
        void ComputeVisibility();
        bool IsLineBlocked(const Vec3& from, const Vec3& to) const;
        bool RayAABBTest(const Vec3& origin, const Vec3& dir,
                         const Vec3& aabbMin, const Vec3& aabbMax) const;
        uint32 ToLinearIndex(uint32 gx, uint32 gy, uint32 gz) const {
            return gz * m_GridWidth * m_GridHeight + gy * m_GridWidth + gx;
        }

        // ── 数据 ──
        std::vector<PVSCell>    m_Cells;
        std::vector<GameObject*> m_AllObjects;  // 外部物体指针快照

        Vec3  m_WorldMin, m_WorldMax;
        Vec3  m_CellSize;
        uint32 m_GridWidth  = 0;
        uint32 m_GridHeight = 0;
        uint32 m_GridDepth  = 0;

        PVSConfig m_Config;

        // 缓存可见单元（避免每帧分配）
        mutable uint32              m_LastCellIndex = UINT32_MAX;
        mutable std::vector<uint32> m_CachedVisibleCells;
    };

} // namespace Engine
