#pragma once

/**
 * @file AudioLoader.h
 * @brief 音频文件加载器 — 使用 dr_wav 和 stb_vorbis 加载音频文件
 *
 * 支持格式：
 *   - WAV (通过 dr_wav)
 *   - Ogg Vorbis (通过 stb_vorbis)
 *
 * 输出格式：16-bit PCM 交错数据，与 IAudioBuffer 兼容。
 */

#include "Engine/Core/Audio/AudioDefs.h"
#include <memory>
#include <string>
#include <vector>

namespace Engine {

/**
 * @brief 解码后的音频数据
 *
 * 包含原始 PCM 数据和格式信息，可直接用于 IAudioBuffer::Load()。
 */
struct AudioData {
    std::vector<uint8> pcmData;     
    AudioClipInfo      info;       
    int32              channels;    
    int32              sampleRate; 

    bool IsValid() const { return !pcmData.empty() && info.dataSize > 0; }
};

/**
 * @brief 音频文件加载器
 *
 * 提供静态方法加载 WAV 和 Ogg Vorbis 文件，返回 AudioData。
 */
class AudioLoader {
public:
    /**
     * @brief 从 WAV 文件加载音频
     * @param filePath WAV 文件路径
     * @return 解码后的音频数据（失败返回无效 AudioData）
     */
    static AudioData LoadWAV(const std::string& filePath);

    /**
     * @brief 从内存加载 WAV 音频
     * @param data      WAV 文件数据
     * @param dataSize  数据大小
     * @return 解码后的音频数据
     */
    static AudioData LoadWAVFromMemory(const void* data, size_t dataSize);

    /**
     * @brief 从 Ogg Vorbis 文件加载音频
     * @param filePath OGG 文件路径
     * @return 解码后的音频数据（失败返回无效 AudioData）
     */
    static AudioData LoadOGG(const std::string& filePath);

    /**
     * @brief 从内存加载 Ogg Vorbis 音频
     * @param data      OGG 文件数据
     * @param dataSize  数据大小
     * @return 解码后的音频数据
     */
    static AudioData LoadOGGFromMemory(const void* data, size_t dataSize);

    /**
     * @brief 根据扩展名自动选择合适的加载器
     * @param filePath 音频文件路径（支持 .wav, .ogg）
     * @return 解码后的音频数据
     */
    static AudioData Load(const std::string& filePath);
};

} // namespace Engine
