#pragma once

/**
 * @file IAudioSource.h
 * @brief 音频源接口 — 不依赖任何第三方音频库
 *
 * 设计原则（与 IPhysicsBody 等 RHI 接口一致）：
 *   - 纯虚接口，引擎核心只依赖此抽象
 *   - 所有方法使用引擎自有类型（Vec3 等）
 */

#include "Engine/Core/Audio/AudioDefs.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <memory>
#include <cfloat>

namespace Engine {

    class IAudioBuffer;

    class IAudioSource {
    public:
        virtual ~IAudioSource() = default;

        // ── 播放控制 ──
        virtual AudioStatus Play(std::shared_ptr<IAudioBuffer> buffer) = 0;
        virtual AudioStatus Stop() = 0;
        virtual AudioStatus Pause() = 0;
        virtual AudioStatus Resume() = 0;

        // ── 状态查询 ──
        virtual PlayState GetState() const = 0;
        virtual bool IsPlaying() const = 0;
        virtual AudioStatus GetLastStatus() const = 0;

        // ── 3D 空间属性 ──
        virtual AudioStatus SetPosition(const Vec3& position) = 0;
        virtual Vec3 GetPosition() const = 0;
        virtual AudioStatus SetVelocity(const Vec3& velocity) = 0;
        virtual Vec3 GetVelocity() const = 0;

        // ── 音频属性 ──
        virtual AudioStatus SetVolume(float32 gain) = 0;  ///< 0.0 ~ 1.0
        virtual float32 GetVolume() const = 0;
        virtual AudioStatus SetPitch(float32 pitch) = 0;   ///< 0.5 ~ 2.0
        virtual float32 GetPitch() const = 0;
        virtual AudioStatus SetLooping(bool loop) = 0;
        virtual bool IsLooping() const = 0;

        // ── 衰减参数 ──
        virtual AudioStatus SetAttenuation(AttenuationMode mode,
                                           float32 minDistance = 1.0f,
                                           float32 maxDistance = FLT_MAX) = 0;

        // ── 播放进度（秒） ──
        virtual float32 GetPlaybackPosition() const = 0;
        virtual AudioStatus SetPlaybackPosition(float32 seconds) = 0;

        // ── Engine 生命周期 ──
        /// 使 wrapper 失效；不得调用 AL
        virtual void Invalidate() = 0;
        /// 是否由指定 engine runtime 创建（内部身份校验；不暴露 Bridge ABI）
        virtual bool IsOwnedBy(const void* runtime) const = 0;

        // ── 获取原生 OpenAL source 句柄（仅实现层使用） ──
        virtual uint32 GetNativeHandle() const = 0;
    };

} // namespace Engine
