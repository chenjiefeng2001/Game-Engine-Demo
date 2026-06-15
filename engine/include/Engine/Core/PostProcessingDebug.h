#pragma once

/**
 * @file PostProcessingDebug.h
 * @brief 后期处理调试数据 — 用于图形调试菜单的 Post-Processing 面板
 *
 * 引擎渲染器每帧读取此结构中的开关状态来控制后期效果。
 * UI 中的开关直接修改标志位，渲染器消费。
 */

#include "Engine/Types.h"

namespace Engine {

    /// 抗锯齿类型
    enum class AAType : uint8 {
        None    = 0,
        FXAA    = 1,
        TAA     = 2,
        SMAA    = 3,
        MSAA    = 4,
    };

    inline const char* AATypeToString(AAType type) {
        switch (type) {
            case AAType::None:  return "None";
            case AAType::FXAA:  return "FXAA";
            case AAType::TAA:   return "TAA";
            case AAType::SMAA:  return "SMAA";
            case AAType::MSAA:  return "MSAA";
            default:            return "Unknown";
        }
    }

    /// 色调映射模式
    enum class ToneMappingMode : uint8 {
        None        = 0,
        Reinhard    = 1,
        ACES        = 2,
        Unreal      = 3,
        Filmic      = 4,
    };

    inline const char* ToneMappingModeToString(ToneMappingMode mode) {
        switch (mode) {
            case ToneMappingMode::None:     return "None";
            case ToneMappingMode::Reinhard: return "Reinhard";
            case ToneMappingMode::ACES:     return "ACES (Filmic)";
            case ToneMappingMode::Unreal:   return "Unreal";
            case ToneMappingMode::Filmic:   return "Filmic";
            default:                        return "Unknown";
        }
    }

    /// 后期处理调试帧数据
    struct PostProcessingDebugFrameData {
        // ── 总开关 ──
        bool postProcessingEnabled = true;

        // ── 分项开关 ──
        bool bloomEnabled         = true;
        bool depthOfFieldEnabled  = true;
        bool colorGradingEnabled  = true;
        bool antiAliasingEnabled  = true;
        bool toneMappingEnabled   = true;

        // ── Bloom 参数 ──
        float bloomIntensity  = 1.0f;
        float bloomThreshold  = 1.0f;
        float bloomRadius     = 0.05f;

        // ── Depth of Field 参数 ──
        float dofFocusDistance = 10.0f;
        float dofFocusWidth   = 2.0f;
        float dofBlurAmount   = 1.0f;

        // ── Color Grading / LUT ──
        float colorGradingStrength = 1.0f;
        float brightness = 1.0f;
        float contrast   = 1.0f;
        float saturation = 1.0f;

        // ── Anti-Aliasing ──
        AAType aaType = AAType::FXAA;
        bool   aaEnable = true;

        // ── Tone Mapping ──
        ToneMappingMode toneMapMode = ToneMappingMode::ACES;
        float exposure   = 1.0f;
        float gamma      = 2.2f;

        /// 重置为默认
        void Reset() {
            postProcessingEnabled = true;
            bloomEnabled = true;
            depthOfFieldEnabled = true;
            colorGradingEnabled = true;
            antiAliasingEnabled = true;
            toneMappingEnabled = true;
            bloomIntensity = 1.0f; bloomThreshold = 1.0f; bloomRadius = 0.05f;
            dofFocusDistance = 10.0f; dofFocusWidth = 2.0f; dofBlurAmount = 1.0f;
            colorGradingStrength = 1.0f; brightness = 1.0f; contrast = 1.0f; saturation = 1.0f;
            aaType = AAType::FXAA; aaEnable = true;
            toneMapMode = ToneMappingMode::ACES; exposure = 1.0f; gamma = 2.2f;
        }
    };

} // namespace Engine