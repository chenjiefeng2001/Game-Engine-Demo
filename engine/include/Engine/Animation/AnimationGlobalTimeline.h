#pragma once

/**
 * @file AnimationGlobalTimeline.h
 * @brief 全局时间线 — 引擎级时间同步与事件调度
 *
 * AnimationGlobalTimeline 是引擎全局唯一的动画时间线，负责：
 *   - 提供全局一致的动画时间（可用于同步所有局部时间线）
 *   - 管理注册到全局的局部时间线，统一推进
 *   - 全局时间事件（在指定全局时间触发回调）
 *   - 全局播放控制（暂停/恢复/跳转所有动画）
 *   - 全局时间缩放（慢动作/加速效果）
 *
 * 全局时间线与局部时间线的关系：
 *   - 全局时间线独立运行，不受局部时间线影响
 *   - 局部时间线可以选择是否与全局时间线同步
 *   - 同步模式下，局部时间线的 AdvanceTime 由全局时间线驱动
 *   - 独立模式下，局部时间线自己管理时间（通过 Update(dt)）
 *
 * 使用示例：
 * @code
 *   AnimationGlobalTimeline::Get().Play();
 *
 *   // 每帧调用
 *   AnimationGlobalTimeline::Get().Update(dt);
 *
 *   // 注册全局事件
 *   AnimationGlobalTimeline::Get().AddEvent({5.0f, "BossAppear", []() {
 *       spawnBoss();
 *   }});
 * @endcode
 */

#include "Engine/Animation/AnimationKeyFrame.h"
#include "Engine/Animation/AnimationLocalTimeline.h"
#include <string>
#include <memory>
#include <vector>
#include <functional>

namespace Engine {

    // ============================================================
    // 全局时间线（单例）
    // ============================================================
    class AnimationGlobalTimeline {
    public:
        /** 获取全局时间线实例 */
        static AnimationGlobalTimeline& Get();

        // ── 生命周期 ──
        /** 初始化全局时间线 */
        void Init();
        /** 关闭全局时间线 */
        void Shutdown();
        /** 是否已初始化 */
        bool IsInitialized() const noexcept { return m_Initialized; }

        // ── 播放控制 ──
        /** 播放全局时间线（所有同步的局部时间线将随之播放） */
        void Play();
        /** 暂停全局时间线 */
        void Pause();
        /** 停止全局时间线（时间重置到起点，所有同步局部时间线停止） */
        void Stop();
        /** 跳转到指定全局时间 */
        void Seek(float32 time);

        TimelineState GetState() const noexcept { return m_State; }
        bool IsPlaying() const noexcept { return m_State == TimelineState::Playing; }

        // ── 时间控制 ──
        /** 设置全局时间缩放 */
        void SetTimeScale(float32 scale) noexcept;
        float32 GetTimeScale() const noexcept { return m_TimeScale; }

        /** 获取全局时间 */
        float32 GetGlobalTime() const noexcept { return m_GlobalTime; }

        // ── 局部时间线管理 ──
        /**
         * @brief 注册一个局部时间线到全局同步
         *
         * 注册后，全局时间线的 Update() 将自动推进此局部时间线。
         * 被注册的局部时间线不应再手动调用 Update()。
         */
        void RegisterTimeline(std::shared_ptr<AnimationLocalTimeline> timeline);

        /**
         * @brief 取消注册局部时间线
         */
        void UnregisterTimeline(AnimationLocalTimeline* timeline);

        /** 获取所有已注册的局部时间线 */
        const std::vector<std::shared_ptr<AnimationLocalTimeline>>& GetTimelines() const noexcept {
            return m_Timelines;
        }

        /** 按名称查找已注册的局部时间线 */
        std::shared_ptr<AnimationLocalTimeline> FindTimeline(const std::string& name) const;

        // ── 全局事件 ──
        void AddEvent(const AnimationEvent& evt);
        void ClearEvents();

        // ── 更新（每帧由引擎调用） ──
        /**
         * @brief 更新全局时间线
         * @param dt 帧时间步长（秒）
         *
         * 推进全局时间，触发全局事件，并推进所有已注册的局部时间线。
         */
        void Update(float32 dt);

    private:
        AnimationGlobalTimeline() = default;
        ~AnimationGlobalTimeline() = default;
        AnimationGlobalTimeline(const AnimationGlobalTimeline&) = delete;
        AnimationGlobalTimeline& operator=(const AnimationGlobalTimeline&) = delete;

        /** 内部：检查并触发到期的全局事件 */
        void FireEvents(float32 prevTime, float32 currentTime);

        // ── 状态 ──
        bool            m_Initialized  = false;
        TimelineState   m_State        = TimelineState::Stopped;
        float32         m_GlobalTime   = 0.0f;
        float32         m_TimeScale    = 1.0f;

        // ── 局部时间线 ──
        std::vector<std::shared_ptr<AnimationLocalTimeline>> m_Timelines;

        // ── 全局事件 ──
        std::vector<AnimationEvent> m_Events;
        float32 m_LastEventCheckTime = 0.0f;
    };

} // namespace Engine
