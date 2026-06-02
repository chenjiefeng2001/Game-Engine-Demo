#pragma once

/**
 * @file AnimationManager.h
 * @brief 动画管理器 — 选择性加载/卸载、串流动画、游戏作者 API
 *
 * ════════════════════════════════════════════
 * 功能概览
 * ════════════════════════════════════════════
 *
 * 1. 选择性加载 (Selective Loading)
 *    - 注册动画资源但不立即加载
 *    - 按名称按需加载/卸载单个动画
 *    - 引用计数自动管理
 *    - 支持异步加载回调
 *
 * 2. 串流动画 (Streaming Animation)
 *    - 将大型动画分块流式加载
 *    - 预取策略：提前加载即将播放的帧
 *    - 适用于过场动画、动作捕捉数据
 *
 * 3. 游戏作者 API
 *    - AnimationManager::Get() 单例
 *    - 简单的 LoadClip / UnloadClip / AcquireClip
 *    - 资源状态查询
 *    - 内存压力管理
 *
 * ════════════════════════════════════════════
 * 使用示例
 * ════════════════════════════════════════════
 *
 *   // 初始化
 *   AnimationManager::Get().Init();
 *
 *   // 注册资源（不加载）
 *   AnimationManager::Get().RegisterClip("walk", "animations/walk.anim");
 *   AnimationManager::Get().RegisterClip("run",  "animations/run.anim");
 *
 *   // 需要时按需加载
 *   AnimationManager::Get().LoadClip("walk");
 *   auto walkClip = AnimationManager::Get().AcquireClip("walk");
 *   walkClip->Play();
 *
 *   // 不再需要时释放
 *   AnimationManager::Get().ReleaseClip("walk");
 *   // 引用计数为 0 时自动卸载
 *
 *   // 串流动画
 *   auto stream = AnimationManager::Get().OpenStream("cinematic.anim");
 *   stream->SetPrefetchLength(2.0f);
 *   while (playing) {
 *       stream->Update(dt);
 *       stream->SampleAt(time, skeleton, poseEval);
 *   }
 *
 *   // 获取统计
 *   auto stats = AnimationManager::Get().GetStats();
 *   Log::Info("Loaded: {}/{} clips, {} MB", stats.currentlyLoaded,
 *             stats.totalRegistered, stats.totalMemoryBytes / 1048576);
 */

#include "Engine/Animation/AnimationLocalTimeline.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Animation/AnimationPose.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>

namespace Engine {

    // ============================================================
    // 串流动画 — 分块加载大型动画数据
    // ============================================================
    class StreamingAnimation {
    public:
        StreamingAnimation() = default;
        ~StreamingAnimation() { Close(); }

        // 禁止拷贝
        StreamingAnimation(const StreamingAnimation&) = delete;
        StreamingAnimation& operator=(const StreamingAnimation&) = delete;

        // ── 生命周期 ──

        /**
         * @brief 打开一个串流动画文件
         * @param filePath   文件路径
         * @param sampleRate 目标采样率（帧/秒）
         * @return 是否成功打开
         *
         * 打开后不会立即加载所有数据，而是按需流式读取。
         */
        bool Open(const std::string& filePath, float32 sampleRate = 30.0f);

        /** 关闭串流动画 */
        void Close();

        /** 是否已打开且有效 */
        bool IsValid() const noexcept { return m_IsOpen; }

        // ── 流控制 ──

        /**
         * @brief 设置预取时长（秒）
         * @param seconds 预取当前播放位置之后多少秒的数据
         *
         * 例如设为 2.0f 时，会提前加载当前时间之后 2 秒内的帧。
         */
        void SetPrefetchLength(float32 seconds) noexcept { m_PrefetchLength = seconds; }
        float32 GetPrefetchLength() const noexcept { return m_PrefetchLength; }

        /**
         * @brief 设置预取帧数（替代 SetPrefetchLength）
         */
        void SetPrefetchFrames(int32 frames) noexcept { m_PrefetchFrames = frames; }
        int32 GetPrefetchFrames() const noexcept { return m_PrefetchFrames; }

        /**
         * @brief 每帧调用，推进流并加载/卸载数据块
         * @param dt 时间步长
         *
         * Update 后可通过 SampleAt 获取当前姿势。
         */
        void Update(float32 dt);

