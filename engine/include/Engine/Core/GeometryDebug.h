#pragma once

/**
 * @file GeometryDebug.h
 * @brief 几何体与空间管理调试数据 — 用于图形调试菜单的 Geometry & Culling 面板
 *
 * 引擎渲染器每帧填充此结构，PerformanceWindow 读取并显示 UI。
 * UI 中的开关直接修改此结构中的标志位，渲染器消费。
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/ISceneGraph.h"

namespace Engine {

    /// LOD 颜色可视化配置
    struct LODDebugInfo {
        bool  visualizeLOD    = false;   // 用颜色显示 LOD 等级
        uint32 currentLODCount = 0;      // 当前使用的 LOD 等级数
        // LOD 0 = 绿, LOD 1 = 黄, LOD 2 = 橙, LOD 3+ = 红
    };

    /// 几何体与空间管理调试帧数据
    struct GeometryDebugFrameData {
        // ── 包围盒调试 ──
        bool showAABB       = false;  // 显示所有物体的 AABB 线框
        bool showOBB        = false;  // 显示朝向包围盒 (Oriented Bounding Box)
        bool showOnlyCulled = false;  // 只显示被剔除的物体的包围盒（红色）
        AABB sceneBounds;            // 场景总包围盒

        // ── 视锥剔除调试 ──
        bool  frustumFreeze   = false;  // 冻结当前视锥
        Frustum frozenFrustum;          // 冻结的视锥
        uint32 totalObjects    = 0;     // 场景总物体数
        uint32 visibleObjects  = 0;     // 视锥内可见物体数
        uint32 culledObjects   = 0;     // 被剔除物体数
        bool   frustumCullingEnabled = true; // 是否启用视锥剔除

        // ── LOD 调试图 ──
        LODDebugInfo lod;

        // ── 遮挡剔除调试 ──
        bool occlusionCullingEnabled = true;
        uint32 occludedObjects       = 0; // 被遮挡剔除的物体数
        bool   showOcclusionBounds   = false; // 显示遮挡物包围盒

        // ── 场景图统计 ──
        SceneGraphStats sceneGraphStats;

        /// 重置默认值
        void Reset() {
            showAABB = false;
            showOBB  = false;
            showOnlyCulled = false;
            frustumFreeze  = false;
            frustumCullingEnabled = true;
            totalObjects = 0;
            visibleObjects = 0;
            culledObjects = 0;
            lod.visualizeLOD = false;
            lod.currentLODCount = 0;
            occlusionCullingEnabled = true;
            occludedObjects = 0;
            showOcclusionBounds = false;
        }
    };

} // namespace Engine