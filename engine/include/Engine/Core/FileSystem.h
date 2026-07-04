#pragma once

/**
 * @file FileSystem.h
 * @brief 虚拟文件系统 (VFS) — 挂载点管理、同步/异步 I/O、主线程派发器
 *
 * v2 变更：
 *   - IMountBackend 架构：支持 OsDirectoryMount / PakArchiveMount 等后端
 *   - 异步 I/O 从 std::async 迁移到 JobSystem
 *   - 主线程派发器：SubmitToMainThread / PollMainThreadTasks
 *   - IFile 接口：统一的文件句柄抽象
 */

#include "Engine/Types.h"
#include "Engine/Core/IFile.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>
#include <mutex>
#include <queue>

namespace Engine {

    struct FileEntry {
        std::string path;
        std::string name;
        std::string extension;
        uint64      size = 0;
        bool        isDirectory = false;
        bool        isReadOnly  = false;
    };

    struct AsyncResult {
        bool     success = false;
        uint64   bytesTransferred = 0;
        std::vector<uint8> buffer;
        std::string errorMessage;
    };

    using FileCallback = std::function<void(const AsyncResult& result)>;

    // ═══════════════════════════════════════════════════════════
    // I/O 优先级
    // ═══════════════════════════════════════════════════════════
    enum class IOPriority : uint8 {
        High   = 0,   ///< 急加载（当前焦点资源）
        Normal = 1,   ///< 默认优先级
        Low    = 2,   ///< 后台预取 / 缓存
    };

    class FileSystem {
    public:
        // ============================================================
        // 生命周期
        // ============================================================

        static void Init(uint32 threadCount = 2);
        static void Shutdown();
        static bool IsInitialized() { return s_Initialized; }

        // ============================================================
        // VFS — 挂载后端
        // ============================================================

        /**
         * @brief 挂载一个文件系统后端
         *
         * @param name    挂载点名称（如 "assets"、"base"）
         * @param backend 后端实现（OsDirectoryMount / PakArchiveMount）
         *
         * 之后可通过 "name:subpath" 格式访问该后端中的文件。
         */
        static void Mount(std::string_view name, MountBackendPtr backend);

        /** 移除一个挂载点 */
        static void Unmount(std::string_view name);

        /** 打开文件（通过 VFS 路径解析） */
        static IFilePtr OpenFile(const std::string& path);

        /**
         * @brief 解析虚拟路径为真实绝对路径
         *
         * 解析规则：
         *   1. "mountName:path" → 从挂载点查找
         *   2. 绝对路径 → 直接返回
         *   3. 相对路径 → 依次尝试每个挂载点，返回第一个存在的
         *   4. 都不存在 → basePath + path
         */
        static std::string ResolvePath(const std::string& path);

        static void SetBasePath(const std::string& path);
        static const std::string& GetBasePath();

        // ── 路径工具 ──

        static std::string GetFileName(const std::string& path);
        static std::string GetStem(const std::string& path);
        static std::string GetExtension(const std::string& path);
        static std::string GetDirectory(const std::string& path);
        static std::string Combine(const std::string& a, const std::string& b);
        static std::string GetAbsolute(const std::string& path);
        static std::string Normalize(const std::string& path);
        static bool IsAbsolute(const std::string& path);
        static std::string GetAppDirectory();

        // ============================================================
        // 文件/目录查询（通过 VFS 后端）
        // ============================================================

        static bool Exists(const std::string& path);
        static bool IsDirectory(const std::string& path);
        static bool IsFile(const std::string& path);
        static uint64 GetFileSize(const std::string& path);
        static int64 GetLastWriteTime(const std::string& path);

        // ── 目录操作 ──

        static bool CreateDirectory(const std::string& path);
        static bool Remove(const std::string& path);
        static bool RemoveAll(const std::string& path);
        static bool Rename(const std::string& from, const std::string& to);
        static bool Copy(const std::string& from, const std::string& to);

        // ── 目录扫描 ──

        static std::vector<FileEntry> ScanDirectory(
            const std::string& dirPath,
            const std::string& pattern = "*",
            bool recursive = false);

        static std::vector<FileEntry> ScanFiles(
            const std::string& dirPath,
            const std::string& pattern = "*",
            bool recursive = false);

        // ============================================================
        // 同步 I/O — 通过 VFS 后端
        // ============================================================

        static std::vector<uint8> ReadFile(const std::string& path);
        static std::string ReadTextFile(const std::string& path);
        static bool WriteFile(const std::string& path, const std::vector<uint8>& data);
        static bool WriteTextFile(const std::string& path, const std::string& text);
        static bool AppendTextFile(const std::string& path, const std::string& text);

        // ============================================================
        // 异步 I/O — 使用 JobSystem（非 std::async）
        // ============================================================

        /**
         * @brief 异步读取文件
         *
         * @param path     文件路径
         * @param callback 完成回调（可选在主线程执行）
         * @param priority 调度优先级
         * @param dispatchToMainThread 是否将回调排队到主线程执行
         */
        static void ReadFileAsync(const std::string& path,
                                  FileCallback callback,
                                  IOPriority priority = IOPriority::Normal,
                                  bool dispatchToMainThread = true);

        static void WriteFileAsync(const std::string& path,
                                   const std::vector<uint8>& data,
                                   FileCallback callback = nullptr,
                                   IOPriority priority = IOPriority::Low);

        /** 等待所有异步 I/O 完成 */
        static void FlushAsync();

        // ============================================================
        // 主线程派发器 — 线程安全回调调度
        // ============================================================

        /**
         * @brief 将回调排队到主线程安全队列，在下一帧 PollMainThreadTasks() 时执行
         *
         * 典型用法：异步 I/O 完成后需要在主线程创建 OpenGL 纹理时。
         */
        static void SubmitToMainThread(std::function<void()> task);

        /** @brief 执行所有已排队的回调（每帧在主线程调用一次） */
        static void PollMainThreadTasks();

    private:
        // ============================================================
        // 挂载点结构
        // ============================================================

        struct MountPoint {
            std::string     name;
            MountBackendPtr backend;
        };

        // ============================================================
        // 辅助
        // ============================================================

        /** 根据名称查找挂载点 */
        static MountPoint* FindMount(std::string_view name);

        /** 解析 "name:path" 格式，返回 {mountName, subPath} */
        static std::pair<std::string, std::string> ParseVfsPath(const std::string& path);

        /** 同步读取实现 */
        static std::vector<uint8> ReadFileRaw(const std::string& realPath);

        // ============================================================
        // 静态数据成员
        // ============================================================

        static bool s_Initialized;

        /** VFS 挂载点列表 */
        static std::vector<MountPoint> s_Mounts;
        static std::mutex s_MountMutex;

        static std::string s_BasePath;

        /** 异步 I/O 待处理计数 */
        static std::atomic<uint32_t> s_AsyncPendingCount;

        /** 主线程派发器队列 */
        static std::mutex s_MainThreadMutex;
        static std::queue<std::function<void()>> s_MainThreadTasks;
    };

} // namespace Engine