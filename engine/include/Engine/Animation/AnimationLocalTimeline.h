#pragma once

/**
 * @file AnimationLocalTimeline.h
 * @brief 局部时间线 — 挂载到 GameObject 上的动画实例
 *
 * AnimationLocalTimeline（简称 LocalTimeline）是一个完整的动画实例，
 * 可以绑定到一个 GameObject 上，控制其位置/旋转/缩放/颜色等属性。
 *
 * 核心能力：
 *   - 独立的播放控制（Play / Pause / Stop / Seek）
 *   - 循环模式（Once / Loop / PingPong）
 *   - 多条属性轨道（位置、旋转、缩放、颜色、自定义浮点）
 *   - 时间点事件（在指定时间触发回调）
 *   - 可由全局时间线驱动同步，也可独立运行
 *   - 支持时间缩放
 *
 * 使用示例：
 * @code
 *   auto anim = std::make_shared<AnimationLocalTimeline>("FloatAnim");
 *   anim->SetTarget(gameObject);
 *   anim->GetPositionTrack().AddKeyFrame({0.0f, Vec3(0,0,0)});
 *   anim->GetPositionTrack().AddKeyFrame({1.0f, Vec3(10,0,0)});
 *   anim->Play();
 *
 *   // 每帧调用
 *   anim->Update(dt);
 * @endcode
 */

#include "Engine/Animation/AnimationKeyFrame.h"
#include "Engine/Animation/AnimationTrack.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <string>
#include <memory>
#include <vector>
#include <functional>

namespace Engine {

    class GameObject;

    // ============================================================
    // 局部时间线
    // ============================================================
    class AnimationLocalTimeline : public std::enable_shared_from_this<AnimationLocalTimeline> {
    public:
        explicit AnimationLocalTimeline(std::string name = "Animation");
        virtual ~AnimationLocalTimeline() = default;

        // 禁止拷贝
        AnimationLocalTimeline(const AnimationLocalTimeline&) = delete;
        AnimationLocalTimeline& operator=(const AnimationLocalTimeline&) = delete;

        // ── 标识 ──
        const std::string& GetName() const noexcept { return m_Name; }
        void SetName(const std::string& name) { m_Name = name; }

        // ── 目标对象 ──
        /** 绑定到 GameObject（动画将驱动此对象的 Transform / Sprite 等） */
        void SetTarget(std::shared_ptr<GameObject> target) { m_Target = target; }
        std::shared_ptr<GameObject> GetTarget() const noexcept { return m_Target.lock(); }
        /** 检查目标是否仍然有效 */
        bool HasValidTarget() const noexcept { return !m_Target.expired(); }

        // ── 播放控制 ──
        /** 开始播放 */
        void Play();
        /** 暂停播放 */
        void Pause();
        /** 停止播放（时间重置到起点） */
        void Stop();
        /** 跳转到指定时间位置 */
        void Seek(float32 time);
        /** 重新开始播放 */
        void Restart();

        /** 获取当前状态 */
        TimelineState GetState() const noexcept { return m_State; }
        /** 是否正在播放 */
        bool IsPlaying() const noexcept { return m_State == TimelineState::Playing; }

        // ── 时间控制 ──
        /** 设置循环模式 */
        void SetLoopMode(AnimationLoopMode mode) noexcept { m_LoopMode = mode; }
        AnimationLoopMode GetLoopMode() const noexcept { return m_LoopMode; }

        /** 设置时间缩放 */
        void SetTimeScale(float32 scale) noexcept { m_TimeScale = (scale < 0.0f) ? 0.0f : scale; }
        float32 GetTimeScale() const noexcept { return m_TimeScale; }

        /** 获取当前本地时间 */
        float32 GetLocalTime() const noexcept { return m_LocalTime; }

        /** 获取动画总时长 */
        float32 GetDuration() const noexcept { return m_Duration; }
        /** 设置动画总时长（如果轨道时长更长，以轨道为准） */
        void SetDuration(float32 duration) noexcept { m_Duration = duration; }

        /** 获取动画播放进度 [0, 1] */
        float32 GetProgress() const;

        // ── 轨道访问 ──
        AnimationTrack& GetPositionTrack() noexcept { return m_PositionTrack; }
        AnimationTrack& GetRotationTrack()   noexcept { return m_RotationTrack; }
        AnimationTrack& GetScaleTrack()      noexcept { return m_ScaleTrack; }
        AnimationTrack& GetColorTrack()      noexcept { return m_ColorTrack; }

        const AnimationTrack& GetPositionTrack() const noexcept { return m_PositionTrack; }
        const AnimationTrack& GetRotationTrack()   const noexcept { return m_RotationTrack; }
        const AnimationTrack& GetScaleTrack()      const noexcept { return m_ScaleTrack; }
        const AnimationTrack& GetColorTrack()      const noexcept { return m_ColorTrack; }

        /** 添加自定义浮点轨道（由字符串标识） */
        AnimationTrack& AddFloatTrack(const std::string& name);
        /** 获取自定义浮点轨道 */
        AnimationTrack* GetFloatTrack(const std::string& name);
        const AnimationTrack* GetFloatTrack(const std::string& name) const;

        // ── 事件管理 ──
        void AddEvent(const AnimationEvent& evt);
        void ClearEvents();

        // ── 更新（每帧调用） ──
        /**
         * @brief 更新动画
         * @param dt 帧时间步长（秒）
         *
         * 如果使用全局时间线驱动，应调用 AdvanceTime() + ApplyToTarget()
         * 而不是 Update()，以避免双重时间累加。
         */
        void Update(float32 dt);

        /**
         * @brief 仅推进时间（不应用属性变化）
         * @param dt 时间步长
         *
         * 用于全局时间线同步时，由外部控制时间推进。
         */
        void AdvanceTime(float32 dt);

        /**
         * @brief 将当前时间点的轨道值应用到目标对象
         *
         * 在 AdvanceTime 之后调用，或者在 Seek 之后调用以刷新。
         */
        void ApplyToTarget();

        // ── 完成回调 ──
        /** 设置动画播放完成的回调（单次播放结束或循环结束等） */
        void SetOnComplete(std::function<void()> callback) { m_OnComplete = std::move(callback); }

    private:
        /** 内部：触发已到达的事件 */
        void FireEvents(float32 prevTime, float32 currentTime);

        // ── 状态 ──
        std::string         m_Name;
        TimelineState       m_State        = TimelineState::Stopped;
        AnimationLoopMode   m_LoopMode     = AnimationLoopMode::Once;
        float32             m_LocalTime    = 0.0f;
        float32             m_Duration     = 0.0f;
        float32             m_TimeScale    = 1.0f;

        // ── 目标 ──
        std::weak_ptr<GameObject> m_Target;

        // ── 轨道 ──
        AnimationTrack m_PositionTrack;   // Vec3 — 位置
        AnimationTrack m_RotationTrack;   // Vec3 — 旋转（欧拉角）
        AnimationTrack m_ScaleTrack;      // Vec3 — 缩放
        AnimationTrack m_ColorTrack;      // Vec4 — 颜色（用于 SpriteComponent）

        struct FloatTrackEntry {
            std::string    name;
            AnimationTrack track;
        };
        std::vector<FloatTrackEntry> m_FloatTracks;

        // ── 事件 ──
        std::vector<AnimationEvent> m_Events;
        float32 m_LastEventCheckTime = 0.0f;

        // ── 完成回调 ──
        std::function<void()> m_OnComplete;
    };

} // namespace Engine
