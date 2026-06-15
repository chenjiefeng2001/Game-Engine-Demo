#pragma once

/**
 * @file BufferVisualization.h
 * @brief 缓冲区可视化枚举与配置 — 用于图形调试菜单的 Buffer Visualization
 *
 * 支持可视化以下缓冲区作为 ImGui 图像：
 *   - Depth Buffer（深度）
 *   - G-Buffer 各层（Normal, Albedo, Roughness 等）
 *   - Stencil Buffer（模板）
 *   - Motion Vectors（运动矢量）
 */

#include "Engine/Types.h"

namespace Engine {

    /// 缓冲区可视化模式
    enum class BufferVisMode : uint8 {
        None        = 0,  /// 不显示任何缓冲区预览
        Depth       = 1,  /// 深度缓冲区 (线性化显示)
        Normal      = 2,  /// 法线缓冲区 (G-Buffer)
        Albedo      = 3,  /// 固有色缓冲区 (G-Buffer)
        Roughness   = 4,  /// 粗糙度缓冲区 (G-Buffer)
        Metallic    = 5,  /// 金属度缓冲区 (G-Buffer)
        Stencil     = 6,  /// 模板缓冲区
        MotionVec   = 7,  /// 运动矢量
        COUNT       = 8
    };

    /// 转换到 UI 可读字符串
    inline const char* BufferVisModeToString(BufferVisMode mode) {
        switch (mode) {
            case BufferVisMode::None:       return "None";
            case BufferVisMode::Depth:      return "Depth Buffer";
            case BufferVisMode::Normal:     return "G-Buffer: Normal";
            case BufferVisMode::Albedo:     return "G-Buffer: Albedo";
            case BufferVisMode::Roughness:  return "G-Buffer: Roughness";
            case BufferVisMode::Metallic:   return "G-Buffer: Metallic";
            case BufferVisMode::Stencil:    return "Stencil Buffer";
            case BufferVisMode::MotionVec:  return "Motion Vectors";
            default:                        return "Unknown";
        }
    }

    /// 缓冲区纹理句柄（跨 API 的抽象纹理 ID）
    using BufferTextureHandle = uint64;

    /// 缓冲区可视化帧数据（每帧由渲染器提供）
    struct BufferVisFrameData {
        // 深度缓冲区 (总是可用，通过 glReadPixels 或深度纹理采样)
        BufferTextureHandle depthTexture    = 0;

        // G-Buffer 各层纹理 (由 Deferred Renderer 填充)
        BufferTextureHandle gbufferNormal   = 0;
        BufferTextureHandle gbufferAlbedo   = 0;
        BufferTextureHandle gbufferRoughness = 0;
        BufferTextureHandle gbufferMetallic  = 0;

        // 模板/运动矢量
        BufferTextureHandle stencilTexture  = 0;
        BufferTextureHandle motionVecTexture = 0;

        // 尺寸
        uint32 width  = 0;
        uint32 height = 0;
    };

} // namespace Engine