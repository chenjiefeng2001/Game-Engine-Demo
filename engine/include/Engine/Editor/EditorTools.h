#pragma once

/**
 * @file EditorTools.h
 * @brief 编辑器增强工具集 — 变换操作、吸附、书签、选择过滤、统计悬浮窗
 *
 * 整合了工业级编辑器所需的进阶功能模块:
 *   1. 万向操作杆 (World/Local/Parent, Center/Pivot, 平面约束)
 *   2. 极速吸附 (Grid/Vertex/Surface)
 *   3. 相机书签系统 (Ctrl+1-9 保存, 1-9 加载)
 *   4. 选择过滤掩码 (Geometry/Lights/Audio/Triggers...)
 *   5. 层级可见性管理 + 孤立模式 (Isolation)
 *   6. 性能统计悬浮窗 (FPS/DrawCalls/VRAM)
 *   7. 摄像机聚焦
 */

#include "Engine/Types.h"
#include "Engine/Editor/EditorDefs.h"
#include "Engine/Editor/EditorCamera.h"
#include "Engine/Core/RHI/MathTypes.h"

namespace Engine {

    // ═══════════════════════════════════════════════════════════════
    // 1. 高级变换工具
    // ═══════════════════════════════════════════════════════════════
    void DrawAdvancedTransformTools(GizmoOperation& currentOp,
                                     GizmoSpace& currentSpace,
                                     PivotMode& currentPivot,
                                     bool& snapEnabled,
                                     SnapConfig& snapCfg);

    // ═══════════════════════════════════════════════════════════════
    // 2. 选择过滤面板
    // ═══════════════════════════════════════════════════════════════
    void DrawSelectionFilterPanel(SelectionMask& mask);

    // ═══════════════════════════════════════════════════════════════
    // 3. 层级可见性管理 + 孤立模式
    // ═══════════════════════════════════════════════════════════════
    void DrawLayerVisibilityPanel(uint32& visibilityMask, IsolationState& isolation);

    // ═══════════════════════════════════════════════════════════════
    // 4. 性能统计悬浮窗
    // ═══════════════════════════════════════════════════════════════
    void DrawStatsOverlay(const StatsOverlayConfig& cfg,
                           float fps, float frameTimeMs,
                           uint32 drawCalls, uint32 verts, uint32 tris,
                           uint64 vramBytes);

    // ═══════════════════════════════════════════════════════════════
    // 5. 相机书签 UI
    // ═══════════════════════════════════════════════════════════════
    void DrawCameraBookmarksUI(EditorCamera& camera);

    /** 处理书签快捷键 (Ctrl+1-9 保存, 1-9 加载)，返回 true 如果消费了按键 */
    bool HandleCameraBookmarkShortcuts(EditorCamera& camera);

    // ═══════════════════════════════════════════════════════════════
    // 6. 摄像机聚焦
    // ═══════════════════════════════════════════════════════════════
    void FocusCameraOnObject(EditorCamera& camera, const Vec3& targetPos, float radius);

    // ═══════════════════════════════════════════════════════════════
    // 7. 高级编辑器设置面板（完整组合）
    // ═══════════════════════════════════════════════════════════════
    void DrawAdvancedEditorSettings(GizmoOperation& currentOp,
                                     GizmoSpace& currentSpace,
                                     PivotMode& currentPivot,
                                     SnapConfig& snapCfg,
                                     SelectionMask& selMask,
                                     uint32& visibilityMask,
                                     IsolationState& isolation,
                                     EditorCamera& camera,
                                     bool showPanel);

} // namespace Engine