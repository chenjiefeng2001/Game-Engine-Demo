#pragma once

/**
 * @file AnimationTrack.h
 * @brief 动画轨道 — 存储某一属性的关键帧序列并提供求值
 *
 * AnimationTrack 负责：
 *   - 存储某一属性类型（Float / Vec2 / Vec3 / Vec4）的关键帧
 *   - 按时间排序关键帧
 *   - 在给定时间点基于插值模式求值
 *
 * 每条轨道只有单一属性类型，由 AnimationLocalTimeline 组合多条轨道
 * 来描述完整的动画。
 */

#include "Engine/Animation/AnimationKeyFrame.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <vector>
#include <algorithm>

namespace Engine {

class AnimationTrack {
public:
    AnimationTrack() = default;

    // ── 属性类型 ──
    AnimationPropertyType GetPropertyType() const noexcept { return m_Type; }
    void SetPropertyType(AnimationPropertyType type) noexcept { m_Type = type; }

    // ── 插值模式 ──
    AnimationInterpolation GetInterpolation() const noexcept { return m_Interpolation; }
    void SetInterpolation(AnimationInterpolation mode) noexcept { m_Interpolation = mode; }

    // ── 关键帧管理 ──

    /** 添加一个浮点关键帧（仅在属性类型为 Float 时有效） */
    void AddKeyFrame(const KeyFrameFloat& kf);
    /** 添加一个 Vec2 关键帧（仅在属性类型为 Vec2 时有效） */
    void AddKeyFrame(const KeyFrameVec2& kf);
    /** 添加一个 Vec3 关键帧（仅在属性类型为 Vec3 时有效） */
    void AddKeyFrame(const KeyFrameVec3& kf);
    /** 添加一个 Vec4 关键帧（仅在属性类型为 Vec4 时有效） */
    void AddKeyFrame(const KeyFrameVec4& kf);

    /** 清除所有关键帧 */
    void ClearKeyFrames();

    /** 获取关键帧数量 */
    size_t GetKeyFrameCount() const;

    /** 获取轨道总时长（最后一个关键帧的时间点） */
    float32 GetDuration() const noexcept { return m_Duration; }

    // ── 求值 ──

    /** 在给定时间点求值（返回值类型取决于 m_Type） */
    float32 EvaluateFloat(float32 time) const;
    Vec2    EvaluateVec2(float32 time) const;
    Vec3    EvaluateVec3(float32 time) const;
    Vec4    EvaluateVec4(float32 time) const;

    // ── 序列化辅助 ──
    /** 检查轨道是否包含任何关键帧 */
    bool IsEmpty() const noexcept;

    // ── 内部求值 ──

    /** 二分查找给定时间前后的两个关键帧索引 */
    bool FindKeyFrameRange(float32 time, size_t& outPrev, size_t& outNext) const;

    /** 执行实际插值 */
    static float32 Interpolate(float32 t, float32 a, float32 b, AnimationInterpolation mode);
    static Vec2    Interpolate(float32 t, const Vec2& a, const Vec2& b, AnimationInterpolation mode);
    static Vec3    Interpolate(float32 t, const Vec3& a, const Vec3& b, AnimationInterpolation mode);
    static Vec4    Interpolate(float32 t, const Vec4& a, const Vec4& b, AnimationInterpolation mode);

private:
    // ── 数据 ──
    AnimationPropertyType    m_Type          = AnimationPropertyType::Float;
    AnimationInterpolation   m_Interpolation = AnimationInterpolation::Linear;
    float32                  m_Duration      = 0.0f;

    // 关键帧存储（只有对应类型的容器会被填充）
    std::vector<KeyFrameFloat> m_FloatKeys;
    std::vector<KeyFrameVec2>  m_Vec2Keys;
    std::vector<KeyFrameVec3>  m_Vec3Keys;
    std::vector<KeyFrameVec4>  m_Vec4Keys;
};

} // namespace Engine
