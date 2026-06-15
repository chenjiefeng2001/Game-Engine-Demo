#pragma once

/**
 * @file HelperToggles.h
 * @brief 常用调试开关数据 — 用于图形调试菜单的 Helper Toggles 面板
 *
 * 引擎渲染器每帧读取此结构中的开关状态来控制辅助可视化。
 */

#include "Engine/Types.h"

namespace Engine {

    /// 常用调试开关帧数据
    struct HelperTogglesFrameData {
        // ── 辅助可视化开关 ──
        bool showGrid           = false;  // 显示世界网格线
        bool showBones          = false;  // 显示骨骼线框
        bool showColliders      = false;  // 显示碰撞体
        bool pauseRendering     = false;  // 暂停渲染
        bool stepOneFrame       = false;  // 单帧步进
        bool showOriginAxis     = true;   // 显示世界坐标轴

        // ── 网格参数 ──
        float gridSize  = 10.0f;
        int   gridSteps = 10;

        // ── 物理碰撞体颜色 ──
        float colliderColor[4] = { 0.0f, 1.0f, 0.0f, 0.5f };

        void Reset() {
            showGrid = false;
            showBones = false;
            showColliders = false;
            pauseRendering = false;
            stepOneFrame = false;
            showOriginAxis = true;
            gridSize = 10.0f;
            gridSteps = 10;
            colliderColor[0] = 0.0f; colliderColor[1] = 1.0f;
            colliderColor[2] = 0.0f; colliderColor[3] = 0.5f;
        }
    };

} // namespace Engine