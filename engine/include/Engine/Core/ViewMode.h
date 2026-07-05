#pragma once

/**
 * @file ViewMode.h
 * @brief 多维度渲染视图模式 — 工业级可视化调试套件
 *
 * 基础渲染模式:
 *   Lit / Unlit / Wireframe / Shaded Wireframe
 *
 * Buffer 可视化 (G-Buffer Debug):
 *   Normal / Depth / Roughness / Metallic / Specular
 *   Albedo / Ambient Occlusion / Velocity
 *
 * 诊断视图:
 *   Overdraw 热力图 / Lightmap Density 棋盘格
 *   Collision 碰撞体 / LOD Coloration / Shader Complexity
 */

#include "Engine/Types.h"

namespace Engine {

    /// 渲染调试模式分组
    enum class ViewModeGroup : uint8 {
        Basic,          ///< 基础渲染模式
        GBuffer,        ///< G-Buffer 可视化
        Lighting,       ///< 光照分析
        Diagnostic,     ///< 诊断视图
        COUNT
    };

    inline const char* ViewModeGroupName(ViewModeGroup g) {
        switch (g) {
            case ViewModeGroup::Basic:     return "Basic";
            case ViewModeGroup::GBuffer:   return "G-Buffer";
            case ViewModeGroup::Lighting:  return "Lighting";
            case ViewModeGroup::Diagnostic:return "Diagnostic";
            default:                       return "Unknown";
        }
    }

    /// 渲染调试模式（扩展版本）
    enum class ViewMode : uint16 {
        // ── Basic ──
        Normal          = 0,    /// 正常渲染（默认，带光照）
        Unlit           = 1,    /// 无光照模式 — 仅显示 Albedo/漫反射纹理
        Wireframe       = 2,    /// 线框模式 — glPolygonMode(GL_LINE)
        ShadedWireframe = 3,    /// 着色线框：有色线框 + 半透明实体叠加
        COUNT_Basic     = 10,

        // ── G-Buffer Debug ──
        GBuffer_Normal      = 10,  /// 法线可视化 (world space → color)
        GBuffer_Depth       = 11,  /// 深度缓冲区可视化 (linear depth → grayscale)
        GBuffer_Albedo      = 12,  /// 基础颜色通道 (Base Color)
        GBuffer_Roughness   = 13,  /// 粗糙度可视化 (0=光滑黑色 → 1=粗糙白色)
        GBuffer_Metallic    = 14,  /// 金属度可视化 (0=非金属黑 → 1=金属白)
        GBuffer_Specular    = 15,  /// 镜面高光可视化
        GBuffer_AO          = 16,  /// 环境光遮蔽可视化
        GBuffer_Velocity    = 17,  /// 运动向量可视化 (运动模糊调试)
        COUNT_GBuffer       = 20,

        // ── Lighting Analysis ──
        LightingOnly        = 20,  /// 纯光照模式 — 所有物体灰色，仅显示光照
        LightmapDensity     = 21,  /// 光照贴图密度 — 棋盘格显示精度分布
        Overdraw           = 22,  /// Overdraw 热力图 — 半透明重叠可视化
        COUNT_Lighting      = 30,

        // ── Diagnostic ──
        ShaderComplexity    = 30,  /// 着色器复杂度评估
        LODColoration       = 31,  /// LOD 等级着色 — 红=高模, 绿=中模, 蓝=低模
        CollisionDebug      = 32,  /// 碰撞体可视化 — 显示物理碰撞盒
        UVOverlap           = 33,  /// UV 重叠检测 — 避免纹理接缝问题
        VertexOverdraw      = 34,  /// 顶点密度热力图
        COUNT               = 40
    };

    /// 获取 ViewMode 所属分组
    inline ViewModeGroup GetViewModeGroup(ViewMode mode) {
        if (static_cast<uint16>(mode) < static_cast<uint16>(ViewMode::COUNT_Basic))
            return ViewModeGroup::Basic;
        if (static_cast<uint16>(mode) < static_cast<uint16>(ViewMode::COUNT_GBuffer))
            return ViewModeGroup::GBuffer;
        if (static_cast<uint16>(mode) < static_cast<uint16>(ViewMode::COUNT_Lighting))
            return ViewModeGroup::Lighting;
        return ViewModeGroup::Diagnostic;
    }

