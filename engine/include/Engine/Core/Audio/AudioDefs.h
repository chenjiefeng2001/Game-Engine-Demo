#pragma once

/**
 * @file AudioDefs.h
 * @brief 音频系统纯数据结构 — 不依赖任何第三方音频库
 *
 * 设计原则（与 RHI/MathTypes.h 一致）：
 *   - 头文件只依赖 Engine/Types.h 和 RHI/MathTypes.h
 *   - 所有类型都是 POD 或轻量值类型
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"

namespace Engine {

// ============================================================
// 音频格式
// ============================================================
enum class AudioFormat : uint8 {
    Mono8,      
    Mono16,     
    Stereo8,    
    Stereo16    
};

// ============================================================
// 音频剪辑信息
// ============================================================
struct AudioClipInfo {
    int32     sampleRate    = 44100;
    AudioFormat format      = AudioFormat::Stereo16;
    float32   duration      = 0.0f;  
    int32     dataSize      = 0;    
};

// ============================================================
// 播放状态
// ============================================================
enum class PlayState : uint8 {
    Initial,
    Playing,
    Paused,
    Stopped
};

// ============================================================
// 衰减模式（3D 音频距离模型）
// ============================================================
enum class AttenuationMode : uint8 {
    None,             
    Inverse,           
    InverseClamped,     
    Linear,             
    LinearClamped       
};

} // namespace Engine
