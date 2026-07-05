#pragma once

/**
 * @file AnimationKeyFrame.h
 * @brief 动画关键帧数据类型定义
 *
 * 提供关键帧（KeyFrame）、插值模式、动画事件等基础类型。
 * 所有类型均为 POD / 轻量结构，可被 AnimationTrack 和 AnimationLocalTimeline 使用。
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <functional>
#include <string>
#include <vector>

namespace Engine {

// ============================================================
// 插值模式
// ============================================================
enum class AnimationInterpolation : uint8 {
    Step,       ///< 无插值，直接跳转到目标值
    Linear,     ///< 线性插值
    Smooth,     ///< 平滑插值（三次 Hermite 曲线）
};

// ============================================================
// 动画属性类型
// ============================================================
enum class AnimationPropertyType : uint8 {
    Float,      ///< 单浮点值
    Vec2,       ///< 二维向量
    Vec3,       ///< 三维向量
    Vec4,       ///< 四维向量 / 颜色
};

// ============================================================
// 关键帧（KeyFrame）
// ============================================================

/// 浮点关键帧
struct KeyFrameFloat {
    float32 time  = 0.0f;  ///< 时间位置（秒）
    float32 value = 0.0f;  ///< 值
};

/// Vec2 关键帧
struct KeyFrameVec2 {
    float32 time  = 0.0f;
    Vec2    value = Vec2(0.0f, 0.0f);
};

/// Vec3 关键帧
struct KeyFrameVec3 {
    float32 time  = 0.0f;
    Vec3    value = Vec3(0.0f, 0.0f, 0.0f);
};

/// Vec4 关键帧
struct KeyFrameVec4 {
    float32 time  = 0.0f;
    Vec4    value = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
};

// ============================================================
// 动画事件
// ============================================================
struct AnimationEvent {
    float32             time;       ///< 触发时间（秒，在时间线本地时间中）
    std::string         name;       ///< 事件名称 / 标识符
    std::function<void()> callback; ///< 事件回调（可选）

    AnimationEvent() : time(0.0f) {}
    AnimationEvent(float32 t, std::string n, std::function<void()> cb = nullptr)
        : time(t), name(std::move(n)), callback(std::move(cb)) {}
};

// ============================================================
// 循环模式
// ============================================================
enum class AnimationLoopMode : uint8 {
    Once,       ///< 单次播放，结束后停在最后一帧
    Loop,       ///< 循环播放
    PingPong,   ///< 来回往复播放
};

// ============================================================
// 时间线状态
// ============================================================
enum class TimelineState : uint8 {
    Stopped,    ///< 停止（未开始或已结束）
    Playing,    ///< 播放中
    Paused,     ///< 暂停
};

} // namespace Engine
