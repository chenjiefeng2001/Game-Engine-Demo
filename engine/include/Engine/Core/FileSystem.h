#pragma once

/**
 * @file FileSystem.h
 * @brief 文件系统抽象层 — 跨平台路径操作、同步/异步文件 I/O、目录扫描
 *
 * 设计原则：
 *   - 所有 API 均为静态方法，全局唯一
 *   - 路径操作使用 std::filesystem 实现（C++17）
 *   - 异步 I/O 使用线程池 + 回调模式
 *   - 同步 I/O 为基础设施，异步为高级功能
 */

#include "Engine/Types.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>

namespace Engine {

    struct FileEntry {
        std::string path;        // 完整路径
        std::string name;        // 文件名（不含目录）
        std::string extension;   // 扩展名（不含点，如 "png"）
        uint64      size = 0;    // 文件大小（字节）
        bool        isDirectory = false;
        bool        isReadOnly  = false;
    };

    // 异步操作结果
    struct AsyncResult {
        bool     success = false;
        uint64   bytesTransferred = 0;
        std::vector<uint8> buffer;  // 读操作时有效
        std::string errorMessage;
    };

    using FileCallback = std::function<void(const AsyncResult& result)>;

    class FileSystem {
    public:
        // ============================================================
        // 生命周期（初始化异步线程池）
        // ============================================================

        /** 初始化文件系统（必须在使用异步 API 前调用） */
        static void Init(uint32 threadCount = 2);

        /** 关闭文件系统，等待所有异步操作完成 */
        static void Shutdown();

        /** 是否已初始化 */
        static bool IsInitialized() { return s_Initialized; }

        // ============================================================
        // 虚拟文件系统（VFS）— 挂载点与路径解析
        // ============================================================

        /**
         * @brief 注册一个路径挂载点
         * @param name      挂载点名称（如 "assets"、"mod"）
         * @param realPath  对应的真实目录路径
         *
         * 注册后可通过 "name:相对路径" 访问文件。
         * 例如 Mount("assets", "/game/data/assets") 后，
         * "assets:tex.png" 解析为 "/game/data/assets/tex.png"
         */
        static void Mount(const std::string& name, const std::string& realPath);

        /** 移除一个挂载点 */
        static void Unmount(const std::string& name);

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

        /**
         * @brief 设置默认基础路径（用于解析无挂载点匹配的相对路径）
         * 默认值：应用程序当前工作目录
         */
        static void SetBasePath(const std::string& path);
        static const std::string& GetBasePath();

        /** 获取文件名（含扩展名）: "/path/to/file.txt" → "file.txt" */
        static std::string GetFileName(const std::string& path);

        /** 获取文件名（不含扩展名）: "/path/to/file.txt" → "file" */
        static std::string GetStem(const std::string& path);

        /** 获取扩展名（不含点）: "/path/to/file.txt" → "txt" */
        static std::string GetExtension(const std::string& path);

        /** 获取目录部分: "/path/to/file.txt" → "/path/to" */
        static std::string GetDirectory(const std::string& path);

        /** 拼接路径 */
        static std::string Combine(const std::string& a, const std::string& b);

        /** 获取绝对路径 */
        static std::string GetAbsolute(const std::string& path);

        /** 标准化路径（替换为 /，移除 .. 和 .） */
        static std::string Normalize(const std::string& path);

        /** 判断是否为绝对路径 */
        static bool IsAbsolute(const std::string& path);

        /** 获取应用程序所在目录 */
        static std::string GetAppDirectory();

        // ============================================================
        // 文件/目录查询
        // ============================================================

        /** 文件或目录是否存在 */
        static bool Exists(const std::string& path);

        /** 是否为目录 */
        static bool IsDirectory(const std::string& path);

        /** 是否为常规文件 */
        static bool IsFile(const std::string& path);

        /** 获取文件大小（字节） */
        static uint64 GetFileSize(const std::string& path);

        /** 获取上次修改时间（时间戳，秒） */
        static int64 GetLastWriteTime(const std::string& path);

        // ============================================================
        // 文件/目录操作
        // ============================================================

        /** 创建目录（类似 mkdir -p，自动创建父目录） */
        static bool CreateDirectory(const std::string& path);

        /** 删除文件或空目录 */
        static bool Remove(const std::string& path);

        /** 递归删除目录及其所有内容 */
        static bool RemoveAll(const std::string& path);

        /** 重命名/移动文件或目录 */
        static bool Rename(const std::string& from, const std::string& to);

        /** 拷贝文件 */
        static bool Copy(const std::string& from, const std::string& to);

        // ============================================================
        // 同步 I/O
        // ============================================================

        /** 读取整个文件的二进制内容 */
        static std::vector<uint8> ReadFile(const std::string& path);

        /** 读取整个文件为文本 */
        static std::string ReadTextFile(const std::string& path);

        /** 写入二进制数据到文件（覆盖） */
        static bool WriteFile(const std::string& path, const std::vector<uint8>& data);

        /** 写入文本到文件（覆盖） */
        static bool WriteTextFile(const std::string& path, const std::string& text);

        /** 追加文本到文件末尾 */
        static bool AppendTextFile(const std::string& path, const std::string& text);

        // ============================================================
        // 目录扫描
        // ============================================================

        /**
         * @brief 扫描目录内容
         * @param dirPath 目录路径
         * @param pattern 通配符过滤（如 "*.png"，为空则返回所有）
         * @param recursive 是否递归子目录
         * @return 文件条目列表
         */
        static std::vector<FileEntry> ScanDirectory(
            const std::string& dirPath,
            const std::string& pattern = "*",
            bool recursive = false);

        /** 扫描目录，仅返回文件（不含目录条目） */
        static std::vector<FileEntry> ScanFiles(
            const std::string& dirPath,
            const std::string& pattern = "*",
            bool recursive = false);

        // ============================================================
        // 异步 I/O（需要 Init() 先调用）
        // ============================================================

        /**
         * @brief 异步读取文件
         * @param path     文件路径
         * @param callback 完成回调（在后台线程中调用，注意线程安全）
         */
        static void ReadFileAsync(const std::string& path, FileCallback callback);

        /**
         * @brief 异步写入文件
         * @param path     文件路径
         * @param data     要写入的数据
         * @param callback 可选完成回调
         */
        static void WriteFileAsync(const std::string& path,
                                   const std::vector<uint8>& data,
                                   FileCallback callback = nullptr);

        /**
         * @brief 等待所有待处理的异步操作完成
         * 主线程在关闭前调用，确保所有 I/O 完成
         */
        static void FlushAsync();

    private:
        static bool s_Initialized;

        // VFS 挂载点
        struct MountPoint {
            std::string name;
            std::string realPath;
        };
        static std::vector<MountPoint> s_Mounts;
        static std::string s_BasePath;

        // 异步线程池成员
        struct AsyncImpl;
        static std::unique_ptr<AsyncImpl> s_Async;

        // 内部：在真实路径上执行 I/O（跳过 ResolvePath 避免循环）
        static std::vector<uint8> ReadFileRaw(const std::string& realPath);
        static std::string ReadTextFileRaw(const std::string& realPath);
        static bool WriteFileRaw(const std::string& realPath, const std::vector<uint8>& data);
        static bool WriteTextFileRaw(const std::string& realPath, const std::string& text);
        static bool AppendTextFileRaw(const std::string& realPath, const std::string& text);
    };

} // namespace Engine
