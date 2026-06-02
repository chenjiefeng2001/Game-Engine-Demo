/**
 * @file AnimationTrack.cpp
 * @brief 动画轨道 — 关键帧序列存储与求值
 */

#include "Engine/Animation/AnimationTrack.h"
#include <algorithm>
#include <cmath>

namespace Engine {

// ============================================================
// 关键帧管理
// ============================================================

void AnimationTrack::AddKeyFrame(const KeyFrameFloat& kf) {
    m_FloatKeys.push_back(kf);
    std::sort(m_FloatKeys.begin(), m_FloatKeys.end(),
              [](const KeyFrameFloat& a, const KeyFrameFloat& b) { return a.time < b.time; });
    m_Duration = m_FloatKeys.empty() ? 0.0f : m_FloatKeys.back().time;
    m_Type = AnimationPropertyType::Float;
}

void AnimationTrack::AddKeyFrame(const KeyFrameVec2& kf) {
    m_Vec2Keys.push_back(kf);
    std::sort(m_Vec2Keys.begin(), m_Vec2Keys.end(),
              [](const KeyFrameVec2& a, const KeyFrameVec2& b) { return a.time < b.time; });
    m_Duration = m_Vec2Keys.empty() ? 0.0f : m_Vec2Keys.back().time;
    m_Type = AnimationPropertyType::Vec2;
}

void AnimationTrack::AddKeyFrame(const KeyFrameVec3& kf) {
    m_Vec3Keys.push_back(kf);
    std::sort(m_Vec3Keys.begin(), m_Vec3Keys.end(),
              [](const KeyFrameVec3& a, const KeyFrameVec3& b) { return a.time < b.time; });
    m_Duration = m_Vec3Keys.empty() ? 0.0f : m_Vec3Keys.back().time;
    m_Type = AnimationPropertyType::Vec3;
}

void AnimationTrack::AddKeyFrame(const KeyFrameVec4& kf) {
    m_Vec4Keys.push_back(kf);
    std::sort(m_Vec4Keys.begin(), m_Vec4Keys.end(),
              [](const KeyFrameVec4& a, const KeyFrameVec4& b) { return a.time < b.time; });
    m_Duration = m_Vec4Keys.empty() ? 0.0f : m_Vec4Keys.back().time;
    m_Type = AnimationPropertyType::Vec4;
}

void AnimationTrack::ClearKeyFrames() {
    m_FloatKeys.clear();
    m_Vec2Keys.clear();
    m_Vec3Keys.clear();
    m_Vec4Keys.clear();
    m_Duration = 0.0f;
}

size_t AnimationTrack::GetKeyFrameCount() const {
    switch (m_Type) {
        case AnimationPropertyType::Float: return m_FloatKeys.size();
        case AnimationPropertyType::Vec2:  return m_Vec2Keys.size();
        case AnimationPropertyType::Vec3:  return m_Vec3Keys.size();
        case AnimationPropertyType::Vec4:  return m_Vec4Keys.size();
    }
    return 0;
}

bool AnimationTrack::IsEmpty() const noexcept {
    return GetKeyFrameCount() == 0;
}

// ============================================================
// 内部：二分查找关键帧范围
// ============================================================

template<typename T>
static bool FindRangeImpl(const std::vector<T>& keys, float32 time,
                          size_t& outPrev, size_t& outNext) {
    if (keys.empty()) {
        outPrev = outNext = 0;
        return false;
    }
    if (time <= keys.front().time) {
        outPrev = outNext = 0;
        return true;
    }
    if (time >= keys.back().time) {
        outPrev = outNext = keys.size() - 1;
        return true;
    }

    // 二分查找
    size_t lo = 0, hi = keys.size() - 1;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (keys[mid].time < time)
            lo = mid + 1;
        else
            hi = mid;
    }

    outNext = lo;
    outPrev = (lo > 0) ? lo - 1 : 0;

    // 如果正好落在关键帧上，prev == next
    if (keys[outPrev].time >= time && outPrev > 0)
        --outPrev;

    return true;
}

