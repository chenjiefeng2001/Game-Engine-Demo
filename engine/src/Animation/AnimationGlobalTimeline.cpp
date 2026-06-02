/**
 * @file AnimationGlobalTimeline.cpp
 * @brief 全局时间线 — 引擎级时间同步与事件调度实现
 */

#include "Engine/Animation/AnimationGlobalTimeline.h"
#include <algorithm>
#include <cmath>

namespace Engine {

// ============================================================
// 单例
// ============================================================

AnimationGlobalTimeline& AnimationGlobalTimeline::Get() {
    static AnimationGlobalTimeline instance;
    return instance;
}

// ============================================================
// 生命周期
// ============================================================

void AnimationGlobalTimeline::Init() {
    m_GlobalTime          = 0.0f;
    m_LastEventCheckTime  = 0.0f;
    m_State               = TimelineState::Playing;  // 默认开始播放
    m_TimeScale           = 1.0f;
    m_Timelines.clear();
    m_Events.clear();
    m_Initialized = true;
}

void AnimationGlobalTimeline::Shutdown() {
    m_Timelines.clear();
    m_Events.clear();
    m_Initialized = false;
}

// ============================================================
// 播放控制
// ============================================================

void AnimationGlobalTimeline::Play() {
    if (m_State == TimelineState::Stopped) {
        m_GlobalTime = 0.0f;
        m_LastEventCheckTime = 0.0f;
        // 同步重置所有已注册的局部时间线
        for (auto& tl : m_Timelines) {
            if (tl) tl->Stop();
        }
    }
    m_State = TimelineState::Playing;

    // 确保所有已注册的局部时间线也进入播放状态
    for (auto& tl : m_Timelines) {
        if (tl) tl->Play();
    }
}

void AnimationGlobalTimeline::Pause() {
    m_State = TimelineState::Paused;

    // 暂停所有已注册的局部时间线
    for (auto& tl : m_Timelines) {
        if (tl) tl->Pause();
    }
}

void AnimationGlobalTimeline::Stop() {
    m_State = TimelineState::Stopped;
    m_GlobalTime = 0.0f;
    m_LastEventCheckTime = 0.0f;

    // 停止所有已注册的局部时间线
    for (auto& tl : m_Timelines) {
        if (tl) tl->Stop();
    }
}

void AnimationGlobalTimeline::Seek(float32 time) {
    m_GlobalTime = (time < 0.0f) ? 0.0f : time;
    m_LastEventCheckTime = m_GlobalTime;

    // 同步所有已注册的局部时间线
    for (auto& tl : m_Timelines) {
        if (tl) tl->Seek(m_GlobalTime);
    }
}

void AnimationGlobalTimeline::SetTimeScale(float32 scale) noexcept {
    m_TimeScale = (scale < 0.0f) ? 0.0f : scale;
}

// ============================================================
// 局部时间线管理
// ============================================================

void AnimationGlobalTimeline::RegisterTimeline(std::shared_ptr<AnimationLocalTimeline> timeline) {
    if (!timeline) return;

    // 避免重复注册
    auto it = std::find_if(m_Timelines.begin(), m_Timelines.end(),
        [&](const std::shared_ptr<AnimationLocalTimeline>& t) { return t == timeline; });
    if (it != m_Timelines.end()) return;

    m_Timelines.push_back(std::move(timeline));
}

void AnimationGlobalTimeline::UnregisterTimeline(AnimationLocalTimeline* timeline) {
    if (!timeline) return;
    auto it = std::remove_if(m_Timelines.begin(), m_Timelines.end(),
        [timeline](const std::shared_ptr<AnimationLocalTimeline>& t) { return t.get() == timeline; });
    m_Timelines.erase(it, m_Timelines.end());
}

std::shared_ptr<AnimationLocalTimeline> AnimationGlobalTimeline::FindTimeline(
    const std::string& name) const {
    for (const auto& tl : m_Timelines) {
        if (tl && tl->GetName() == name)
            return tl;
    }
    return nullptr;
}

// ============================================================
// 全局事件管理
// ============================================================

void AnimationGlobalTimeline::AddEvent(const AnimationEvent& evt) {
    m_Events.push_back(evt);
    std::sort(m_Events.begin(), m_Events.end(),
              [](const AnimationEvent& a, const AnimationEvent& b) { return a.time < b.time; });
}

void AnimationGlobalTimeline::ClearEvents() {
    m_Events.clear();
}

// ============================================================
// 更新
// ============================================================

void AnimationGlobalTimeline::Update(float32 dt) {
    if (!m_Initialized || m_State != TimelineState::Playing)
        return;

    float32 prevTime = m_GlobalTime;

    // 推进全局时间（应用全局时间缩放）
    m_GlobalTime += dt * m_TimeScale;
    if (m_GlobalTime < 0.0f) m_GlobalTime = 0.0f;

    // 触发全局事件
    FireEvents(prevTime, m_GlobalTime);

    // 推进所有已注册的局部时间线
    // 使用索引遍历，以防回调中修改容器
    for (size_t i = 0; i < m_Timelines.size(); ++i) {
        auto& tl = m_Timelines[i];
        if (!tl) continue;

        // 同步模式下，用全局 dt（不带局部时间缩放）推进局部时间线
        // 局部时间线内部的 m_TimeScale 仍然生效
        if (tl->IsPlaying()) {
            tl->AdvanceTime(dt);
            tl->ApplyToTarget();
        }
    }
}

// ============================================================
// 内部：事件触发
// ============================================================

void AnimationGlobalTimeline::FireEvents(float32 prevTime, float32 currentTime) {
    if (m_Events.empty()) return;

    for (const auto& evt : m_Events) {
        if (evt.time > prevTime && evt.time <= currentTime) {
            if (evt.callback) evt.callback();
        }
    }
}

} // namespace Engine
