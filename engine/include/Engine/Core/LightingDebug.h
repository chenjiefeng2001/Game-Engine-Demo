#pragma once

/**
 * @file LightingDebug.h
 * @brief 光照与阴影调试数据结构 — 用于图形调试菜单的 Lighting & Shadows 面板
 *
 * 引擎渲染器每帧填充此结构，PerformanceWindow 读取并显示 UI。
 * UI 中的开关直接修改此结构中的标志位，渲染器帧循环中消费。
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <string>
#include <cstring>

namespace Engine {

    /// 单个光源调试信息
    struct LightDebugInfo {
        std::string name;          // 光源标识名
        Vec3        position;      // 世界坐标
        Vec3        color;         // 颜色
        float       intensity;     // 强度
        bool        enabled   = true;  // 是否启用
        bool        isShadowCaster = true;
    };

    /// 阴影级联调试数据
    struct ShadowCascadeDebugInfo {
        static constexpr uint32 kMaxCascades = 4;
        float cascadeSplits[kMaxCascades] = {};   // 分割深度值
        bool  visualizeCascades = false;           // 用颜色显示级联
        uint32 cascadeCount = 0;
    };

    /// 光照与阴影调试帧数据
    struct LightingDebugFrameData {
        // ── 光源列表 ──
        static constexpr uint32 kMaxLights = 16;
        LightDebugInfo lights[kMaxLights];
        uint32         lightCount = 0;

        // ── 全局开关 ──
        bool globalLightsEnabled    = true;  // 一键开关所有光源
        bool ambientLightEnabled    = true;  // 环境光
        bool shadowsEnabled         = true;  // 阴影
        bool ssaoEnabled            = true;  // SSAO
        bool giEnabled              = false; // 全局光照（预留）
        bool reflectionsEnabled     = false; // 反射（预留）

        // ── 环境光参数 ──
        Vec3  ambientColor   = {0.15f, 0.15f, 0.20f};
        float ambientIntensity = 1.0f;

        // ── 阴影参数 ──
        ShadowCascadeDebugInfo cascades;
        uint32 shadowMapSize = 1024;
        bool   showShadowMap = false;       // 预览 Shadow Map 纹理
        float  shadowBias    = 0.005f;

        // ── 反射调试 ──
        bool   showReflectionProbes = false;
        bool   showSSR              = false;

        // ── Shadow Map 纹理句柄（用于 ImGui::Image 预览） ──
        uint64 shadowTextureHandle = 0;
        int32  shadowTexWidth = 0;
        int32  shadowTexHeight = 0;

        /// 重置为默认值
        void Reset() {
            globalLightsEnabled = true;
            ambientLightEnabled = true;
            shadowsEnabled = true;
            ssaoEnabled = true;
            giEnabled = false;
            reflectionsEnabled = false;
            ambientColor = {0.15f, 0.15f, 0.20f};
            ambientIntensity = 1.0f;
            shadowMapSize = 1024;
            showShadowMap = false;
            shadowBias = 0.005f;
            showReflectionProbes = false;
            showSSR = false;
            shadowTextureHandle = 0;
            shadowTexWidth = 0;
            shadowTexHeight = 0;
            cascades.visualizeCascades = false;
            cascades.cascadeCount = 0;
            std::memset(cascades.cascadeSplits, 0, sizeof(cascades.cascadeSplits));
        }
    };

} // namespace Engine