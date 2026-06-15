#pragma once

/**
 * @file ViewMode.h
 * @brief 渲染模式枚举 — 用于图形调试菜单的 View Modes
 *
 * 通过 IRenderContext::SetViewMode() 切换当前渲染模式，
 * 所有渲染路径通过 shader uniform 或管线状态响应模式变化。
 */

#include "Engine/Types.h"

namespace Engine {

    /// 渲染调试模式
    enum class ViewMode : uint8 {
        Normal          = 0,  /// 正常渲染（默认）
        Wireframe       = 1,  /// 线框模式 — glPolygonMode(GL_LINE)
        Unlit           = 2,  /// 无光照模式 — 仅显示 Albedo 纹理
        LightingOnly    = 3,  /// 纯光照模式 — 所有物体灰色，仅显示光照
        NormalMap       = 4,  /// 法线贴图可视化 — 检查法线是否正确
        PBRAsset        = 5,  /// PBR 参数可视化基础枚举
        PBR_BaseColor   = 5,  /// 显示 Base Color
        PBR_Roughness   = 6,  /// 显示 Roughness 通道
        PBR_Metallic    = 7,  /// 显示 Metallic 通道
        PBR_Specular    = 8,  /// 显示 Specular 高光
        ShaderComplexity = 9, /// 着色器复杂度评估（通过叠加透明 Pass）
        COUNT           = 10
    };

    /// 将 ViewMode 转换为 UI 可读字符串
    inline const char* ViewModeToString(ViewMode mode) {
        switch (mode) {
            case ViewMode::Normal:          return "Normal";
            case ViewMode::Wireframe:       return "Wireframe";
            case ViewMode::Unlit:           return "Unlit";
            case ViewMode::LightingOnly:    return "Lighting Only";
            case ViewMode::NormalMap:       return "Normal Map";
            case ViewMode::PBR_BaseColor:   return "PBR: Base Color";
            case ViewMode::PBR_Roughness:   return "PBR: Roughness";
            case ViewMode::PBR_Metallic:    return "PBR: Metallic";
            case ViewMode::PBR_Specular:    return "PBR: Specular";
            case ViewMode::ShaderComplexity: return "Shader Complexity";
            default:                        return "Unknown";
        }
    }

    /// 获取 ViewMode 的 uniform 整数值（用于 shader 内统一分派）
    inline int32 ViewModeToShaderInt(ViewMode mode) {
        return static_cast<int32>(mode);
    }

} // namespace Engine