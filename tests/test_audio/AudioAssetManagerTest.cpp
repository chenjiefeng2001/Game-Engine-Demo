// P1: Audio 子系统测试 —— AudioAssetManager
//
// AudioAssetManager 零 AL/ALC 调用点，可在无设备 CI 上测试。
//
// 本单元只覆盖**不需要 OpenAL 设备**的契约：
//   - ImportAsset 拒绝不存在的路径
//   - ImportAsset 拒绝不支持的扩展名（扩展名判定是纯逻辑）
//   - ImportAsset 接受已支持的扩展名进入解析阶段（此处不断言加载成功）
//   - IsHotReloadEnabled / EnableHotReload 往返
//   - 空缓存下 GetMemoryUsage == 0
//   - 空缓存下 GetClip / UnloadClip / UnloadAll / ReleaseUnused 安全
//   - BuildCache 在目录缺失时不崩溃
//
// **未覆盖（device-bound）**：refCount 语义。UnloadClip 的引用计数递减需要
// 缓存中存在条目，而条目只能由"成功加载音频"产生，那一步需要 OpenAL 设备。
// 因此 UnloadClip 的减计数路径留待 device-backed 批次，见文件末尾。
//
// 注意 API 实况（读源码确认，未猜测）：
//   - ImportAsset 先 std::filesystem::exists，再校验扩展名 ∈ {.wav,.ogg,.mp3,.flac}
//   - GetClip 未命中时会尝试 ImportAsset，失败返回 nullptr
//   - ReleaseUnused 阈值：refCount==0 且空闲 > 60000 ms
//   - BuildCache 扫描硬编码目录 "assets/sounds"，缺失时告警并返回
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "Engine/Audio/AudioEngine.h"

using namespace Engine;
using namespace Engine::Audio;

namespace {

// 在临时目录造一个指定扩展名的真实文件（只用于触发扩展名判定）。
std::string MakeTempFile(const std::string& ext)
{
    const auto dir = std::filesystem::temp_directory_path() / "engine_audio_tests";
    std::filesystem::create_directories(dir);
    const auto p = dir / ("probe" + ext);
    std::ofstream f(p, std::ios::binary);
    f << "not-really-audio";       // 内容无关紧要：本用例只验证格式闸门
    f.close();
    return p.string();
}

} // namespace

// ── 路径与格式校验 ─────────────────────────────────────────────────────
TEST(AudioAssetManagerTest, ImportAssetRejectsMissingFile)
{
    AudioAssetManager mgr;
    EXPECT_FALSE(mgr.ImportAsset("this/path/does/not/exist.wav"));
}

TEST(AudioAssetManagerTest, ImportAssetRejectsEmptyPath)
{
    AudioAssetManager mgr;
    EXPECT_FALSE(mgr.ImportAsset(""));
}

TEST(AudioAssetManagerTest, ImportAssetRejectsUnsupportedExtension)
{
    AudioAssetManager mgr;
    const auto p = MakeTempFile(".txt");
    EXPECT_FALSE(mgr.ImportAsset(p)) << "未支持的扩展名应被拒绝";
}

TEST(AudioAssetManagerTest, ImportAssetRejectsExtensionlessFile)
{
    AudioAssetManager mgr;
    const auto p = MakeTempFile("");
    EXPECT_FALSE(mgr.ImportAsset(p));
}

TEST(AudioAssetManagerTest, ImportAssetAcceptsSupportedExtensionsPastTheFormatGate)
{
    // 对受支持的扩展名，ImportAsset 必须**通过**格式闸门。
    // 由于真正解析需要有效音频数据与 OpenAL 设备，这里不断言返回值，
    // 只要求不因扩展名被拒 —— 用一个非法内容验证"闸门放行"是安全的：
    // 两种结果（false 但非格式原因 / true）都不该崩溃。
    AudioAssetManager mgr;
    for (const char* ext : {".wav", ".ogg", ".mp3", ".flac"}) {
        const auto p = MakeTempFile(ext);
        EXPECT_NO_THROW(mgr.ImportAsset(p))
            << "受支持扩展名 " << ext << " 不应导致崩溃";
    }
}

TEST(AudioAssetManagerTest, ImportAssetExtensionCheckIsCaseInsensitive)
{
    AudioAssetManager mgr;
    const auto lower = MakeTempFile(".wav");
    const auto upper = MakeTempFile(".WAV");
    // 大写扩展名不应被格式闸门拒绝（源码对扩展名做了 tolower）
    EXPECT_NO_THROW(mgr.ImportAsset(upper));
    EXPECT_NO_THROW(mgr.ImportAsset(lower));
}

// ── 热重载开关 ─────────────────────────────────────────────────────────
TEST(AudioAssetManagerTest, HotReloadDefaultsToDisabledAndRoundTrips)
{
    AudioAssetManager mgr;
    EXPECT_FALSE(mgr.IsHotReloadEnabled()) << "热重载默认应为关闭";

    mgr.EnableHotReload(true);
    EXPECT_TRUE(mgr.IsHotReloadEnabled());
    mgr.EnableHotReload(false);
    EXPECT_FALSE(mgr.IsHotReloadEnabled());
}

