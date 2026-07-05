#include "Engine/Core/Audio/AudioLoader.h"
#include "Engine/Core/Log.h"
#include <cstring>

namespace {
    Engine::Logger s_Log("AudioLoader");
}
#include <string>
#include <algorithm>

// ── 第三方单头库 ──
#include "stb_vorbis.h"
#include "dr_wav.h"

namespace Engine {

    // ============================================================
    // 工具函数：将 AudioFormat 映射到 dr_wav 格式/位数
    // ============================================================
    namespace {

        /** 获取文件扩展名（小写） */
        std::string GetExtension(const std::string& filePath) {
            size_t dot = filePath.find_last_of('.');
            if (dot == std::string::npos) return "";
            std::string ext = filePath.substr(dot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return ext;
        }

    } // anonymous namespace

    // ============================================================
    // LoadWAV — 从文件加载 WAV
    // ============================================================

    AudioData AudioLoader::LoadWAV(const std::string& filePath) {
        unsigned int channels = 0;
        unsigned int sampleRate = 0;
        drwav_uint64 totalPCMFrameCount = 0;

        // 使用 dr_wav 的便捷 API 直接加载解码为 16-bit PCM
        drwav_int16* pSampleData = drwav_open_file_and_read_pcm_frames_s16(
            filePath.c_str(),
            &channels,
            &sampleRate,
            &totalPCMFrameCount,
            nullptr
        );

        if (!pSampleData) {
            s_Log.Error("Failed to load WAV file: {}", filePath);
            return AudioData();
        }

        size_t totalSamples = static_cast<size_t>(totalPCMFrameCount * channels);
        size_t dataSize = totalSamples * sizeof(int16);

        AudioData result;
        result.pcmData.resize(dataSize);
        std::memcpy(result.pcmData.data(), pSampleData, dataSize);

        result.channels   = static_cast<int32>(channels);
        result.sampleRate = static_cast<int32>(sampleRate);

        // 填充 AudioClipInfo
        result.info.sampleRate = static_cast<int32>(sampleRate);
        result.info.dataSize   = static_cast<int32>(dataSize);
        result.info.duration   = static_cast<float32>(totalPCMFrameCount) / sampleRate;
        result.info.format     = (channels == 1) ? AudioFormat::Mono16 : AudioFormat::Stereo16;

        drwav_free(pSampleData, nullptr);

        return result;
    }

    // ============================================================
    // LoadWAVFromMemory — 从内存加载 WAV
    // ============================================================

    AudioData AudioLoader::LoadWAVFromMemory(const void* data, size_t dataSize) {
        unsigned int channels = 0;
        unsigned int sampleRate = 0;
        drwav_uint64 totalPCMFrameCount = 0;

        drwav_int16* pSampleData = drwav_open_memory_and_read_pcm_frames_s16(
            data, dataSize,
            &channels,
            &sampleRate,
            &totalPCMFrameCount,
            nullptr
        );

        if (!pSampleData) {
            s_Log.Error("Failed to load WAV from memory");
            return AudioData();
        }

        size_t totalSamples = static_cast<size_t>(totalPCMFrameCount * channels);
        size_t byteSize = totalSamples * sizeof(int16);

        AudioData result;
        result.pcmData.resize(byteSize);
        std::memcpy(result.pcmData.data(), pSampleData, byteSize);

        result.channels   = static_cast<int32>(channels);
        result.sampleRate = static_cast<int32>(sampleRate);
        result.info.sampleRate = static_cast<int32>(sampleRate);
        result.info.dataSize   = static_cast<int32>(byteSize);
        result.info.duration   = static_cast<float32>(totalPCMFrameCount) / sampleRate;
        result.info.format     = (channels == 1) ? AudioFormat::Mono16 : AudioFormat::Stereo16;

        drwav_free(pSampleData, nullptr);

        return result;
    }

    // ============================================================
    // LoadOGG — 从文件加载 Ogg Vorbis
    // ============================================================

    AudioData AudioLoader::LoadOGG(const std::string& filePath) {
        int channels = 0;
        int sampleRate = 0;
        short* pOutput = nullptr;

        int totalSamples = stb_vorbis_decode_filename(
            filePath.c_str(),
            &channels,
            &sampleRate,
            &pOutput
        );

        if (totalSamples <= 0) {
            s_Log.Error("Failed to load OGG file: {}", filePath);
            return AudioData();
        }

        // totalSamples 是每声道的样本数，交错数据总长度为 totalSamples * channels
        size_t totalSampleCount = static_cast<size_t>(totalSamples) * channels;
        size_t dataSize = totalSampleCount * sizeof(int16);

        AudioData result;
        result.pcmData.resize(dataSize);
        std::memcpy(result.pcmData.data(), pOutput, dataSize);

        result.channels   = static_cast<int32>(channels);
        result.sampleRate = static_cast<int32>(sampleRate);
        result.info.sampleRate = static_cast<int32>(sampleRate);
        result.info.dataSize   = static_cast<int32>(dataSize);
        result.info.duration   = static_cast<float32>(totalSamples) / sampleRate;
        result.info.format     = (channels == 1) ? AudioFormat::Mono16 : AudioFormat::Stereo16;

        free(pOutput);

        return result;
    }

    // ============================================================
    // LoadOGGFromMemory — 从内存加载 Ogg Vorbis
    // ============================================================

    AudioData AudioLoader::LoadOGGFromMemory(const void* data, size_t dataSize) {
        int channels = 0;
        int sampleRate = 0;
        short* pOutput = nullptr;

        int totalSamples = stb_vorbis_decode_memory(
            static_cast<const unsigned char*>(data),
            static_cast<int>(dataSize),
            &channels,
            &sampleRate,
            &pOutput
        );

        if (totalSamples <= 0) {
            s_Log.Error("Failed to load OGG from memory");
            return AudioData();
        }

        size_t totalSampleCount = static_cast<size_t>(totalSamples) * channels;
        size_t byteSize = totalSampleCount * sizeof(int16);

        AudioData result;
        result.pcmData.resize(byteSize);
        std::memcpy(result.pcmData.data(), pOutput, byteSize);

        result.channels   = static_cast<int32>(channels);
        result.sampleRate = static_cast<int32>(sampleRate);
        result.info.sampleRate = static_cast<int32>(sampleRate);
        result.info.dataSize   = static_cast<int32>(byteSize);
        result.info.duration   = static_cast<float32>(totalSamples) / sampleRate;
        result.info.format     = (channels == 1) ? AudioFormat::Mono16 : AudioFormat::Stereo16;

        free(pOutput);

        return result;
    }

    // ============================================================
    // Load — 根据扩展名自动选择加载器
    // ============================================================

    AudioData AudioLoader::Load(const std::string& filePath) {
        std::string ext = GetExtension(filePath);

        if (ext == "wav") {
            return LoadWAV(filePath);
        } else if (ext == "ogg") {
            return LoadOGG(filePath);
        } else {
            s_Log.Error("Unsupported audio format: .{}", ext);
            return AudioData();
        }
    }

} // namespace Engine
