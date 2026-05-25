#pragma once

/**
 * @file FileWatcher.h
 * @brief 文件变更监视器 — 监听磁盘文件修改，触发资源热加载
 *
 * 在后台线程中轮询缓存资源的文件修改时间（mtime），
 * 当检测到变更时将路径加入待处理队列，由主线程
 * 在下一帧通过 ResourceManager::PollHotReload() 处理。
 *
 * 设计原则：
 *   - 平台无关（使用 stat() 轮询，而非平台特定 API）
 *   - 线程安全：后台线程只写队列，主线程消费队列
 *   - 低开销：默认 1 秒轮询间隔，仅检查已加载的资源文件
 *
 * 使用方式（由 Application 初始化）：
 * @code
 *   FileWatcher::Init();
 *   FileWatcher::Get()->Start();
 *   ...
 *   // 每帧调用
 *   FileWatcher::Get()->PollPendingChanges();
 * @endcode
 */

#include "Engine/Types.h"
#include <string>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <functional>

namespace Engine {

    class FileWatcher {
    public:
        using ChangeCallback = std::function<void(const std::string& filePath)>;

        // ── 单例生命周期 ──
        static void Init();
        static void Shutdown();
        static FileWatcher* Get() { return s_Instance; }

        // ── 控制 ──
        /** 启动后台监视线程 */
        void Start();
        /** 停止后台监视线程 */
        void Stop();
        /** 是否正在监视 */
        bool IsRunning() const noexcept { return m_Running; }

        // ── 配置 ──
        /** 设置轮询间隔（毫秒，默认 1000ms） */
        void SetPollInterval(uint32 ms) { m_PollIntervalMs = ms; }
        /** 获取轮询间隔 */
        uint32 GetPollInterval() const noexcept { return m_PollIntervalMs; }

        // ── 路径管理 ──
        /**
         * @brief 将路径加入监视列表
         * @param path 资源文件路径
         *
         * 每次资源成功加载后应调用此方法。
         * 内部自动创建父目录监视（若尚未监视）。
         */
        void Watch(const std::string& path);

        /**
         * @brief 将路径从监视列表移除
         * @param path 资源文件路径
         */
        void Unwatch(const std::string& path);

        /**
         * @brief 消费待处理变更——在主线程每帧调用
         * @param callback 每检测到一个文件变更，调用此回调
         *
         * ResourceManager 在 PollHotReload() 中使用此方法。
         */
        void PollPendingChanges(ChangeCallback callback);

        /** 清空所有监视路径 */
        void Clear();

        ~FileWatcher();

    private:
        FileWatcher() = default;
        FileWatcher(const FileWatcher&) = delete;
        FileWatcher& operator=(const FileWatcher&) = delete;

        /** 后台线程主循环 */
        void ThreadLoop();

        static FileWatcher* s_Instance;
        static std::unique_ptr<FileWatcher> s_InstanceOwner;

        // ── 线程控制 ──
        std::thread              m_WatcherThread;
        std::atomic<bool>        m_Running{ false };
        std::atomic<bool>        m_StopRequested{ false };

        // ── 配置 ──
        uint32 m_PollIntervalMs = 1000;

        // ── 监视文件状态（线程安全：仅由后台线程读/写） ──
        // path → last modification time (time_t)
        std::unordered_map<std::string, int64> m_WatchedFiles;

        // ── 变更队列（线程间通信） ──
        std::mutex               m_PendingMutex;
        std::vector<std::string> m_PendingChanges;

        // ── 目录监视（用于自动添加新文件，当前简化：只监视明确注册的文件） ──
        // 后续可扩展为监视整个 resource 目录
    };

} // namespace Engine