        /**
         * @brief 获取串流进度
         * @return [0, 1] 范围的进度值
         */
        float32 GetProgress() const noexcept;

        /** 获取动画总时长 */
        float32 GetDuration() const noexcept { return m_Duration; }

        /** 获取已加载的帧数 */
        int32 GetLoadedFrames() const noexcept { return m_LoadedFrames; }

        /** 获取总帧数 */
        int32 GetTotalFrames() const noexcept { return m_TotalFrames; }

        /** 是否正在流式加载中 */
        bool IsStreaming() const noexcept { return m_IsOpen && m_LoadedFrames < m_TotalFrames; }

        // ── 姿势采样 ──

        /**
         * @brief 在指定时间点采样动画姿势
         * @param time       时间点（秒）
         * @param outSkeleton 输出骨骼
         * @param poseEval   姿势求值器
         * @return 该时间点的数据是否已加载
         *
         * 如果数据尚未加载，会返回 false 并使用最近可用帧代替。
         */
        bool SampleAt(float32 time, Skeleton& outSkeleton,
                      AnimationPose& poseEval) const;

        /**
         * @brief 将整个串流动画加载为完整的时间线
         * @return 完整动画（如果动画较小可以全部加载）
         *
         * 会阻塞直到全部加载完成。
         */
        std::shared_ptr<AnimationLocalTimeline> LoadFullClip();

    private:
        // ── 流式数据块 ──
        struct StreamChunk {
            float32 startTime = 0.0f;
            float32 endTime   = 0.0f;
            std::vector<uint8_t> data;  // 序列化的关键帧数据
            int32 refCount = 0;
        };

        // 内部：加载指定时间段的数据块
        bool LoadChunk(float32 startTime, float32 endTime);
        // 内部：卸载最久未使用的数据块
        void EvictChunk();

        std::string m_FilePath;
        bool        m_IsOpen      = false;
        float32     m_Duration    = 0.0f;
        int32       m_TotalFrames = 0;
        int32       m_LoadedFrames = 0;
        float32     m_SampleRate  = 30.0f;
        float32     m_CurrentTime = 0.0f;

        // 预取配置
        float32     m_PrefetchLength = 2.0f;
        int32       m_PrefetchFrames = 60;
        int32       m_MaxChunks     = 16;

        // 数据块缓存（环状/最近最少使用）
        std::vector<StreamChunk> m_Chunks;
    };

    // ============================================================
    // 动画管理器（单例）
    // ============================================================
    class AnimationManager {
    public:
        /** 获取全局单例 */
        static AnimationManager& Get();

        // ── 生命周期 ──
        void Init();
        void Shutdown();
        bool IsInitialized() const noexcept { return m_Initialized; }

        // ── 资源注册 ──

        /**
         * @brief 注册一个动画片段
         * @param name 动画名称（用于后续查找）
         * @param clip 动画时间线
         *
         * 注册后不会立即加载，直到首次调用 LoadClip 或 AcquireClip。
         * 如果已存在同名资源，会被替换。
         */
        void RegisterClip(const std::string& name,
                          std::shared_ptr<AnimationLocalTimeline> clip);

        /**
         * @brief 从文件注册动画
         * @param name     动画名称
         * @param filePath 文件路径
         *
         * 文件格式由 AnimationSerializer 定义。
         * 仅记录路径，不立即加载。
         */
        void RegisterFromFile(const std::string& name, const std::string& filePath);

        /** 取消注册 */
        void UnregisterClip(const std::string& name);

        /** 清空所有注册 */
        void Clear();

        // ── 选择性加载 / 卸载 ──

        /**
         * @brief 加载指定动画
         * @param name 动画名称
         * @return 是否成功
         *
         * 如果动画已加载，重复调用无副作用。
         * 如果动画尚未注册，返回 false。
         */
        bool LoadClip(const std::string& name);

        /**
         * @brief 卸载指定动画
         * @param name 动画名称
         * @return 是否成功
         *
         * 如果有活跃使用者（AcquireClip 未 Release），
         * 引用计数会阻止实际卸载。
         * 设置 force=true 可强制卸载。
         */
        bool UnloadClip(const std::string& name, bool force = false);

