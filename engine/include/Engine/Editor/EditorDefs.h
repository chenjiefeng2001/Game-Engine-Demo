#pragma once

/**
 * @file EditorDefs.h
 * @brief 编辑器公共类型定义 — 变换工具、吸附、书签、过滤等
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <string>
#include <vector>
#include <functional>

namespace Engine {

    // ═══════════════════════════════════════════════════════════════
    // 0. 面板可见性
    // ═══════════════════════════════════════════════════════════════

    struct PanelVisibility {
        // ── 基础面板 ──
        bool sceneHierarchy = true;   // 实体层级树
        bool inspector      = true;
        bool console        = true;
        bool performance    = true;
        bool contentBrowser = true;
        bool assetBrowser   = true;
        bool depGraph       = false;
        bool viewport       = true;
        bool rendererDebug  = false;

        // ── 工具面板 ──
        bool shaderGraph    = false;  // 着色器图编辑器
        bool vfxEditor      = false;  // VFX 编辑器
        bool animationEditor = false; // 动画编辑器

        // ── 场景面板 ──
        bool sceneManager    = true;
        bool sceneViewerPanel = true;
        bool scenePanelTabbed = false;
    };

    // ═══════════════════════════════════════════════════════════════
    // 1. 变换工具模式
    // ═══════════════════════════════════════════════════════════════

    enum class GizmoOperation : uint8 {
        Translate   = 0,
        Rotate      = 1,
        Scale       = 2,
        Rect        = 3,   ///< 矩形选择
        COUNT
    };

    enum class GizmoSpace : uint8 {
        World   = 0,
        Local   = 1,
        Parent  = 2,   ///< 以父级坐标系为参考
        COUNT
    };

    enum class PivotMode : uint8 {
        Center  = 0,   ///< 围绕选中物体的平均中心
        Pivot   = 1,   ///< 围绕各自轴心
        COUNT
    };

    inline const char* GizmoOperationName(GizmoOperation op) {
        switch (op) {
            case GizmoOperation::Translate: return "Translate";
            case GizmoOperation::Rotate:    return "Rotate";
            case GizmoOperation::Scale:     return "Scale";
            case GizmoOperation::Rect:      return "Rect Select";
            default: return "Unknown";
        }
    }

    inline const char* GizmoSpaceName(GizmoSpace s) {
        switch (s) {
            case GizmoSpace::World:  return "World";
            case GizmoSpace::Local:  return "Local";
            case GizmoSpace::Parent: return "Parent";
            default: return "Unknown";
        }
    }

    inline const char* PivotModeName(PivotMode p) {
        switch (p) {
            case PivotMode::Center: return "Center";
            case PivotMode::Pivot:  return "Pivot";
            default: return "Unknown";
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 2. 吸附模式
    // ═══════════════════════════════════════════════════════════════

    enum class SnapMode : uint8 {
        Grid    = 0,   ///< 网格吸附
        Vertex  = 1,   ///< 顶点吸附（需按住 V 键）
        Surface = 2,   ///< 表面贴合
        COUNT
    };

    inline const char* SnapModeName(SnapMode m) {
        switch (m) {
            case SnapMode::Grid:    return "Grid Snap";
            case SnapMode::Vertex:  return "Vertex Snap";
            case SnapMode::Surface: return "Surface Placement";
            default: return "Unknown";
        }
    }

    struct SnapConfig {
        SnapMode mode        = SnapMode::Grid;
        bool     enabled     = false;
        float    translateSnap = 0.5f;
        float    rotateSnap    = 15.0f;  ///< 度
        float    scaleSnap     = 0.1f;

        // 顶点吸附 — 临时覆盖
        bool     vertexSnapHeld = false;
    };

    // ═══════════════════════════════════════════════════════════════
    // 3. 选择过滤掩码
    // ═══════════════════════════════════════════════════════════════

    struct SelectionMask {
        bool allowGeometry  = true;
        bool allowLights    = true;
        bool allowAudio     = true;
        bool allowTriggers  = true;
        bool allowParticles = true;
        bool allowCameras   = true;
        bool allowColliders = false;  ///< 默认不可框选碰撞体
    };

    // ═══════════════════════════════════════════════════════════════
    // 4. 相机书签
    // ═══════════════════════════════════════════════════════════════

    struct CameraBookmark {
        std::string name;
        Vec3 position;
        Vec3 focusPoint;
        float distance;
        float yaw;
        float pitch;
        float orbitYaw;
        float orbitPitch;
    };

    // ═══════════════════════════════════════════════════════════════
    // 5. 性能统计悬浮窗
    // ═══════════════════════════════════════════════════════════════

    struct StatsOverlayConfig {
        bool visible     = true;
        bool showFPS     = true;
        bool showDrawCalls = true;
        bool showVerts   = true;
        bool showVRAM    = true;
        bool showBatches = true;
        float posX       = 10.0f;
        float posY       = 30.0f;
    };

    // ═══════════════════════════════════════════════════════════════
    // 6. 孤立模式状态
    // ═══════════════════════════════════════════════════════════════

    struct IsolationState {
        bool  active = false;
        uint32 isolatedObjectId = 0;
        std::string sceneName;
    };

} // namespace Engine