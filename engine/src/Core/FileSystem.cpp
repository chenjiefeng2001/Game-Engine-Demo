#include "Engine/Core/FileSystem.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

namespace Engine {

    namespace {
        FileEntry MakeEntry(const fs::directory_entry& entry) {
            FileEntry fe;
            fe.path        = entry.path().string();
            fe.name        = entry.path().filename().string();
            fe.extension   = entry.path().extension().string();
            if (!fe.extension.empty() && fe.extension[0] == '.')
                fe.extension.erase(0, 1);
            fe.isDirectory = entry.is_directory();
            fe.isReadOnly  = (entry.symlink_status().permissions() &
                              fs::perms::owner_write) == fs::perms::none;
            if (entry.is_regular_file())
                fe.size = static_cast<uint64>(entry.file_size());
            return fe;
        }

        bool MatchesPattern(const std::string& name, const std::string& pattern) {
            if (pattern.empty() || pattern == "*") return true;
            if (pattern.size() > 1 && pattern[0] == '*' && pattern[1] == '.') {
                std::string ext = pattern.substr(1);
                if (name.size() < ext.size()) return false;
                return name.substr(name.size() - ext.size()) == ext;
            }
            return name == pattern;
        }
    }

    // ============================================================
    // 异步 I/O 实现
    // ============================================================

    struct FileSystem::AsyncImpl {
        std::mutex              mutex;
        std::queue<std::future<void>> pending;
        std::atomic<uint32>     activeCount{0};
        uint32                  maxThreads = 2;
        bool                    shutdownRequested = false;

        void Enqueue(std::function<void()> task) {
            if (shutdownRequested) return;
            auto fut = std::async(std::launch::async, [this, task]() {
                activeCount++;
                task();
                activeCount--;
            });
            std::lock_guard<std::mutex> lock(mutex);
            while (!pending.empty()) {
                if (pending.front().wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                    break;
                pending.pop();
            }
            pending.push(std::move(fut));
        }

        void WaitAll() {
            shutdownRequested = true;
            std::unique_lock<std::mutex> lock(mutex);
            while (!pending.empty()) {
                auto fut = std::move(pending.front());
                pending.pop();
                lock.unlock();
                if (fut.valid()) fut.wait();
                lock.lock();
            }
        }
    };

    // ============================================================
    // 静态成员
    // ============================================================

    bool FileSystem::s_Initialized = false;
    std::unique_ptr<FileSystem::AsyncImpl> FileSystem::s_Async = nullptr;
    std::vector<FileSystem::MountPoint> FileSystem::s_Mounts;
    std::string FileSystem::s_BasePath;

    // ============================================================
    // 生命周期
    // ============================================================

    void FileSystem::Init(uint32 threadCount) {
        if (s_Initialized) return;
        s_Async = std::make_unique<AsyncImpl>();
        s_Async->maxThreads = std::max(1u, threadCount);
        s_BasePath = fs::current_path().string();
        s_Initialized = true;
        std::cout << "[FileSystem] Initialized (" << s_Async->maxThreads
                  << " threads, base: " << s_BasePath << ")" << std::endl;
    }

    void FileSystem::Shutdown() {
        if (!s_Initialized) return;
        if (s_Async) { s_Async->WaitAll(); s_Async.reset(); }
        s_Mounts.clear();
        s_Initialized = false;
    }

    // ============================================================
    // VFS — 挂载点管理
    // ============================================================

    void FileSystem::Mount(const std::string& name, const std::string& realPath) {
        std::string abs = fs::absolute(fs::path(realPath)).lexically_normal().string();
        // 如果同名挂载点已存在，替换
        for (auto& mp : s_Mounts) {
            if (mp.name == name) {
                mp.realPath = abs;
                std::cout << "[FileSystem] Remounted '" << name
                          << "' -> " << abs << std::endl;
                return;
            }
        }
        s_Mounts.push_back({name, abs});
        std::cout << "[FileSystem] Mounted '" << name
                  << "' -> " << abs << std::endl;
    }