        /** 加载所有已注册的动画 */
        void LoadAll();

        /** 卸载所有已加载的动画 */
        void UnloadAll();

        /** 垃圾回收：卸载引用计数为 0 且不再使用的动画 */
        void GarbageCollect();

        // ── 运行时访问（游戏作者 API） ──

        /**
         * @brief 获取动画片段（增加引用计数）
         * @param name 动画名称
         * @return 动画时间线指针，若未找到或未加载则返回 nullptr
         *
         * 每次 AcquireClip 应配对一次 ReleaseClip。
         * 如果动画尚未加载，会自动加载。
         */
        std::shared_ptr<AnimationLocalTimeline> AcquireClip(const std::string& name);

        /**
         * @brief 释放动画片段（减少引用计数）
         * @param name 动画名称
         *
         * 当引用计数降为 0 时，动画可能被垃圾回收卸载。
         */
        void ReleaseClip(const std::string& name);

        // ── 异步加载（非阻塞） ──

        /**
         * @brief 异步加载动画
         * @param name     动画名称
         * @param callback 完成回调 (bool success)
         *
         * 加载完成后调用 callback。
         * 若动画已在加载中，后续请求排队。
         */
        void LoadClipAsync(const std::string& name,
                           std::function<void(bool)> callback = nullptr);

        /** 检查动画是否正在异步加载中 */
        bool IsLoading(const std::string& name) const;

        // ── 串流动画 ──

        /**
         * @brief 打开一个串流动画
         * @param filePath   文件路径
         * @param sampleRate 采样率
         * @return 串流动画句柄
         */
        std::shared_ptr<StreamingAnimation> OpenStream(const std::string& filePath,
                                                        float32 sampleRate = 30.0f);

        // ── 状态查询 ──

        /** 检查动画是否已加载到内存 */
        bool IsLoaded(const std::string& name) const;

        /** 检查动画是否已注册 */
        bool IsRegistered(const std::string& name) const;

        // ── 配置 ──

        /** 设置最大同时加载的动画数量（超过时触发 GC） */
        void SetMaxLoadedClips(int32 max) noexcept { m_MaxLoadedClips = max; }
        int32 GetMaxLoadedClips() const noexcept { return m_MaxLoadedClips; }

        /** 设置内存预算（字节），超过时自动卸载最近未使用的动画 */
        void SetMemoryBudget(size_t bytes) noexcept { m_MemoryBudget = bytes; }
        size_t GetMemoryBudget() const noexcept { return m_MemoryBudget; }

        // ── 统计 ──

        struct Stats {
            int32  totalRegistered  = 0;    ///< 注册总数
            int32  currentlyLoaded  = 0;    ///< 当前加载数
            int32  activeUsers      = 0;    ///< 活跃使用者数
            size_t totalMemoryBytes = 0;    ///< 占用内存（字节）
            int32  streamingCount   = 0;    ///< 串流动画数
        };

        Stats GetStats() const;

    private:
        AnimationManager() = default;
        ~AnimationManager() { Shutdown(); }
        AnimationManager(const AnimationManager&) = delete;
        AnimationManager& operator=(const AnimationManager&) = delete;

        // ── 内部资源条目 ──
        struct ClipEntry {
            std::string name;
            std::string filePath;                          // 文件路径（可选）
            std::shared_ptr<AnimationLocalTimeline> clip;   // 加载后的数据
            int32   refCount     = 0;                        // 引用计数
            bool    loaded       = false;
            bool    loading      = false;                    // 异步加载中
        };

        ClipEntry* FindEntry(const std::string& name);
        const ClipEntry* FindEntry(const std::string& name) const;

        bool        m_Initialized    = false;
        int32       m_MaxLoadedClips = 64;
        size_t      m_MemoryBudget   = 512 * 1024 * 1024;  // 512 MB

        std::unordered_map<std::string, ClipEntry> m_Clips;
        std::vector<std::shared_ptr<StreamingAnimation>> m_Streams;

        // 异步加载队列
        struct AsyncRequest {
            std::string name;
            std::function<void(bool)> callback;
        };
        std::vector<AsyncRequest> m_AsyncQueue;
    };

} // namespace Engine
