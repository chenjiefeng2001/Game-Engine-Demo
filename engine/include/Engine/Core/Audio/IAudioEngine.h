#pragma once

/**
 * @file IAudioEngine.h
 * @brief 音频引擎接口 — 不依赖任何第三方音频库
 *
 * 设计原则（与 IGraphicsFactory 等工厂接口一致）：
 *   - 纯虚接口，引擎核心只依赖此抽象
 *   - OpenAL 实现层提供具体实现
 *   - 管理音频设备、听者、资源池
 */

#include "Engine/Core/Audio/AudioDefs.h"
#include "Engine/Core/Audio/IAudioBuffer.h"
#include "Engine/Core/Audio/IAudioSource.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <memory>

namespace Engine {

    class IAudioEngine {
    public:
        virtual ~IAudioEngine() = default;

        // ── 初始化 / 关闭 ──
        virtual bool Init() = 0;
        virtual void Shutdown() = 0;
        virtual bool IsInitialized() const = 0;

        // ── 资源管理 ──
        /**
         * @brief 从 PCM 数据创建音频缓冲区
         * @param pcmData  PCM 数据
         * @param dataSize 数据大小（字节）
         * @param info     音频格式信息
         * @return 音频缓冲区（可被多个 IAudioSource 共享）
         */
        virtual std::shared_ptr<IAudioBuffer> CreateBuffer(
            const void* pcmData, int32 dataSize,
            const AudioClipInfo& info) = 0;

        /**
         * @brief 创建一个音源
         * @return 音源对象，可播放 IAudioBuffer
         */
        virtual std::shared_ptr<IAudioSource> CreateSource() = 0;

        /// 销毁音源
        virtual void DestroySource(IAudioSource* source) = 0;

        // ── 听者（Listener）管理 ──
        virtual void SetListenerPosition(const Vec3& position) = 0;
        virtual Vec3 GetListenerPosition() const = 0;

        /**
         * @brief 设置听者朝向
         * @param forward 前方向量
         * @param up      上方向量（通常为 {0,1,0}）
         */
        virtual void SetListenerOrientation(const Vec3& forward,
                                            const Vec3& up) = 0;

        virtual void SetListenerVolume(float32 gain) = 0;

        // ── 全局控制 ──
        virtual void SetMasterVolume(float32 gain) = 0;
        virtual float32 GetMasterVolume() const = 0;

        /**
         * @brief 设置衰减模式（所有音源的默认值）
         */
        virtual void SetDistanceModel(AttenuationMode mode) = 0;

        // ── 每帧更新 ──
        /**
         * @brief 每帧调用，处理异步事件等
         */
        virtual void Update() = 0;

        // ── 获取原生 OpenAL 设备/上下文句柄（仅实现层使用） ──
        virtual void* GetNativeDevice() const = 0;
        virtual void* GetNativeContext() const = 0;
    };

} // namespace Engine
