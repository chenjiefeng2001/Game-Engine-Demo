#include "Engine/Audio/AudioEngine.h"
#include "Engine/Core/Audio/AudioClip.h"
#include "Engine/Core/Audio/AudioLoader.h"
#include "Engine/Core/Log.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {
    Engine::Logger s_Log("AudioAsset");
}

namespace Engine {
namespace Audio {

// ============================================================================
// 导入资产
// ============================================================================

bool AudioAssetManager::ImportAsset(const std::string& path)
{
    // 验证文件是否存在且格式受支持
    if (!std::filesystem::exists(path))
    {
        s_Log.Error("ImportAsset: File '{}' not found", path);
        return false;
    }

    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext != ".wav" && ext != ".ogg" && ext != ".mp3" && ext != ".flac")
    {
        s_Log.Error("ImportAsset: Unsupported format '{}'", ext);
        return false;
    }

    // 检查是否已缓存
    auto it = m_Cache.find(path);
    if (it != m_Cache.end())
    {
        s_Log.Warn("ImportAsset: '{}' already cached", path);
        return true;
    }

    // 加载并缓存
    auto clip = std::make_shared<AudioClip>();
    if (!clip->LoadFromFile(path))
    {
        s_Log.Error("ImportAsset: Failed to load '{}'", path);
        return false;
    }

    CachedEntry entry;
    entry.clip = clip;
    entry.lastAccessTime = GetCurrentTimeMs();
    entry.refCount = 1;
    entry.isStreaming = false;
    m_Cache[path] = entry;

    s_Log.Info("ImportAsset: Imported '{}' ({:.2f}s, {}Hz)",
        path, clip->GetDuration(), clip->GetSampleRate());
    return true;
}

// ============================================================================
// 构建缓存
// ============================================================================

void AudioAssetManager::BuildCache()
{
    // 扫描 assets/sounds 目录
    const std::string soundDir = "assets/sounds";
    if (!std::filesystem::exists(soundDir))
    {
        s_Log.Warn("BuildCache: Sound directory '{}' not found", soundDir);
        return;
    }

    size_t loaded = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(soundDir))
    {
        if (!entry.is_regular_file()) continue;

        std::string path = entry.path().string();
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".wav" || ext == ".ogg")
        {
            if (ImportAsset(path))
            {
                loaded++;
            }
        }
    }

    s_Log.Info("BuildCache: Preloaded {} audio assets from '{}'", loaded, soundDir);
}

// ============================================================================
// 流式加载
// ============================================================================

void AudioAssetManager::StreamAsset(const std::string& path)
{
    // 对于大文件，标记为流式并分块加载
    auto it = m_Cache.find(path);
    if (it != m_Cache.end())
    {
        it->second.isStreaming = true;
        s_Log.Info("StreamAsset: '{}' marked for streaming", path);
        return;
    }

    // 未缓存时先加载
    ImportAsset(path);

    it = m_Cache.find(path);
    if (it != m_Cache.end())
    {
        it->second.isStreaming = true;
    }
}

// ============================================================================
// 释放未使用的资源
// ============================================================================

void AudioAssetManager::ReleaseUnused()
{
    // 清理引用计数为 0 且超过 60 秒未访问的资源
    uint64_t currentTime = GetCurrentTimeMs();
    const uint64_t maxIdleTime = 60000; // 60 秒

    std::vector<std::string> toRemove;
    for (const auto& [path, entry] : m_Cache)
    {
        if (entry.refCount == 0 &&
            (currentTime - entry.lastAccessTime) > maxIdleTime)
        {
            toRemove.push_back(path);
        }
    }

    for (const auto& path : toRemove)
    {
        m_Cache.erase(path);
        s_Log.Trace("ReleaseUnused: Released '{}'", path);
    }

    if (!toRemove.empty())
    {
        s_Log.Info("ReleaseUnused: Released {} unused assets", toRemove.size());
    }
}

// ============================================================================
// 内存使用统计
// ============================================================================

size_t AudioAssetManager::GetMemoryUsage() const
{
    size_t total = 0;
    for (const auto& [path, entry] : m_Cache)
    {
        if (entry.clip)
        {
            total += entry.clip->EstimatedMemoryBytes();
        }
    }
    return total;
}

// ============================================================================
// 异步加载
// ============================================================================

void AudioAssetManager::LoadAsync(const std::string& path,
    std::function<void(std::shared_ptr<AudioClip>)> callback)
{
    // 如果在主线程有 JobSystem，可以提交任务
    // 这里使用简化版：同步加载然后在回调中返回
    auto clip = GetClip(path);
    if (callback)
    {
        callback(clip);
    }
}

// ============================================================================
// 资源生命周期管理
// ============================================================================

std::shared_ptr<AudioClip> AudioAssetManager::GetClip(const std::string& path)
{
    auto it = m_Cache.find(path);
    if (it != m_Cache.end())
    {
        it->second.lastAccessTime = GetCurrentTimeMs();
        it->second.refCount++;
        return it->second.clip;
    }

    // 未缓存时自动加载
    if (ImportAsset(path))
    {
        it = m_Cache.find(path);
        if (it != m_Cache.end())
        {
            return it->second.clip;
        }
    }

    return nullptr;
}

void AudioAssetManager::UnloadClip(const std::string& path)
{
    auto it = m_Cache.find(path);
    if (it == m_Cache.end()) return;

    if (it->second.refCount > 0)
    {
        it->second.refCount--;
    }

    if (it->second.refCount == 0)
    {
        m_Cache.erase(it);
        s_Log.Info("UnloadClip: Unloaded '{}'", path);
    }
}

void AudioAssetManager::UnloadAll()
{
    m_Cache.clear();
    s_Log.Info("UnloadAll: All audio assets unloaded");
}

// ============================================================================
// 热重载
// ============================================================================

void AudioAssetManager::EnableHotReload(bool enabled)
{
    m_HotReload = enabled;
    s_Log.Info("HotReload {}", enabled ? "enabled" : "disabled");
}

bool AudioAssetManager::IsHotReloadEnabled() const
{
    return m_HotReload;
}

// ============================================================================
// 内部辅助
// ============================================================================

uint64_t AudioAssetManager::GetCurrentTimeMs() const
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void AudioAssetManager::CheckHotReload()
{
    if (!m_HotReload) return;

    for (auto& [path, entry] : m_Cache)
    {
        // 检查文件修改时间
        std::error_code ec;
        auto fileTime = std::filesystem::last_write_time(path, ec);
        if (ec) continue;

        auto fileTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            fileTime.time_since_epoch()).count();

        // 简化检查：最后写入时间与缓存的加载时间比较
        // 如果文件更新了，重新加载
        if (fileTimeMs > entry.lastAccessTime && entry.clip)
        {
            s_Log.Info("HotReload: Reloading '{}'", path);
            auto newClip = std::make_shared<AudioClip>();
            if (newClip->LoadFromFile(path))
            {
                entry.clip = newClip;
                entry.lastAccessTime = GetCurrentTimeMs();
                s_Log.Info("HotReload: Successfully reloaded '{}'", path);
            }
        }
    }
}

// ============================================================================
// 静态工厂函数
// ============================================================================

static AudioAssetManager* g_AssetManager = nullptr;

AudioAssetManager& GetAudioAssetManager()
{
    if (!g_AssetManager)
    {
        g_AssetManager = new AudioAssetManager();
    }
    return *g_AssetManager;
}

} // namespace Audio
} // namespace Engine