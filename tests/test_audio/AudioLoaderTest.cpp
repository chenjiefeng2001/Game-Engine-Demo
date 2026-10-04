// P1: Core/Audio headless 覆盖 —— AudioLoader
//
// 对应 engine/include/Engine/Core/Audio/AudioLoader.h +
// engine/src/Audio/AudioLoader.cpp（217 行实现），此前零直接测试。
//
// 准入结论（审计确认）：Category 1 —— 类内全部是 static 方法，
// 实现只依赖 drwav / stb_vorbis 对**字节流**的处理，不需要 OpenAL 设备。
// 关键是 LoadWAVFromMemory 直接接受内存字节，因此可以在测试里构造
// WAV 字节而完全不碰文件系统（避免 CWD 依赖）。
//
// 覆盖：
//   - 合法 mono16 / stereo16 WAV 的 channels / sampleRate / dataSize /
//     duration / AudioFormat 映射
//   - 非法输入：空缓冲、垃圾字节、截断的 chunk 头、声明长度但无数据
//   - LoadWAV 对不存在文件的行为
//   - AudioData::IsValid() 作为"成功/失败"的唯一判定
//
// 已记录但不在此断言为契约的行为：
//   info.format 的映射是 (channels == 1) ? Mono16 : Stereo16，
//   因此 **>2 声道会被静默标记为 Stereo16**。这是有损映射，
//   是否应改为拒绝或支持多声道属于产品/API 决策，不在本 slice 改变。
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Engine/Core/Audio/AudioLoader.h"

using namespace Engine;

namespace {

void AppendU32(std::vector<uint8>& v, uint32 x) {
    v.push_back(static_cast<uint8>(x & 0xFF));
    v.push_back(static_cast<uint8>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8>((x >> 24) & 0xFF));
}

void AppendU16(std::vector<uint8>& v, uint16 x) {
    v.push_back(static_cast<uint8>(x & 0xFF));
    v.push_back(static_cast<uint8>((x >> 8) & 0xFF));
}

void AppendTag(std::vector<uint8>& v, const char* tag) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8>(tag[i]));
}

/// 构造一个最小合法 16-bit PCM WAV
std::vector<uint8> MakeWav16(uint16 channels, uint32 sampleRate,
                             uint32 frameCount) {
    const uint16 bitsPerSample = 16;
    const uint16 blockAlign = static_cast<uint16>(channels * bitsPerSample / 8);
    const uint32 byteRate = sampleRate * blockAlign;
    const uint32 dataBytes = frameCount * blockAlign;

    std::vector<uint8> w;
    AppendTag(w, "RIFF");
    AppendU32(w, 36u + dataBytes);
    AppendTag(w, "WAVE");

    AppendTag(w, "fmt ");
    AppendU32(w, 16u);                       // fmt chunk size
    AppendU16(w, 1u);                        // PCM
    AppendU16(w, channels);
    AppendU32(w, sampleRate);
    AppendU32(w, byteRate);
    AppendU16(w, blockAlign);
    AppendU16(w, bitsPerSample);

    AppendTag(w, "data");
    AppendU32(w, dataBytes);
    for (uint32 i = 0; i < dataBytes; ++i) w.push_back(0);   // 静音
    return w;
}

} // namespace

// ── 合法 WAV：mono ─────────────────────────────────────────────────────
TEST(AudioLoaderTest, LoadWAVFromMemory_Mono16_ReportsOneChannel)
{
    const auto wav = MakeWav16(1, 44100, 8);
    const AudioData d = AudioLoader::LoadWAVFromMemory(wav.data(), wav.size());
    ASSERT_TRUE(d.IsValid()) << "mono WAV should decode";
    EXPECT_EQ(d.channels, 1);
    EXPECT_EQ(d.sampleRate, 44100);
    EXPECT_EQ(d.info.format, AudioFormat::Mono16);
}

