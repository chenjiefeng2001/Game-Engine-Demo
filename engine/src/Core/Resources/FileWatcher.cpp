#include "Engine/Core/Resources/FileWatcher.h"
#include "Engine/Core/Log.h"
#include <sys/stat.h>
#include <algorithm>

namespace {
    Engine::Logger s_Log("FileWatcher");
}

namespace Engine {

    FileWatcher* FileWatcher::s_Instance = nullptr;
    std::unique_ptr<FileWatcher> FileWatcher::s_InstanceOwner = nullptr;

    // ============================================================
    // 单例生命周期
    // ============================================================

    void FileWatcher::Init()
    {
        if (s_Instance)
        {
            s_Log.Error("Already initialized");
            return;
        }
        s_InstanceOwner = std::unique_ptr<FileWatcher>(new FileWatcher());
        s_Instance = s_InstanceOwner.get();
        s_Log.Info("Initialized (poll interval: {}ms)", s_Instance->m_PollIntervalMs);
    }

    void FileWatcher::Shutdown()
    {
        if (!s_Instance) return;
        s_Log.Info("Shutting down...");
        s_Instance->Stop();
        s_Instance = nullptr;
        s_InstanceOwner.reset();
        s_Log.Info("Shutdown complete");
    }

    FileWatcher::~FileWatcher()
    {
        Stop();
    }

    // ============================================================
    // 控制
    // ============================================================

    void FileWatcher::Start()
    {
        if (m_Running.load()) return;
        if (m_WatchedFiles.empty())
        {
            s_Log.Info("No files to watch, skipping thread start");
            return;
        }

        m_StopRequested = false;
        m_Running = true;
        m_WatcherThread = std::thread(&FileWatcher::ThreadLoop, this);
        s_Log.Info("Watcher thread started ({} files, {}ms interval)", m_WatchedFiles.size(), m_PollIntervalMs);
    }

    void FileWatcher::Stop()
    {
        if (!m_Running.load()) return;
        m_StopRequested = true;
        m_Running = false;
        if (m_WatcherThread.joinable())
            m_WatcherThread.join();
        s_Log.Info("Watcher thread stopped");
    }

    // ============================================================
    // 路径管理
    // ============================================================

    void FileWatcher::Watch(const std::string& path)
    {
        if (path.empty()) return;

        // 检查文件是否存在并记录当前 mtime
        struct stat fileInfo;
        if (stat(path.c_str(), &fileInfo) != 0)
        {
            s_Log.Error("Cannot watch non-existent file: {}", path);
            return;
        }

        m_WatchedFiles[path] = static_cast<int64>(fileInfo.st_mtime);
        s_Log.Info("Watching: {}", path);
    }

    void FileWatcher::Unwatch(const std::string& path)
    {
        auto it = m_WatchedFiles.find(path);
        if (it != m_WatchedFiles.end())
        {
            m_WatchedFiles.erase(it);
            s_Log.Info("Unwatched: {}", path);
        }
    }

    void FileWatcher::Clear()
    {
        m_WatchedFiles.clear();
        std::lock_guard<std::mutex> lock(m_PendingMutex);
        m_PendingChanges.clear();
        s_Log.Info("All watches cleared");
    }

    // ============================================================
    // 消费待处理变更（主线程调用）
    // ============================================================

    void FileWatcher::PollPendingChanges(ChangeCallback callback)
    {
        if (!callback) return;

        std::vector<std::string> changes;
        {
            std::lock_guard<std::mutex> lock(m_PendingMutex);
            if (m_PendingChanges.empty()) return;
            changes.swap(m_PendingChanges);
        }

        for (const auto& path : changes)
        {
            callback(path);
        }
    }

    // ============================================================
    // 后台线程主循环
    // ============================================================

    void FileWatcher::ThreadLoop()
    {
        while (!m_StopRequested.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_PollIntervalMs));
            if (m_StopRequested.load()) break;

            std::vector<std::string> changed;

            for (auto& [path, lastMtime] : m_WatchedFiles)
            {
                struct stat fileInfo;
                if (stat(path.c_str(), &fileInfo) != 0)
                {
                    // 文件已删除，跳过
                    continue;
                }

                int64 currentMtime = static_cast<int64>(fileInfo.st_mtime);
                if (currentMtime != lastMtime)
                {
                    lastMtime = currentMtime;
                    changed.push_back(path);
                }
            }

            if (!changed.empty())
            {
                std::lock_guard<std::mutex> lock(m_PendingMutex);
                m_PendingChanges.insert(m_PendingChanges.end(),
                                        changed.begin(), changed.end());
            }
        }
    }

} // namespace Engine