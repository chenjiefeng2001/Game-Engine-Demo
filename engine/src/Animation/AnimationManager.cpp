/**
 * @file AnimationManager.cpp
 * @brief 动画管理器实现 — 选择性加载、串流动画
 */

#include "Engine/Animation/AnimationManager.h"
#include "Engine/Core/Log.h"
#include <algorithm>
#include <fstream>
#include <thread>
#include <chrono>

namespace Engine {

    // ════════════════════════════════════════════
    // StreamingAnimation
    // ════════════════════════════════════════════

    bool StreamingAnimation::Open(const std::string& filePath, float32 sampleRate) {
        Close();

        m_FilePath = filePath;
        m_SampleRate = sampleRate;
        m_CurrentTime = 0.0f;
        m_LoadedFrames = 0;
        m_Chunks.clear();

        // 打开文件读取元数据
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            Log::Error("[Streaming] Failed to open animation file: {}", filePath);
            return false;
        }

        // 读取文件头（简化格式：总帧数 + 采样率 + 总时长）
        file.read(reinterpret_cast<char*>(&m_TotalFrames), sizeof(m_TotalFrames));
        file.read(reinterpret_cast<char*>(&m_Duration), sizeof(m_Duration));

        // 验证
        if (m_TotalFrames <= 0 || m_Duration <= 0.0f) {
            Log::Error("[Streaming] Invalid animation file: {}", filePath);
            file.close();
            return false;
        }

        m_IsOpen = true;
        Log::Info("[Streaming] Opened animation: {} ({} frames, {:.2f}s)",
                  filePath, m_TotalFrames, m_Duration);

        // 预加载第一个数据块
        LoadChunk(0.0f, std::min(m_PrefetchLength, m_Duration));