    /// 将 ViewMode 转换为 UI 可读字符串
    inline const char* ViewModeToString(ViewMode mode) {
        switch (mode) {
            // Basic
            case ViewMode::Normal:           return "Lit (Normal)";
            case ViewMode::Unlit:            return "Unlit";
            case ViewMode::Wireframe:        return "Wireframe";
            case ViewMode::ShadedWireframe:  return "Shaded Wireframe";

            // G-Buffer
            case ViewMode::GBuffer_Normal:   return "G-Buffer: Normal";
            case ViewMode::GBuffer_Depth:    return "G-Buffer: Depth";
            case ViewMode::GBuffer_Albedo:   return "G-Buffer: Albedo";
            case ViewMode::GBuffer_Roughness:return "G-Buffer: Roughness";
            case ViewMode::GBuffer_Metallic: return "G-Buffer: Metallic";
            case ViewMode::GBuffer_Specular: return "G-Buffer: Specular";
            case ViewMode::GBuffer_AO:       return "G-Buffer: AO";
            case ViewMode::GBuffer_Velocity: return "G-Buffer: Velocity";

            // Lighting
            case ViewMode::LightingOnly:     return "Lighting Only";
            case ViewMode::LightmapDensity:  return "Lightmap Density";
            case ViewMode::Overdraw:         return "Overdraw Heatmap";

            // Diagnostic
            case ViewMode::ShaderComplexity: return "Shader Complexity";
            case ViewMode::LODColoration:    return "LOD Coloration";
            case ViewMode::CollisionDebug:   return "Collision Debug";
            case ViewMode::UVOverlap:        return "UV Overlap Detection";
            case ViewMode::VertexOverdraw:   return "Vertex Density";

            default:                         return "Unknown";
        }
    }

    /// 获取 ViewMode 的简短标签（用于按钮/下拉）
    inline const char* ViewModeToShortString(ViewMode mode) {
        switch (mode) {
            case ViewMode::Normal:           return "Lit";
            case ViewMode::Unlit:            return "Unlit";
            case ViewMode::Wireframe:        return "Wire";
            case ViewMode::ShadedWireframe:  return "ShadedWire";

            case ViewMode::GBuffer_Normal:   return "Normal";
            case ViewMode::GBuffer_Depth:    return "Depth";
            case ViewMode::GBuffer_Albedo:   return "Albedo";
            case ViewMode::GBuffer_Roughness:return "Rough";
            case ViewMode::GBuffer_Metallic: return "Metal";
            case ViewMode::GBuffer_Specular: return "Spec";
            case ViewMode::GBuffer_AO:       return "AO";
            case ViewMode::GBuffer_Velocity: return "Velocity";

            case ViewMode::LightingOnly:     return "Light";
            case ViewMode::LightmapDensity:  return "LM Density";
            case ViewMode::Overdraw:         return "Overdraw";

            case ViewMode::ShaderComplexity: return "Complex";
            case ViewMode::LODColoration:    return "LOD";
            case ViewMode::CollisionDebug:   return "Collision";
            case ViewMode::UVOverlap:        return "UV Check";
            case ViewMode::VertexOverdraw:   return "Vtx Density";

            default:                         return "?";
        }
    }

    /// 获取 ViewMode 的 uniform 整数值（用于 shader 内统一分派）
    inline int32 ViewModeToShaderInt(ViewMode mode) {
        return static_cast<int32>(mode);
    }

    /// 判断是否需要开启半透明叠加渲染（如 Overdraw）
    inline bool ViewModeNeedsBlend(ViewMode mode) {
        return mode == ViewMode::Overdraw ||
               mode == ViewMode::ShadedWireframe ||
               mode == ViewMode::LightmapDensity;
    }

    /// 判断是否需要深度测试（Overdraw 等不需要）
    inline bool ViewModeNeedsDepthTest(ViewMode mode) {
        return mode != ViewMode::Overdraw &&
               mode != ViewMode::Wireframe;
    }

} // namespace Engine