bool AnimationTrack::FindKeyFrameRange(float32 time, size_t& outPrev, size_t& outNext) const {
    switch (m_Type) {
        case AnimationPropertyType::Float: return FindRangeImpl(m_FloatKeys, time, outPrev, outNext);
        case AnimationPropertyType::Vec2:  return FindRangeImpl(m_Vec2Keys, time, outPrev, outNext);
        case AnimationPropertyType::Vec3:  return FindRangeImpl(m_Vec3Keys, time, outPrev, outNext);
        case AnimationPropertyType::Vec4:  return FindRangeImpl(m_Vec4Keys, time, outPrev, outNext);
    }
    return false;
}

// ============================================================
// 插值辅助
// ============================================================

static float32 SmoothStep(float32 t) {
    // 三次 Hermite 曲线: t^2 * (3 - 2t)
    return t * t * (3.0f - 2.0f * t);
}

float32 AnimationTrack::Interpolate(float32 t, float32 a, float32 b, AnimationInterpolation mode) {
    switch (mode) {
        case AnimationInterpolation::Step:
            return a;
        case AnimationInterpolation::Smooth:
            return a + (b - a) * SmoothStep(t);
        case AnimationInterpolation::Linear:
        default:
            return a + (b - a) * t;
    }
}

Vec2 AnimationTrack::Interpolate(float32 t, const Vec2& a, const Vec2& b, AnimationInterpolation mode) {
    float32 s = (mode == AnimationInterpolation::Smooth) ? SmoothStep(t) : t;
    if (mode == AnimationInterpolation::Step) s = 0.0f;
    return Vec2(a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s);
}

Vec3 AnimationTrack::Interpolate(float32 t, const Vec3& a, const Vec3& b, AnimationInterpolation mode) {
    float32 s = (mode == AnimationInterpolation::Smooth) ? SmoothStep(t) : t;
    if (mode == AnimationInterpolation::Step) s = 0.0f;
    return Vec3(a.x + (b.x - a.x) * s,
                a.y + (b.y - a.y) * s,
                a.z + (b.z - a.z) * s);
}

Vec4 AnimationTrack::Interpolate(float32 t, const Vec4& a, const Vec4& b, AnimationInterpolation mode) {
    float32 s = (mode == AnimationInterpolation::Smooth) ? SmoothStep(t) : t;
    if (mode == AnimationInterpolation::Step) s = 0.0f;
    return Vec4(a.x + (b.x - a.x) * s,
                a.y + (b.y - a.y) * s,
                a.z + (b.z - a.z) * s,
                a.w + (b.w - a.w) * s);
}

// ============================================================
// 求值
// ============================================================

template<typename T, typename KF>
static T EvaluateImpl(const std::vector<KF>& keys, float32 time,
                      AnimationInterpolation mode, const T& defaultVal) {
    if (keys.empty())
        return defaultVal;
    if (keys.size() == 1 || time <= keys.front().time)
        return keys.front().value;
    if (time >= keys.back().time)
        return keys.back().value;

    size_t prev = 0, next = 0;
    // 线性搜索（小数据集时比二分更快）
    for (size_t i = 1; i < keys.size(); ++i) {
        if (keys[i].time >= time) {
            prev = i - 1;
            next = i;
            break;
        }
    }

    float32 t = 0.0f;
    float32 range = keys[next].time - keys[prev].time;
    if (range > 0.0f)
        t = (time - keys[prev].time) / range;

    return AnimationTrack::Interpolate(t, keys[prev].value, keys[next].value, mode);
}

float32 AnimationTrack::EvaluateFloat(float32 time) const {
    return EvaluateImpl<float32, KeyFrameFloat>(m_FloatKeys, time, m_Interpolation, 0.0f);
}

Vec2 AnimationTrack::EvaluateVec2(float32 time) const {
    return EvaluateImpl<Vec2, KeyFrameVec2>(m_Vec2Keys, time, m_Interpolation, Vec2(0.0f, 0.0f));
}

Vec3 AnimationTrack::EvaluateVec3(float32 time) const {
    return EvaluateImpl<Vec3, KeyFrameVec3>(m_Vec3Keys, time, m_Interpolation, Vec3(0.0f, 0.0f, 0.0f));
}

Vec4 AnimationTrack::EvaluateVec4(float32 time) const {
    return EvaluateImpl<Vec4, KeyFrameVec4>(m_Vec4Keys, time, m_Interpolation, Vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

} // namespace Engine
