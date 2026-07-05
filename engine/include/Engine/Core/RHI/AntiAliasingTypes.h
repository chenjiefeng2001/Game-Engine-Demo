#pragma once

#include "Engine/Types.h"

namespace Engine {

// ============================================================
// 抗锯齿模式枚举
// ============================================================
enum class AntiAliasingMode : uint8 {
    None = 0,   // 关闭抗锯齿
    MSAA,       // 多重采样抗锯齿 (Multisample Anti-Aliasing)
    SSAA,       // 超采样抗锯齿   (Supersampling Anti-Aliasing)
    CSAA,       // 覆盖采样抗锯齿 (Coverage Sampling Anti-Aliasing, NVIDIA)
    MLAA,       // 形态抗锯齿     (Morphological Anti-Aliasing)
};

// ============================================================
// 抗锯齿配置结构
// ============================================================
struct AntiAliasingConfig {
    AntiAliasingMode mode = AntiAliasingMode::None;

    // MSAA / SSAA / CSAA 共享
    int32 sampleCount = 4;          // 样本数 (2, 4, 8)

    // CSAA 专用
    int32 coverageSamples = 8;      // 覆盖样本数 (通常为 8 或 16)

    // SSAA 专用
    float superSamplingScale = 1.5f; // 超采样缩放比例 (1.5 = 1.5x)

    // MLAA 专用
    float edgeThreshold = 0.1f;     // 边缘检测阈值
    float maxSearchSteps = 16.0f;   // 最大搜索步数
};

// ============================================================
// 抗锯齿能力查询
// ============================================================
struct AntiAliasingCaps {
    bool supportsMSAA = true;       // MSAA 基本都支持
    bool supportsSSAA = true;       // SSAA 始终可用（自定义 FBO）
    bool supportsCSAA = false;      // 需要 NV 扩展
    bool supportsMLAA = true;       // MLAA 通过着色器实现

    int32 maxSamples = 0;           // 最大支持样本数（0=未知）
    bool hasCoverageExtension = false; // GL_NV_framebuffer_multisample_coverage
};

} // namespace Engine