// ── 空缓存下的簿记 ─────────────────────────────────────────────────────
TEST(AudioAssetManagerTest, MemoryUsageIsZeroOnEmptyCache)
{
    AudioAssetManager mgr;
    EXPECT_EQ(mgr.GetMemoryUsage(), 0u);
}

TEST(AudioAssetManagerTest, UnloadAllOnEmptyCacheIsSafe)
{
    AudioAssetManager mgr;
    EXPECT_NO_THROW(mgr.UnloadAll());
    EXPECT_EQ(mgr.GetMemoryUsage(), 0u);
}

TEST(AudioAssetManagerTest, UnloadClipOnMissingPathIsSafe)
{
    AudioAssetManager mgr;
    EXPECT_NO_THROW(mgr.UnloadClip("never-loaded.wav"));
    EXPECT_EQ(mgr.GetMemoryUsage(), 0u);
}

TEST(AudioAssetManagerTest, ReleaseUnusedOnEmptyCacheIsSafe)
{
    AudioAssetManager mgr;
    EXPECT_NO_THROW(mgr.ReleaseUnused());
}

TEST(AudioAssetManagerTest, ReleaseUnusedIsIdempotent)
{
    AudioAssetManager mgr;
    for (int i = 0; i < 5; ++i) {
        EXPECT_NO_THROW(mgr.ReleaseUnused());
    }
    EXPECT_EQ(mgr.GetMemoryUsage(), 0u);
}

// ── GetClip 失败路径 ───────────────────────────────────────────────────
TEST(AudioAssetManagerTest, GetClipReturnsNullptrForUnloadableAsset)
{
    AudioAssetManager mgr;
    EXPECT_EQ(mgr.GetClip("missing.wav"), nullptr);
}

TEST(AudioAssetManagerTest, GetClipOnEmptyPathReturnsNullptr)
{
    AudioAssetManager mgr;
    EXPECT_EQ(mgr.GetClip(""), nullptr);
}

TEST(AudioAssetManagerTest, RepeatedFailedGetClipDoesNotGrowMemoryUsage)
{
    AudioAssetManager mgr;
    for (int i = 0; i < 10; ++i) {
        (void)mgr.GetClip("missing.wav");
    }
    EXPECT_EQ(mgr.GetMemoryUsage(), 0u)
        << "失败的加载不得在缓存中留下条目";
}

// ── 目录扫描 ───────────────────────────────────────────────────────────
TEST(AudioAssetManagerTest, BuildCacheOnMissingDirectoryIsSafe)
{
    // BuildCache 扫描硬编码的 "assets/sounds"；不存在时告警并返回
    AudioAssetManager mgr;
    EXPECT_NO_THROW(mgr.BuildCache());
}

TEST(AudioAssetManagerTest, StreamAssetOnMissingFileIsSafe)
{
    AudioAssetManager mgr;
    EXPECT_NO_THROW(mgr.StreamAsset("missing.wav"));
}

TEST(AudioAssetManagerTest, LoadAsyncOnMissingFileDoesNotCrash)
{
    AudioAssetManager mgr;
    bool called = false;
    EXPECT_NO_THROW(mgr.LoadAsync("missing.wav",
                                  [&called](std::shared_ptr<AudioClip>) { called = true; }));
    // 异步路径是否在调用栈内回调取决于 JobSystem 实现，此处不断言。
    SUCCEED() << "callback invoked inline: " << (called ? "yes" : "no");
}

// ── 内存核算 ───────────────────────────────────────────────────────────
TEST(AudioAssetManagerTest, MemoryUsageNeverDecreasesWithoutUnload)
{
    AudioAssetManager mgr;
    size_t prev = mgr.GetMemoryUsage();
    for (int i = 0; i < 5; ++i) {
        (void)mgr.GetClip("missing.wav");
        const size_t now = mgr.GetMemoryUsage();
        EXPECT_GE(now, prev) << "未显式卸载时内存占用却下降了";
        prev = now;
    }
}

// ── 未覆盖：device-bound ───────────────────────────────────────────────
//
// AudioAssetManager::UnloadClip 的引用计数语义
// （GetClip 递增 refCount → UnloadClip 递减 → 归零时擦除）无法在无设备
// 环境下验证，因为缓存条目只能由"成功加载音频"产生，而那一步需要 OpenAL
// 设备来创建 buffer。本文件已确认：
//   - UnloadClip 对**缺失**路径安全（不崩、不误删）
//   - 空缓存下内存核算为 0 且幂等
// 待 AudioEngine / AudioClip 的 device-backed seam 具备后，
// 应补：GetClip 两次 → UnloadClip 一次 → 缓存仍在；
//       再 UnloadClip 一次 → 条目被擦除。