TEST(AudioLoaderTest, LoadWAVFromMemory_DataSizeIsFramesTimesChannelsTimesTwo)
{
    const uint32 frames = 100;
    const auto wav = MakeWav16(2, 48000, frames);
    const AudioData d = AudioLoader::LoadWAVFromMemory(wav.data(), wav.size());
    ASSERT_TRUE(d.IsValid());
    // loader 统一读成 int16，故每帧每声道 2 字节
    EXPECT_EQ(d.info.dataSize, static_cast<int32>(frames * 2 * 2));
    EXPECT_EQ(static_cast<uint32>(d.pcmData.size()),
              static_cast<uint32>(d.info.dataSize));
}

TEST(AudioLoaderTest, LoadWAVFromMemory_DurationIsFramesOverSampleRate)
{
    const uint32 frames = 44100;   // 1 秒 @ 44100
    const auto wav = MakeWav16(1, 44100, frames);
    const AudioData d = AudioLoader::LoadWAVFromMemory(wav.data(), wav.size());
    ASSERT_TRUE(d.IsValid());
    EXPECT_NEAR(d.info.duration, 1.0f, 1e-3f);
    EXPECT_EQ(d.info.sampleRate, 44100);
}

// ── 合法 WAV：stereo ───────────────────────────────────────────────────
TEST(AudioLoaderTest, LoadWAVFromMemory_Stereo16_ReportsStereoFormat)
{
    const auto wav = MakeWav16(2, 22050, 16);
    const AudioData d = AudioLoader::LoadWAVFromMemory(wav.data(), wav.size());
    ASSERT_TRUE(d.IsValid());
    EXPECT_EQ(d.channels, 2);
    EXPECT_EQ(d.info.format, AudioFormat::Stereo16);
    EXPECT_EQ(d.sampleRate, 22050);
}

TEST(AudioLoaderTest, LoadWAVFromMemory_StereoDataSizeIsDoubleMono)
{
    const uint32 frames = 50;
    const auto mono = MakeWav16(1, 44100, frames);
    const auto stereo = MakeWav16(2, 44100, frames);
    const AudioData a = AudioLoader::LoadWAVFromMemory(mono.data(), mono.size());
    const AudioData b = AudioLoader::LoadWAVFromMemory(stereo.data(), stereo.size());
    ASSERT_TRUE(a.IsValid());
    ASSERT_TRUE(b.IsValid());
    EXPECT_EQ(b.info.dataSize, a.info.dataSize * 2);
}

TEST(AudioLoaderTest, LoadWAVFromMemory_DurationIndependentOfChannelCount)
{
    const uint32 frames = 22050;
    const auto mono = MakeWav16(1, 22050, frames);
    const auto stereo = MakeWav16(2, 22050, frames);
    const AudioData a = AudioLoader::LoadWAVFromMemory(mono.data(), mono.size());
    const AudioData b = AudioLoader::LoadWAVFromMemory(stereo.data(), stereo.size());
    ASSERT_TRUE(a.IsValid());
    ASSERT_TRUE(b.IsValid());
    EXPECT_NEAR(a.info.duration, 1.0f, 1e-3f);
    EXPECT_NEAR(b.info.duration, 1.0f, 1e-3f);
}

// ── 非法输入 ───────────────────────────────────────────────────────────
TEST(AudioLoaderTest, LoadWAVFromMemory_EmptyBuffer_ReturnsInvalid)
{
    const AudioData d = AudioLoader::LoadWAVFromMemory(nullptr, 0);
    EXPECT_FALSE(d.IsValid());
    EXPECT_TRUE(d.pcmData.empty());
    EXPECT_EQ(d.info.dataSize, 0);
}

TEST(AudioLoaderTest, LoadWAVFromMemory_GarbageBytes_ReturnsInvalid)
{
    const char junk[] = "this is definitely not a RIFF WAVE file";
    const AudioData d = AudioLoader::LoadWAVFromMemory(junk, sizeof(junk));
    EXPECT_FALSE(d.IsValid());
}

