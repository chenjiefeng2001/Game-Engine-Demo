#pragma once

/**
 * @file AudioClip.h
 * @brief 音频资源类 — 整合加载与 OpenAL 缓冲区管理
 *
 * AudioClip 是一个高层资源类，将 AudioLoader 的文件解码与
 * OpenAL 缓冲区（ALuint）的创建/销毁合并为一步操作。
 *
 * 设计目标：
 *   - 从文件或内存加载音频 → 自动解码 → 上传到 OpenAL
 *   - RAII 管理 ALuint 缓冲区生命周期
 *   - 提供格式、采样率、时长等查询接口
 *
 * 使用示例：
 *   Engine::AudioClip clip;
 *   if (clip.LoadFromFile("explosion.wav")) {
 *       alSourcei(source, AL_BUFFER, clip.GetBufferHandle());
 *       alSourcePlay(source);
 *   }
 */

#include "Engine/Core/Audio/AudioDefs.h"
#include "Engine/Core/Audio/AudioLoader.h"
#include <string>

namespace Engine {

class AudioClip {
public:
    AudioClip() = default;
    ~AudioClip();

    // 禁止拷贝，允许移动
    AudioClip(const AudioClip&) = delete;
    AudioClip& operator=(const AudioClip&) = delete;

    AudioClip(AudioClip&& other) noexcept;
    AudioClip& operator=(AudioClip&& other) noexcept;

    // ── 加载 ──

    /**
     * @brief 从音频文件加载（支持 .wav / .ogg，根据扩展名自动选择解码器）
     * @param filePath 音频文件路径
     * @return 成功返回 true
     */
    bool LoadFromFile(const std::string& filePath);

    /**
     * @brief 从内存加载音频数据
     * @param data     音频文件数据指针
     * @param dataSize 数据大小（字节）
     * @param hint     文件扩展名提示（如 "wav" / "ogg"），用于选择解码器
     * @return 成功返回 true
     */
    bool LoadFromMemory(const void* data, size_t dataSize,
                        const std::string& hint = "");

    // ── 查询 ──

    /// 获取原生 OpenAL buffer 句柄（ALuint）
    uint32 GetBufferHandle() const { return m_BufferID; }

    /// 获取音频元信息
    const AudioClipInfo& GetInfo() const { return m_Info; }

    /// 音频时长（秒）
    float32 GetDuration() const { return m_Info.duration; }

    /// 采样率（Hz）
    int32 GetSampleRate() const { return m_Info.sampleRate; }

    /// 音频格式
    AudioFormat GetFormat() const { return m_Info.format; }

    /// 数据大小（字节）
    int32 GetDataSize() const { return m_Info.dataSize; }

    /// 是否已成功加载有效数据
    bool IsValid() const { return m_BufferID != 0 && m_Info.dataSize > 0; }

    // ── 生命周期 ──

    /// 手动释放 OpenAL 缓冲区（析构函数自动调用）
    void Release();

private:
    /// 内部：将解码后的 PCM 数据上传到 OpenAL 缓冲区
    bool LoadPCM(const AudioData& audioData);

    uint32    m_BufferID = 0;
    AudioClipInfo m_Info{};
};

} // namespace Engine
