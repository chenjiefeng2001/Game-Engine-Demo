#pragma once

/**
 * @file IAudioBuffer.h
 * @brief 音频缓冲区接口 — 不依赖任何第三方音频库
 *
 * 设计原则（与 IRenderQueue 等 RHI 接口一致）：
 *   - 纯虚接口，引擎核心只依赖此抽象
 *   - OpenAL 实现层提供具体实现
 *   - 所有方法使用引擎自有类型
 */

#include "Engine/Core/Audio/AudioDefs.h"
#include <memory>

namespace Engine {

    class IAudioBuffer {
    public:
        virtual ~IAudioBuffer() = default;

        /**
         * @brief 从 PCM 数据加载音频
         * @param pcmData  PCM 数据指针
         * @param dataSize PCM 数据大小（字节）
         * @param info     音频格式信息
         */
        virtual void Load(const void* pcmData, int32 dataSize,
                          const AudioClipInfo& info) = 0;

        /// 获取音频信息
        virtual AudioClipInfo GetInfo() const = 0;

        /// 获取 PCM 数据大小（字节）
        virtual int32 GetDataSize() const = 0;

        /// 获取原生 OpenAL buffer 句柄（仅实现层使用）
        virtual uint32 GetNativeHandle() const = 0;
    };

} // namespace Engine
