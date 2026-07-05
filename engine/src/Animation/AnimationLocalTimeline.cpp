/**
 * @file AnimationLocalTimeline.cpp
 * @brief 局部时间线 — 挂载到 GameObject 上的动画实例实现
 */

#include "Engine/Animation/AnimationLocalTimeline.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/SpriteComponent.h"
#include <algorithm>
#include <cmath>

namespace Engine {

// ============================================================
// 构造
// ============================================================

AnimationLocalTimeline::AnimationLocalTimeline(std::string name)
    : m_Name(std::move(name)) {
    // 预设轨道的属性类型
    m_PositionTrack.SetPropertyType(AnimationPropertyType::Vec3);
    m_RotationTrack.SetPropertyType(AnimationPropertyType::Vec3);
    m_ScaleTrack.SetPropertyType(AnimationPropertyType::Vec3);
    m_ColorTrack.SetPropertyType(AnimationPropertyType::Vec4);
}

// ============================================================
// 播放控制
// ============================================================

void AnimationLocalTimeline::Play() {
    if (m_State == TimelineState::Stopped) {
        m_LocalTime = 0.0f;
        m_LastEventCheckTime = 0.0f;
    }
    m_State = TimelineState::Playing;
}

void AnimationLocalTimeline::Pause() {
    if (m_State == TimelineState::Playing) {
        m_State = TimelineState::Paused;
    }
}

void AnimationLocalTimeline::Stop() {
    m_State = TimelineState::Stopped;
    m_LocalTime = 0.0f;
    m_LastEventCheckTime = 0.0f;
}

void AnimationLocalTimeline::Seek(float32 time) {
    m_LocalTime = (time < 0.0f) ? 0.0f : time;
    m_LastEventCheckTime = m_LocalTime;
    // Seek 后刷新目标状态
    ApplyToTarget();
}

void AnimationLocalTimeline::Restart() {
    Stop();
    Play();
}

float32 AnimationLocalTimeline::GetProgress() const {
    float32 duration = m_Duration;
    if (duration <= 0.0f) {
        // 以轨道最长时长为准
        duration = std::max({
            m_PositionTrack.GetDuration(),
            m_RotationTrack.GetDuration(),
            m_ScaleTrack.GetDuration(),
            m_ColorTrack.GetDuration()
        });
        for (const auto& ft : m_FloatTracks) {
            duration = std::max(duration, ft.track.GetDuration());
        }
    }
    if (duration <= 0.0f) return 0.0f;
    return std::min(m_LocalTime / duration, 1.0f);
}

// ============================================================
// 轨道管理
// ============================================================

AnimationTrack& AnimationLocalTimeline::AddFloatTrack(const std::string& name) {
    // 检查是否已存在同名的
    for (auto& ft : m_FloatTracks) {
        if (ft.name == name) {
            return ft.track;
        }
    }
    FloatTrackEntry entry;
    entry.name = name;
    entry.track.SetPropertyType(AnimationPropertyType::Float);
    m_FloatTracks.push_back(std::move(entry));
    return m_FloatTracks.back().track;
}

AnimationTrack* AnimationLocalTimeline::GetFloatTrack(const std::string& name) {
    for (auto& ft : m_FloatTracks) {
        if (ft.name == name) return &ft.track;
    }
    return nullptr;
}

const AnimationTrack* AnimationLocalTimeline::GetFloatTrack(const std::string& name) const {
    for (const auto& ft : m_FloatTracks) {
        if (ft.name == name) return &ft.track;
    }
    return nullptr;
}

// ============================================================
// 事件管理
// ============================================================

void AnimationLocalTimeline::AddEvent(const AnimationEvent& evt) {
    m_Events.push_back(evt);
    // 按时间排序
    std::sort(m_Events.begin(), m_Events.end(),
              [](const AnimationEvent& a, const AnimationEvent& b) { return a.time < b.time; });
}

void AnimationLocalTimeline::ClearEvents() {
    m_Events.clear();
}

// ============================================================
// 更新
// ============================================================

void AnimationLocalTimeline::Update(float32 dt) {
    if (m_State != TimelineState::Playing)
        return;

    float32 prevTime = m_LocalTime;
    AdvanceTime(dt);

    // 触发事件
    FireEvents(prevTime, m_LocalTime);

    // 应用属性
    ApplyToTarget();
}

void AnimationLocalTimeline::AdvanceTime(float32 dt) {
    if (m_State != TimelineState::Playing)
        return;

    // 应用时间缩放
    float32 scaledDt = dt * m_TimeScale;
    m_LocalTime += scaledDt;

    // 获取实际时长
    float32 duration = m_Duration;
    if (duration <= 0.0f) {
        // 以所有轨道中最长的为准
        duration = std::max({
            m_PositionTrack.GetDuration(),
            m_RotationTrack.GetDuration(),
            m_ScaleTrack.GetDuration(),
            m_ColorTrack.GetDuration()
        });
        for (const auto& ft : m_FloatTracks) {
            duration = std::max(duration, ft.track.GetDuration());
        }
    }

    if (duration <= 0.0f)
        return;

    // 循环处理
    switch (m_LoopMode) {
        case AnimationLoopMode::Once:
            if (m_LocalTime >= duration) {
                m_LocalTime = duration;
                m_State = TimelineState::Stopped;
                if (m_OnComplete)
                    m_OnComplete();
            }
            break;

        case AnimationLoopMode::Loop:
            if (m_LocalTime >= duration) {
                m_LocalTime = std::fmod(m_LocalTime, duration);
                m_LastEventCheckTime = 0.0f;  // 重置事件检测，避免环绕时重复触发
            }
            break;

        case AnimationLoopMode::PingPong: {
            float32 cycleTime = std::fmod(m_LocalTime, duration * 2.0f);
            if (cycleTime > duration) {
                // 反向阶段
                m_LocalTime = duration * 2.0f - cycleTime;
            } else {
                m_LocalTime = cycleTime;
            }
            break;
        }
    }
}

void AnimationLocalTimeline::ApplyToTarget() {
    auto target = m_Target.lock();
    if (!target)
        return;

    float32 t = m_LocalTime;

    // ── 位置轨道 ──
    if (!m_PositionTrack.IsEmpty()) {
        Vec3 pos = m_PositionTrack.EvaluateVec3(t);
        target->GetTransform().SetPosition(pos);
    }

    // ── 旋转轨道 ──
    if (!m_RotationTrack.IsEmpty()) {
        Vec3 rot = m_RotationTrack.EvaluateVec3(t);
        target->GetTransform().SetRotation(rot);
    }

    // ── 缩放轨道 ──
    if (!m_ScaleTrack.IsEmpty()) {
        Vec3 scale = m_ScaleTrack.EvaluateVec3(t);
        target->GetTransform().SetScale(scale);
    }

    // ── 颜色轨道（应用到 SpriteComponent） ──
    if (!m_ColorTrack.IsEmpty()) {
        if (auto* sprite = target->GetComponent<SpriteComponent>()) {
            Vec4 color = m_ColorTrack.EvaluateVec4(t);
            sprite->SetColor(color);
        }
    }
}

// ============================================================
// 内部：事件触发
// ============================================================

void AnimationLocalTimeline::FireEvents(float32 prevTime, float32 currentTime) {
    if (m_Events.empty()) return;

    // 处理时间回绕（循环模式）
    float32 duration = m_Duration;
    if (duration <= 0.0f) {
        duration = std::max({
            m_PositionTrack.GetDuration(),
            m_RotationTrack.GetDuration(),
            m_ScaleTrack.GetDuration(),
            m_ColorTrack.GetDuration()
        });
    }

    bool wrapped = false;
    if (duration > 0.0f && currentTime < prevTime) {
        // 发生了时间回绕（循环/乒乓从头开始）
        wrapped = true;
        // 触发 prevTime → duration 区间的事件
        for (const auto& evt : m_Events) {
            if (evt.time > prevTime && evt.time <= duration) {
                if (evt.callback) evt.callback();
            }
        }
        // 触发 0 → currentTime 区间的事件
        for (const auto& evt : m_Events) {
            if (evt.time >= 0.0f && evt.time <= currentTime) {
                if (evt.callback) evt.callback();
            }
        }
    }

    if (!wrapped) {
        // 正常推进：扫描 prevTime → currentTime 区间的事件
        for (const auto& evt : m_Events) {
            if (evt.time > prevTime && evt.time <= currentTime) {
                if (evt.callback) evt.callback();
            }
        }
    }
}

} // namespace Engine
