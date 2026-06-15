#pragma once

/**
 * @file TextureDebug.h
 * @brief 纹理与资源调试数据 — 用于图形调试菜单的 Textures & Assets 面板
 */

#include "Engine/Types.h"
#include <string>
#include <cstdint>

namespace Engine {

    /// 单个纹理调试信息
    struct TextureDebugInfo {
        std::string name;
        uint32      width      = 0;
        uint32      height     = 0;
        uint32      mipCount   = 0;
        uint64      vramBytes  = 0;
        bool        isStreaming = false;
        bool        isLoading   = false;
        bool        isResident  = true;
        float       streamProgress = 1.0f;  // 0~1
    };

    /// 纹理与资源调试帧数据
    struct TextureDebugFrameData {
        static constexpr uint32 kMaxTextures = 64;
        TextureDebugInfo textures[kMaxTextures];
        uint32 textureCount = 0;
        uint64 totalVRAM    = 0;

        // ── 调试开关 ──
        bool showMipmapColors    = false;  // 用颜色显示 mip 级别
        bool replaceWithChecker  = false;  // 将所有纹理替换为棋盘格
        bool showStreamingStatus = false;  // 显示纹理加载流状态

        void Reset() {
            textureCount = 0;
            totalVRAM = 0;
            showMipmapColors = false;
            replaceWithChecker = false;
            showStreamingStatus = false;
        }
    };

} // namespace Engine