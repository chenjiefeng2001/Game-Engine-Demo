#pragma once

/**
 * @file ViewportConfig.h
 * @brief 视口配置数据结构 — 将"画什么"与"怎么画"解耦
 *
 * 所有视口相关的开关、参数打包到纯数据结构中，
 * ViewportPanel 引用此配置运行，而非硬编码散落的 bool 变量。
 */

#include "Engine/Types.h"
#include "Engine/Core/ViewMode.h"
#include <string>
#include <cstdint>

namespace Engine {

    // ──────────────────────────────────────────────
    // 1. 投影类型枚举
    // ──────────────────────────────────────────────
    enum class ProjectionType : uint8 {
        Perspective,   ///< 透视投影（默认）
        Orthographic   ///< 正交投影（三视图）
    };

    // ──────────────────────────────────────────────
    // 2. 相机配置
    // ──────────────────────────────────────────────
    struct CameraConfig {
        float FOV            = 60.0f;   ///< 视野角度（度）
        float NearClip       = 0.1f;    ///< 近裁剪面
        float FarClip        = 1000.0f; ///< 远裁剪面
        ProjectionType Type  = ProjectionType::Perspective;

        // 初始视口相机姿态（编辑器默认俯瞰原点）
        float PositionX = 0.0f;
        float PositionY = 5.0f;
        float PositionZ = 10.0f;
        float Pitch     = -30.0f;   ///< 俯仰角（度）
        float Yaw       = -45.0f;   ///< 偏航角（度）
        float Distance  = 10.0f;    ///< 到焦点的距离（轨道模式）
    };

    // ──────────────────────────────────────────────
    // 3. 视口显示配置
    // ──────────────────────────────────────────────
    struct ViewportConfig {
        std::string Name;

        // ── 相机 ──
        CameraConfig Camera;

        // ── 显示开关 ──
        bool ShowGrid           = true;    ///< 显示参考网格
        bool ShowGizmos         = true;    ///< 显示 ImGuizmo 操作手柄
        bool ShowPostProcessing = true;    ///< 启用后期处理
        bool ShowGridAxis       = true;    ///< 显示坐标轴指示
        bool ShowSelectionOutline = true;  ///< 选中物体高亮边框

        // ── 渲染模式 ──
        ViewMode CurrentMode = ViewMode::Normal;

        // ── 可见性遮罩 (Visibility Mask) ──
        // 每一位代表一个渲染层级：
        //   bit 0: 静态几何体 (Static Geometry)
        //   bit 1: 动态物体    (Dynamic Objects)
        //   bit 2: 灯光图标    (Light Icons)
        //   bit 3: 粒子系统    (Particles)
        //   bit 4: 触发器/区域 (Trigger Volumes)
        //   bit 5: 碰撞体可视化 (Collision Visualization)
        //   bit 6: 骨骼/蒙皮  (Skeleton / Skinning Debug)
        uint32 VisibilityMask = 0xFFFFFFFF;

        // ── 辅助方法 ──
        bool IsLayerVisible(uint32 layerIndex) const {
            return (VisibilityMask & (1u << layerIndex)) != 0;
        }

        void SetLayerVisible(uint32 layerIndex, bool visible) {
            if (visible)
                VisibilityMask |= (1u << layerIndex);
            else
                VisibilityMask &= ~(1u << layerIndex);
        }

        // ── Gizmo 空间控制 ──
        bool GizmoLocal = false;     ///< true=Local, false=World
        int  GizmoMode  = 0;         ///< 0=Translate, 1=Rotate, 2=Scale

        // ── 吸附 ──
        bool  SnapEnabled = false;
        float SnapValue   = 0.5f;

        // ── 相机飞行速度 ──
        float CameraFlySpeed = 5.0f;

        // ── 网格配置（用于 EditorOverlay 的无限网格着色器） ──
        float GridSize        = 20.0f;    ///< 网格总尺寸
        float GridCellSize    = 1.0f;     ///< 单元格大小
        int   GridSubdivision = 1;        ///< 子网格细分（用于精确对齐）
    };

    // ──────────────────────────────────────────────
    // 4. 视口可见性层级名称（用于 UI 显示）
    // ──────────────────────────────────────────────
    namespace ViewportLayer {
        enum : uint32 {
            StaticGeometry  = 0,
            DynamicObjects  = 1,
            LightIcons      = 2,
            Particles       = 3,
            TriggerVolumes  = 4,
            CollisionDebug  = 5,
            SkeletonDebug   = 6,
            COUNT           = 7
        };

        inline const char* ToString(uint32 layer) {
            switch (layer) {
                case StaticGeometry:  return "Static Geometry";
                case DynamicObjects:  return "Dynamic Objects";
                case LightIcons:      return "Light Icons";
                case Particles:       return "Particles";
                case TriggerVolumes:  return "Trigger Volumes";
                case CollisionDebug:  return "Collision Debug";
                case SkeletonDebug:   return "Skeleton Debug";
                default:              return "Unknown";
            }
        }
    }

} // namespace Engine