TEST(AudioLoaderTest, LoadWAVFromMemory_TruncatedHeader_ReturnsInvalid)
{
    auto wav = MakeWav16(1, 44100, 4);
    wav.resize(20);   // 只剩 RIFF + size + WAVE + 部分 fmt
    const AudioData d = AudioLoader::LoadWAVFromMemory(wav.data(), wav.size());
    EXPECT_FALSE(d.IsValid());
}

TEST(AudioLoaderTest, LoadWAVFromMemory_TruncatedDataChunk_DecodesFewerFrames)
{
    // 已记录的行为（不是理想契约）：drwav 对缺失的 data chunk 字节**容忍**，
    // 只解码实际存在的帧，而不是报错。pcmData 因此非空、IsValid() 为真，
    // 但时长只有原始声明的一部分 —— 截断的音频文件不会 fail loudly。
    // 若将来决定严格校验，这里应改为 EXPECT_FALSE(IsValid())。
    const uint32 frames = 64;
    auto wav = MakeWav16(1, 44100, frames);
    const size_t declaredSize = wav.size();
    wav.resize(declaredSize - 64);          // 砍掉最后 64 字节的采样数据

    const AudioData d = AudioLoader::LoadWAVFromMemory(wav.data(), wav.size());
    EXPECT_TRUE(d.IsValid());
    EXPECT_LT(d.info.dataSize, static_cast<int32>(frames * 2));
}

TEST(AudioLoaderTest, LoadWAVFromMemory_ZeroFrameWav_IsInvalid)
{
    const auto wav = MakeWav16(1, 44100, 0);
    const AudioData d = AudioLoader::LoadWAVFromMemory(wav.data(), wav.size());
    // 没有 PCM 帧 → pcmData 为空 → IsValid() 为假
    EXPECT_FALSE(d.IsValid());
}

TEST(AudioLoaderTest, IsValid_RequiresBothPcmAndDataSize)
{
    AudioData d;
    EXPECT_FALSE(d.IsValid());
    d.pcmData.push_back(0);
    EXPECT_FALSE(d.IsValid());          // dataSize 仍为 0
    d.info.dataSize = 1;
    EXPECT_TRUE(d.IsValid());
}

// ── 文件入口 ───────────────────────────────────────────────────────────
TEST(AudioLoaderTest, LoadWAV_NonexistentFile_ReturnsInvalid)
{
    const std::string missing = (std::filesystem::temp_directory_path() /
                                 "engine_audio_tests_absent" / "nope.wav").string();
    const AudioData d = AudioLoader::LoadWAV(missing);
    EXPECT_FALSE(d.IsValid());
}

TEST(AudioLoaderTest, LoadWAV_ValidFileOnDisk_MatchesInMemoryDecode)
{
    const auto dir = std::filesystem::temp_directory_path() / "engine_audio_tests";
    std::filesystem::create_directories(dir);
    const std::string path = (dir / "sample.wav").string();
    const auto wav = MakeWav16(2, 44100, 32);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(wav.data()),
                  static_cast<std::streamsize>(wav.size()));
    }

    const AudioData fromFile = AudioLoader::LoadWAV(path);
    const AudioData fromMem = AudioLoader::LoadWAVFromMemory(wav.data(), wav.size());
    ASSERT_TRUE(fromFile.IsValid());
    ASSERT_TRUE(fromMem.IsValid());
    EXPECT_EQ(fromFile.channels, fromMem.channels);
    EXPECT_EQ(fromFile.sampleRate, fromMem.sampleRate);
    EXPECT_EQ(fromFile.info.dataSize, fromMem.info.dataSize);
    EXPECT_EQ(fromFile.info.format, fromMem.info.format);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(AudioLoaderTest, Load_UnknownExtension_ReturnsInvalid)
{
    // 未扩展名分派的后缀不应被当成可解码音频
    const AudioData d = AudioLoader::Load("not_audio_at_all.xyz");
    EXPECT_FALSE(d.IsValid());
}

TEST(AudioLoaderTest, Load_EmptyPath_ReturnsInvalid)
{
    const AudioData d = AudioLoader::Load("");
    EXPECT_FALSE(d.IsValid());
}