    void FileSystem::Unmount(const std::string& name) {
        s_Mounts.erase(std::remove_if(s_Mounts.begin(), s_Mounts.end(),
            [&](const MountPoint& mp) { return mp.name == name; }),
            s_Mounts.end());
    }

    void FileSystem::SetBasePath(const std::string& path) {
        s_BasePath = fs::absolute(fs::path(path)).lexically_normal().string();
    }

    const std::string& FileSystem::GetBasePath() {
        if (s_BasePath.empty())
            s_BasePath = fs::current_path().string();
        return s_BasePath;
    }

    std::string FileSystem::ResolvePath(const std::string& path) {
        if (path.empty()) return path;

        // 1. "mountName:sub/path" 格式 → 从挂载点解析
        auto colonPos = path.find(':');
        if (colonPos != std::string::npos && colonPos > 0) {
            std::string mountName = path.substr(0, colonPos);
            std::string subPath   = path.substr(colonPos + 1);

            // 检查是否是挂载点（而非 Windows 盘符如 "C:"）
            bool isWindowsDrive = (colonPos == 1 &&
                ((path[0] >= 'A' && path[0] <= 'Z') ||
                 (path[0] >= 'a' && path[0] <= 'z')));

            if (!isWindowsDrive) {
                for (const auto& mp : s_Mounts) {
                    if (mp.name == mountName) {
                        return (fs::path(mp.realPath) / subPath).lexically_normal().string();
                    }
                }
                // 挂载点不存在，回退
            }
        }

        // 2. 绝对路径 → 直接返回
        if (fs::path(path).is_absolute())
            return fs::path(path).lexically_normal().string();

        // 3. 相对路径 → 依次尝试每个挂载点
        for (const auto& mp : s_Mounts) {
            fs::path candidate = fs::path(mp.realPath) / path;
            if (fs::exists(candidate))
                return candidate.lexically_normal().string();
        }

        // 4. 都不存在 → basePath + path
        return (fs::path(GetBasePath()) / path).lexically_normal().string();
    }

    // ============================================================
    // 路径操作
    // ============================================================

    std::string FileSystem::GetFileName(const std::string& path) {
        return fs::path(path).filename().string();
    }

    std::string FileSystem::GetStem(const std::string& path) {
        return fs::path(path).stem().string();
    }

    std::string FileSystem::GetExtension(const std::string& path) {
        std::string ext = fs::path(path).extension().string();
        if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
        return ext;
    }

    std::string FileSystem::GetDirectory(const std::string& path) {
        auto parent = fs::path(path).parent_path();
        return parent.empty() ? "" : parent.string();
    }

    std::string FileSystem::Combine(const std::string& a, const std::string& b) {
        return (fs::path(a) / fs::path(b)).string();
    }

    std::string FileSystem::GetAbsolute(const std::string& path) {
        return fs::absolute(fs::path(path)).lexically_normal().string();
    }

    std::string FileSystem::Normalize(const std::string& path) {
        return fs::path(path).lexically_normal().string();
    }

    bool FileSystem::IsAbsolute(const std::string& path) {
        return fs::path(path).is_absolute();
    }

    std::string FileSystem::GetAppDirectory() {
        try { return fs::current_path().string(); }
        catch (...) { return "."; }
    }

    // ============================================================
    // 文件/目录查询
    // ============================================================

    bool FileSystem::Exists(const std::string& path) {
        return fs::exists(fs::path(ResolvePath(path)));
    }

    bool FileSystem::IsDirectory(const std::string& path) {
        return fs::is_directory(fs::path(ResolvePath(path)));
    }

    bool FileSystem::IsFile(const std::string& path) {
        return fs::is_regular_file(fs::path(ResolvePath(path)));
    }

    uint64 FileSystem::GetFileSize(const std::string& path) {
        try { return static_cast<uint64>(fs::file_size(fs::path(ResolvePath(path)))); }
        catch (...) { return 0; }
    }

    int64 FileSystem::GetLastWriteTime(const std::string& path) {
        try {
            auto ft = fs::last_write_time(fs::path(ResolvePath(path)));
            auto epoch = ft.time_since_epoch();
            return std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
        } catch (...) { return 0; }
    }