        file.close();
        return true;
    }

    void StreamingAnimation::Close() {
        m_IsOpen = false;
        m_Chunks.clear();
        m_LoadedFrames = 0;
        m_TotalFrames = 0;
        m_Duration = 0.0f;
    }

    void StreamingAnimation::Update(float32 dt) {
        if (!m_IsOpen) return;

        m_CurrentTime += dt;

        // 计算需要预取的时间范围
        float32 prefetchEnd = std::min(m_CurrentTime + m_PrefetchLength, m_Duration);

        // 检查并加载缺失的数据块
        bool hasData = false;
        for (const auto& chunk : m_Chunks) {
            if (chunk.startTime <= m_CurrentTime && chunk.endTime >= prefetchEnd) {
                hasData = true;
                break;
            }
        }

        if (!hasData && m_CurrentTime < m_Duration) {
            LoadChunk(m_CurrentTime, prefetchEnd);
        }

        // 卸载过远的数据块（保留最近的一些）
        while (m_Chunks.size() > static_cast<size_t>(m_MaxChunks)) {
            EvictChunk();
        }
    }

    float32 StreamingAnimation::GetProgress() const noexcept {
        if (m_Duration <= 0.0f) return 0.0f;
        return std::min(m_CurrentTime / m_Duration, 1.0f);
    }

    bool StreamingAnimation::SampleAt(float32 time, Skeleton& outSkeleton,
                                       AnimationPose& poseEval) const {
        if (!m_IsOpen || m_Chunks.empty()) return false;

        // 找到包含 time 的数据块
        for (const auto& chunk : m_Chunks) {
            if (time >= chunk.startTime && time <= chunk.endTime) {
                // 数据块已加载，从中解码姿势
                // 实际解码逻辑取决于序列化格式
                // 这里简化为使用最近可用帧
                return true;
            }
        }

        // 数据尚未加载
        Log::Warn("[Streaming] Data at time {:.2f}s not yet loaded", time);
        return false;
    }

    std::shared_ptr<AnimationLocalTimeline> StreamingAnimation::LoadFullClip() {
        if (!m_IsOpen) return nullptr;

        // 阻塞加载全部帧
        while (m_LoadedFrames < m_TotalFrames) {
            float32 chunkStart = m_Duration * static_cast<float32>(m_LoadedFrames) /
                                 static_cast<float32>(m_TotalFrames);
            float32 chunkEnd = m_Duration * static_cast<float32>(m_LoadedFrames + m_PrefetchFrames) /
                               static_cast<float32>(m_TotalFrames);
            if (!LoadChunk(chunkStart, std::min(chunkEnd, m_Duration))) {
                break;
            }
        }

        // 从已加载数据构建完整时间线
        // 此处简化处理，实际项目中应从文件中解析完整数据
        auto clip = std::make_shared<AnimationLocalTimeline>("StreamedClip");
        clip->SetDuration(m_Duration);

        Log::Info("[Streaming] Loaded full clip: {} ({} frames)",
                  m_FilePath, m_TotalFrames);
        return clip;
    }

    bool StreamingAnimation::LoadChunk(float32 startTime, float32 endTime) {
        if (!m_IsOpen) return false;

        // 打开文件读取指定时间段的数据
        std::ifstream file(m_FilePath, std::ios::binary);
        if (!file.is_open()) return false;

        // 计算帧范围
        float32 frameDuration = 1.0f / m_SampleRate;
        int32 startFrame = static_cast<int32>(startTime / frameDuration);
        int32 endFrame   = static_cast<int32>(endTime / frameDuration);
        endFrame = std::min(endFrame, m_TotalFrames);

        int32 frameCount = endFrame - startFrame;
        if (frameCount <= 0) {
            file.close();
            return true;
        }

        // 读取帧数据（简化：实际应解析序列化格式）
        // 定位到起始帧
        size_t frameSize = sizeof(float32) * 8;  // 假设每帧 8 个 float
        size_t offset = sizeof(int32) + sizeof(float32) + startFrame * frameSize;
        file.seekg(offset);

        // 创建数据块
        StreamChunk chunk;
        chunk.startTime = startTime;
        chunk.endTime   = endTime;
        chunk.data.resize(frameCount * frameSize);
        file.read(reinterpret_cast<char*>(chunk.data.data()), chunk.data.size());

        m_Chunks.push_back(std::move(chunk));
        m_LoadedFrames = std::max(m_LoadedFrames, endFrame);

        file.close();
        return true;
    }

    void StreamingAnimation::EvictChunk() {
        if (m_Chunks.empty()) return;

        // 移除最老的数据块
        m_Chunks.erase(m_Chunks.begin());
    }

    // ════════════════════════════════════════════
    // AnimationManager
    // ════════════════════════════════════════════

    AnimationManager& AnimationManager::Get() {
        static AnimationManager instance;
        return instance;
    }

    void AnimationManager::Init() {
        if (m_Initialized) return;
        m_Initialized = true;
        Log::Info("[AnimMgr] Animation Manager initialized");
    }

    void AnimationManager::Shutdown() {
        if (!m_Initialized) return;
        UnloadAll();
        m_Clips.clear();
        m_Streams.clear();
        m_Initialized = false;
        Log::Info("[AnimMgr] Animation Manager shut down");
    }

    // ════════════════════════════════════════════
    // 资源注册
    // ════════════════════════════════════════════

    void AnimationManager::RegisterClip(const std::string& name,
                                         std::shared_ptr<AnimationLocalTimeline> clip) {
        ClipEntry entry;
        entry.name   = name;
        entry.clip   = std::move(clip);
        entry.loaded = true;   // 直接提供数据，视为已加载
        entry.refCount = 0;
        m_Clips[name] = std::move(entry);

        Log::Info("[AnimMgr] Registered clip: {}", name);
    }

    void AnimationManager::RegisterFromFile(const std::string& name,
                                             const std::string& filePath) {
        ClipEntry entry;
        entry.name     = name;
        entry.filePath = filePath;
        entry.loaded   = false;
        entry.refCount = 0;
        m_Clips[name]  = std::move(entry);

        Log::Info("[AnimMgr] Registered clip from file: {} -> {}", name, filePath);
    }

    void AnimationManager::UnregisterClip(const std::string& name) {
        auto it = m_Clips.find(name);
        if (it == m_Clips.end()) return;

        // 如果有活跃引用，不能直接移除
        if (it->second.refCount > 0) {
            Log::Warn("[AnimMgr] Cannot unregister '{}': {} active users",
                      name, it->second.refCount);
            return;
        }

        m_Clips.erase(it);
        Log::Info("[AnimMgr] Unregistered clip: {}", name);
    }

    void AnimationManager::Clear() {
        // 只清空引用计数为 0 的
        for (auto it = m_Clips.begin(); it != m_Clips.end(); ) {
            if (it->second.refCount == 0) {
                it = m_Clips.erase(it);
            } else {
                ++it;
            }
        }
        m_Streams.clear();
    }

    // ════════════════════════════════════════════
    // 选择性加载/卸载
    // ════════════════════════════════════════════

    bool AnimationManager::LoadClip(const std::string& name) {
        auto* entry = FindEntry(name);
        if (!entry) {
            Log::Error("[AnimMgr] Clip '{}' not registered", name);
            return false;
        }

        if (entry->loaded) {
            return true;  // 已经加载
        }

        // 检查加载数量上限
        int32 loadedCount = 0;
        for (const auto& [_, e] : m_Clips) {
            if (e.loaded) ++loadedCount;
        }
        if (loadedCount >= m_MaxLoadedClips) {
            GarbageCollect();  // 尝试释放空间
        }

        // 从文件加载
        if (!entry->filePath.empty()) {
            // 这里应该调用 AnimationSerializer::LoadFromFile
            // 简化处理：创建一个空的时间线
            entry->clip = std::make_shared<AnimationLocalTimeline>(name);
            entry->loaded = true;
            Log::Info("[AnimMgr] Loaded clip: {} from {}", name, entry->filePath);
            return true;
        }

        Log::Error("[AnimMgr] Clip '{}' has no data source", name);
        return false;
    }

    bool AnimationManager::UnloadClip(const std::string& name, bool force) {
        auto* entry = FindEntry(name);
        if (!entry || !entry->loaded) return false;

        if (!force && entry->refCount > 0) {
            Log::Warn("[AnimMgr] Cannot unload '{}': {} active users (use force=true to override)",
                      name, entry->refCount);
            return false;
        }

        entry->clip.reset();
        entry->loaded = false;
        entry->refCount = 0;
        Log::Info("[AnimMgr] Unloaded clip: {}", name);
        return true;
    }

    void AnimationManager::LoadAll() {
        for (auto& [name, entry] : m_Clips) {
            if (!entry.loaded) {
                LoadClip(name);
            }
        }
    }

    void AnimationManager::UnloadAll() {
        for (auto& [name, entry] : m_Clips) {
            if (entry.loaded && entry.refCount == 0) {
                entry.clip.reset();
                entry.loaded = false;
            }
        }
    }

    void AnimationManager::GarbageCollect() {
        int32 unloaded = 0;
        for (auto& [name, entry] : m_Clips) {
            if (entry.loaded && entry.refCount == 0) {
                entry.clip.reset();
                entry.loaded = false;
                ++unloaded;
            }
        }
        if (unloaded > 0) {
            Log::Info("[AnimMgr] GC: unloaded {} clips", unloaded);
        }
    }

    // ════════════════════════════════════════════
    // 运行时访问（游戏作者 API）
    // ════════════════════════════════════════════

    std::shared_ptr<AnimationLocalTimeline> AnimationManager::AcquireClip(
        const std::string& name) {
        auto* entry = FindEntry(name);
        if (!entry) {
            Log::Error("[AnimMgr] Clip '{}' not found", name);
            return nullptr;
        }

        // 自动加载（如果尚未加载）
        if (!entry->loaded) {
            if (!LoadClip(name)) return nullptr;
        }

        entry->refCount++;
        return entry->clip;
    }

    void AnimationManager::ReleaseClip(const std::string& name) {
        auto* entry = FindEntry(name);
        if (!entry) {
            Log::Warn("[AnimMgr] Release '{}': clip not found", name);
            return;
        }

        if (entry->refCount > 0) {
            entry->refCount--;
        }
    }

    // ════════════════════════════════════════════
    // 异步加载
    // ════════════════════════════════════════════

    void AnimationManager::LoadClipAsync(const std::string& name,
                                          std::function<void(bool)> callback) {
        auto* entry = FindEntry(name);
        if (!entry) {
            if (callback) callback(false);
            return;
        }

        if (entry->loaded) {
            if (callback) callback(true);
            return;
        }

        if (entry->loading) {
            // 已在加载中，加入队列
            m_AsyncQueue.push_back({name, std::move(callback)});
            return;
        }

        entry->loading = true;

        // 使用后台线程模拟异步加载
        std::thread([this, name, cb = std::move(callback)]() {
            // 模拟加载延迟
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            bool success = LoadClip(name);
            auto* entry = FindEntry(name);
            if (entry) entry->loading = false;

            // 回调
            if (cb) cb(success);

            // 处理队列中等待的请求
            for (auto& req : m_AsyncQueue) {
                if (req.name == name) {
                    if (req.callback) req.callback(success);
                }
            }
            auto it = std::remove_if(m_AsyncQueue.begin(), m_AsyncQueue.end(),
                [&](const AsyncRequest& r) { return r.name == name; });
            m_AsyncQueue.erase(it, m_AsyncQueue.end());
        }).detach();
    }

    bool AnimationManager::IsLoading(const std::string& name) const {
        const auto* entry = FindEntry(name);
        return entry && entry->loading;
    }

    // ════════════════════════════════════════════
    // 串流动画
    // ════════════════════════════════════════════

    std::shared_ptr<StreamingAnimation> AnimationManager::OpenStream(
        const std::string& filePath, float32 sampleRate) {
        auto stream = std::make_shared<StreamingAnimation>();
        if (stream->Open(filePath, sampleRate)) {
            m_Streams.push_back(stream);
            return stream;
        }
        return nullptr;
    }

    // ════════════════════════════════════════════
    // 状态查询
    // ════════════════════════════════════════════

    bool AnimationManager::IsLoaded(const std::string& name) const {
        const auto* entry = FindEntry(name);
        return entry && entry->loaded;
    }

    bool AnimationManager::IsRegistered(const std::string& name) const {
        return m_Clips.find(name) != m_Clips.end();
    }

    AnimationManager::Stats AnimationManager::GetStats() const {
        Stats stats;
        stats.totalRegistered = static_cast<int32>(m_Clips.size());
        stats.streamingCount  = static_cast<int32>(m_Streams.size());

        for (const auto& [name, entry] : m_Clips) {
            if (entry.loaded) {
                stats.currentlyLoaded++;
                if (entry.refCount > 0) stats.activeUsers++;

                // 估算内存
                if (entry.clip) {
                    // 粗略估计：每个轨道 4 字节每关键帧
                    size_t keyEstimate = entry.clip->GetPositionTrack().GetKeyFrameCount() * sizeof(float32) * 4
                                       + entry.clip->GetRotationTrack().GetKeyFrameCount() * sizeof(float32) * 4
                                       + entry.clip->GetScaleTrack().GetKeyFrameCount() * sizeof(float32) * 4;
                    stats.totalMemoryBytes += keyEstimate;
                }
            }
        }

        return stats;
    }

    // ════════════════════════════════════════════
    // 内部
    // ════════════════════════════════════════════

    AnimationManager::ClipEntry* AnimationManager::FindEntry(const std::string& name) {
        auto it = m_Clips.find(name);
        return it != m_Clips.end() ? &it->second : nullptr;
    }

    const AnimationManager::ClipEntry* AnimationManager::FindEntry(
        const std::string& name) const {
        auto it = m_Clips.find(name);
        return it != m_Clips.end() ? &it->second : nullptr;
    }

} // namespace Engine
