#include "Engine/Core/Resources/FileWatcher.h"

#include <iostream>
#include <sys/stat.h>
#include <algorithm>

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
            std::cerr << "[FileWatcher] Already initialized" << std::endl;
            return;
        }
        s_InstanceOwner = std::unique_ptr<FileWatcher>(new FileWatcher());
        s_Instance = s_InstanceOwner.get();
        std::cout << "[FileWatcher] Initialized (poll interval: "
                  << s_Instance->m_PollIntervalMs << "ms)" << std::endl;
    }

    void FileWatcher::Shutdown()
    {
        if (!s_Instance) return;
        std::cout << "[FileWatcher] Shutting down..." << std::endl;
        s_Instance->Stop();
        s_Instance = nullptr;
        s_InstanceOwner.reset();
        std::cout << "[FileWatcher] Shutdown complete" << std::endl;
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
            std::cout << "[FileWatcher] No files to watch, skipping thread start" << std::endl;
            return;
        }

        m_StopRequested = false;
        m_Running = true;
        m_WatcherThread = std::thread(&FileWatcher::ThreadLoop, this);
        std::cout << "[FileWatcher] Watcher thread started (" << m_WatchedFiles.size()
                  << " files, " << m_PollIntervalMs << "ms interval)" << std::endl;
    }

    void FileWatcher::Stop()
    {
        if (!m_Running.load()) return;
        m_StopRequested = true;
        m_Running = false;
        if (m_WatcherThread.joinable())
            m_WatcherThread.join();
        std::cout << "[FileWatcher] Watcher thread stopped" << std::endl;
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
            std::cerr << "[FileWatcher] Cannot watch non-existent file: " << path << std::endl;
            return;
        }

        m_WatchedFiles[path] = static_cast<int64>(fileInfo.st_mtime);
        std::cout << "[FileWatcher] Watching: " << path << std::endl;
    }

    void FileWatcher::Unwatch(const std::string& path)
    {
        auto it = m_WatchedFiles.find(path);
        if (it != m_WatchedFiles.end())
        {
            m_WatchedFiles.erase(it);
            std::cout << "[FileWatcher] Unwatched: " << path << std::endl;
        }
    }

    void FileWatcher::Clear()
    {
        m_WatchedFiles.clear();
        std::lock_guard<std::mutex> lock(m_PendingMutex);
        m_PendingChanges.clear();
        std::cout << "[FileWatcher] All watches cleared" << std::endl;
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