    // ============================================================
    // 文件/目录操作
    // ============================================================

    bool FileSystem::CreateDirectory(const std::string& path) {
        try {
            return fs::create_directories(fs::path(ResolvePath(path)));
        } catch (const fs::filesystem_error& e) {
            std::cerr << "[FileSystem] CreateDirectory failed: " << e.what() << std::endl;
            return false;
        }
    }

    bool FileSystem::Remove(const std::string& path) {
        try { return fs::remove(fs::path(ResolvePath(path))); }
        catch (const fs::filesystem_error& e) {
            std::cerr << "[FileSystem] Remove failed: " << e.what() << std::endl;
            return false;
        }
    }

    bool FileSystem::RemoveAll(const std::string& path) {
        try { return fs::remove_all(fs::path(ResolvePath(path))) > 0; }
        catch (const fs::filesystem_error& e) {
            std::cerr << "[FileSystem] RemoveAll failed: " << e.what() << std::endl;
            return false;
        }
    }

    bool FileSystem::Rename(const std::string& from, const std::string& to) {
        try {
            fs::rename(fs::path(ResolvePath(from)), fs::path(ResolvePath(to)));
            return true;
        } catch (const fs::filesystem_error& e) {
            std::cerr << "[FileSystem] Rename failed: " << e.what() << std::endl;
            return false;
        }
    }

    bool FileSystem::Copy(const std::string& from, const std::string& to) {
        try {
            fs::copy(fs::path(ResolvePath(from)), fs::path(ResolvePath(to)),
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            return true;
        } catch (const fs::filesystem_error& e) {
            std::cerr << "[FileSystem] Copy failed: " << e.what() << std::endl;
            return false;
        }
    }

    // ============================================================
    // 同步 I/O — 公开方法（先解析路径，再转发到 Raw）
    // ============================================================

    std::vector<uint8> FileSystem::ReadFile(const std::string& path) {
        return ReadFileRaw(ResolvePath(path));
    }

    std::string FileSystem::ReadTextFile(const std::string& path) {
        return ReadTextFileRaw(ResolvePath(path));
    }

    bool FileSystem::WriteFile(const std::string& path, const std::vector<uint8>& data) {
        return WriteFileRaw(ResolvePath(path), data);
    }

    bool FileSystem::WriteTextFile(const std::string& path, const std::string& text) {
        return WriteTextFileRaw(ResolvePath(path), text);
    }

    bool FileSystem::AppendTextFile(const std::string& path, const std::string& text) {
        return AppendTextFileRaw(ResolvePath(path), text);
    }

    // ============================================================
    // 同步 I/O — Raw 实现（接收已解析的绝对路径，仅供内部使用）
    // ============================================================

    std::vector<uint8> FileSystem::ReadFileRaw(const std::string& realPath) {
        fs::path p{realPath};
        std::ifstream file{p, std::ios::binary | std::ios::ate};
        if (!file.is_open()) {
            std::cerr << "[FileSystem] ReadFile failed: " << realPath << std::endl;
            return {};
        }
        auto fileSize = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);
        std::vector<uint8> buffer(fileSize);
        if (fileSize > 0) {
            file.read(reinterpret_cast<char*>(buffer.data()),
                      static_cast<std::streamsize>(fileSize));
        }
        return buffer;
    }

    std::string FileSystem::ReadTextFileRaw(const std::string& realPath) {
        fs::path p{realPath};
        std::ifstream file{p};
        if (!file.is_open()) {
            std::cerr << "[FileSystem] ReadTextFile failed: " << realPath << std::endl;
            return {};
        }
        return std::string(std::istreambuf_iterator<char>(file),
                           std::istreambuf_iterator<char>());
    }

    bool FileSystem::WriteFileRaw(const std::string& realPath, const std::vector<uint8>& data) {
        fs::path p{realPath};
        // 自动创建父目录
        fs::create_directories(p.parent_path());
        std::ofstream file{p, std::ios::binary};
        if (!file.is_open()) {
            std::cerr << "[FileSystem] WriteFile failed: " << realPath << std::endl;
            return false;
        }
        file.write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
        return file.good();
    }

