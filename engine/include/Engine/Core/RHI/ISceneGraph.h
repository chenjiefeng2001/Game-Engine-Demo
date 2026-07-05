#pragma once

/**
 * @file ISceneGraph.h
 * @brief 场景图抽象接口 — 适配不同游戏类型的空间管理策略
 *
 * 内置实现：
 *   SceneGraphType::Flat  — 线性列表（小场景，无剔除开销）
 *   SceneGraphType::BVH   — 层次包围盒（通用 3D 场景，均衡性能）
 *   SceneGraphType::Grid  — 均匀网格（大量小型物体，粒子系统）
 *
 * 可通过 SceneGraphManager 自动选择或手动切换。
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <string>
#include <vector>
#include <cstdint>

namespace Engine {

    class GameObject;
    class PerspectiveCamera;

    // ── 场景图类型枚举 ──
    enum class SceneGraphType : uint8 {
        Flat   = 0,   // 线性列表 — 无剔除，适合 < 50 物体
        BVH    = 1,   // 层次包围盒 — 通用 3D，适合 50~10000 物体
        Grid   = 2,   // 均匀网格 — 粒子/大量小物体
        Auto   = 255, // 自动选择
    };

    inline const char* SceneGraphTypeName(SceneGraphType t) {
        switch (t) {
            case SceneGraphType::Flat: return "Flat (List)";
            case SceneGraphType::BVH:  return "BVH (Hierarchy)";
            case SceneGraphType::Grid: return "Grid (Spatial)";
            case SceneGraphType::Auto: return "Auto";
            default:                   return "Unknown";
        }
    }

    // ── AABB 包围盒（与所有场景图共享） ──
    struct AABB {
        Vec3 min = { 1e30f,  1e30f,  1e30f};
        Vec3 max = {-1e30f, -1e30f, -1e30f};

        void Expand(const Vec3& point);
        void Expand(const AABB& other);
        AABB  Transformed(const class Mat4& worldMatrix) const;
        Vec3  Center() const { return Vec3((min.x+max.x)*0.5f, (min.y+max.y)*0.5f, (min.z+max.z)*0.5f); }
        bool  IsValid() const { return min.x <= max.x; }
    };

    // ── 平截头体（6 个平面方程，供剔除使用） ──
    struct Frustum {
        Vec4 planes[6];
        enum Side { Left=0, Right=1, Bottom=2, Top=3, Near=4, Far=5 };

        void Extract(const class Mat4& viewProj);
        bool TestAABB(const AABB& aabb) const;
    };

    // ── 场景图统计 ──
    struct SceneGraphStats {
        uint32 totalObjects  = 0;
        uint32 visibleCount  = 0;
        uint32 nodeCount     = 0;       // BVH/Grid 内部节点数
        float  buildTimeMs   = 0.0f;    // 构建耗时
        float  queryTimeMs   = 0.0f;    // 查询耗时
        float  cullRatio     = 1.0f;    // visible / total
    };

    // ── 场景图配置 ──
    struct SceneGraphConfig {
        SceneGraphType type = SceneGraphType::Auto;
        // Grid 专用
        Vec3  gridCellSize = Vec3(8.0f, 8.0f, 8.0f);
        // BVH 专用
        uint32 bvhMaxLeafSize = 4;
        // 通用
        uint32 autoThreshold = 50;  // Auto 模式下切换的物体数阈值
    };

    // ═══════════════════════════════════════════════
    // 场景图抽象接口
    // ═══════════════════════════════════════════════
    class ISceneGraph {
    public:
        virtual ~ISceneGraph() = default;

        /** @brief 场景图类型 */
        virtual SceneGraphType GetType() const = 0;

        /** @brief 名称（调试用） */
        virtual const char* GetName() const = 0;

        // ── 生命周期 ──

        /** 从物体列表构建空间结构 */
        virtual void Build(std::vector<GameObject*>& objects) = 0;

        /** 清空所有数据 */
        virtual void Clear() = 0;

        /** 是否已构建 */
        virtual bool IsValid() const = 0;

        // ── 平截头体 ──

        /** 从相机设置平截头体 */
        virtual void SetFrustum(const PerspectiveCamera* camera) = 0;
        virtual void SetFrustum(const Frustum& frustum) = 0;
        virtual const Frustum& GetFrustum() const = 0;
        virtual bool HasFrustum() const = 0;

        // ── 可见性查询 ──

        /** 获取平截头体内可见的物体列表 */
        virtual void GetVisibleObjects(std::vector<GameObject*>& outObjects) const = 0;

        /** 获取场景中所有物体 */
        virtual uint32 GetObjectCount() const = 0;

        // ── 统计 ──

        virtual SceneGraphStats GetStats() const = 0;
    };

} // namespace Engine