    bool FileSystem::WriteTextFileRaw(const std::string& realPath, const std::string& text) {
        fs::path p{realPath};
        fs::create_directories(p.parent_path());
        std::ofstream file{p};
        if (!file.is_open()) {
            std::cerr << "[FileSystem] WriteTextFile failed: " << realPath << std::endl;
            return false;
        }
        file << text;
        return file.good();
    }

    bool FileSystem::AppendTextFileRaw(const std::string& realPath, const std::string& text) {
        fs::path p{realPath};
        fs::create_directories(p.parent_path());
        std::ofstream file{p, std::ios::app};
        if (!file.is_open()) {
            std::cerr << "[FileSystem] AppendTextFile failed: " << realPath << std::endl;
            return false;
        }
        file << text;
        return file.good();
    }

    // ============================================================
    // 目录扫描
    // ============================================================

    std::vector<FileEntry> FileSystem::ScanDirectory(
        const std::string& dirPath,
        const std::string& pattern,
        bool recursive)
    {
        std::vector<FileEntry> entries;
        std::string realDir = ResolvePath(dirPath);
        fs::path dir(realDir);

        if (!fs::exists(dir) || !fs::is_directory(dir)) {
            std::cerr << "[FileSystem] ScanDirectory failed: " << realDir << std::endl;
            return entries;
        }

        try {
            if (recursive) {
                for (const auto& entry : fs::recursive_directory_iterator(
                         dir, fs::directory_options::skip_permission_denied)) {
                    if (!MatchesPattern(entry.path().filename().string(), pattern)) continue;
                    entries.push_back(MakeEntry(entry));
                }
            } else {
                for (const auto& entry : fs::directory_iterator(
                         dir, fs::directory_options::skip_permission_denied)) {
                    if (!MatchesPattern(entry.path().filename().string(), pattern)) continue;
                    entries.push_back(MakeEntry(entry));
                }
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "[FileSystem] ScanDirectory error: " << e.what() << std::endl;
        }

        std::sort(entries.begin(), entries.end(),
            [](const FileEntry& a, const FileEntry& b) {
                if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
                return a.name < b.name;
            });
        return entries;
    }

    std::vector<FileEntry> FileSystem::ScanFiles(
        const std::string& dirPath,
        const std::string& pattern,
        bool recursive)
    {
        auto all = ScanDirectory(dirPath, pattern, recursive);
        all.erase(std::remove_if(all.begin(), all.end(),
            [](const FileEntry& e) { return e.isDirectory; }), all.end());
        return all;
    }

    // ============================================================
    // 异步 I/O
    // ============================================================

    void FileSystem::ReadFileAsync(const std::string& path, FileCallback callback) {
        if (!s_Async) {
            if (callback) callback({false, 0, {}, "Async not initialized"});
            return;
        }
        std::string resolved = ResolvePath(path);
        s_Async->Enqueue([resolved, callback]() {
            AsyncResult result;
            result.buffer = ReadFileRaw(resolved);
            result.success = !result.buffer.empty();
            result.bytesTransferred = result.buffer.size();
            if (!result.success) result.errorMessage = "Failed to read: " + resolved;
            if (callback) callback(result);
        });
    }

    void FileSystem::WriteFileAsync(const std::string& path,
                                    const std::vector<uint8>& data,
                                    FileCallback callback)
    {
        if (!s_Async) {
            if (callback) callback({false, 0, {}, "Async not initialized"});
            return;
        }
        std::string resolved = ResolvePath(path);
        auto dataCopy = std::make_shared<std::vector<uint8>>(data);
        s_Async->Enqueue([resolved, dataCopy, callback]() {
            AsyncResult result;
            result.success = WriteFileRaw(resolved, *dataCopy);
            result.bytesTransferred = dataCopy->size();
            if (!result.success) result.errorMessage = "Failed to write: " + resolved;
            if (callback) callback(result);
        });
    }

    void FileSystem::FlushAsync() {
        if (s_Async) s_Async->WaitAll();
    }

} // namespace Engine
