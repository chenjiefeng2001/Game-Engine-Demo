/**
 * @file FileSystem.cpp
 * @brief VFS 后端挂载 + JobSystem 异步 I/O + 主线程派发器
 *
 * v2 架构：
 *   - IMountBackend 后端体系（OsDirectoryMount / PakArchiveMount）
 *   - Mount(name, backend) 挂载接口
 *   - OpenFile() / ReadFile() 通过 IFile 统一路径
 *   - ReadFileAsync / WriteFileAsync 基于 JobSystem（非 std::async）
 *   - SubmitToMainThread / PollMainThreadTasks 主线程派发器
 */

#include "Engine/Core/FileSystem.h"
#include "Engine/Core/OsFileMount.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Core/Log.h"
#include <filesystem>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <mutex>

namespace fs = std::filesystem;

namespace {
    Engine::Logger s_Log("FileSystem");
}

namespace Engine {

    // ============================================================
    // 静态成员初始化
    // ============================================================

    bool FileSystem::s_Initialized = false;
    std::vector<FileSystem::MountPoint> FileSystem::s_Mounts;
    std::mutex FileSystem::s_MountMutex;
    std::string FileSystem::s_BasePath;

    std::atomic<uint32_t> FileSystem::s_AsyncPendingCount{0};

    std::mutex FileSystem::s_MainThreadMutex;
    std::queue<std::function<void()>> FileSystem::s_MainThreadTasks;

    // ============================================================
    // 生命周期
    // ============================================================

    void FileSystem::Init(uint32 /*threadCount*/) {
        if (s_Initialized) return;
        s_BasePath = fs::current_path().string();
        s_Initialized = true;
        s_Log.Info("VFS initialized (base: {})", s_BasePath);
    }

    void FileSystem::Shutdown() {
        if (!s_Initialized) return;

        while (s_AsyncPendingCount.load() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        std::lock_guard<std::mutex> lock(s_MountMutex);
        s_Mounts.clear();
        s_Initialized = false;
        s_Log.Info("VFS shut down");
    }

    // ============================================================
    // VFS — 挂载后端
    // ============================================================

    void FileSystem::Mount(std::string_view name, MountBackendPtr backend) {
        std::string nameStr(name);
        std::lock_guard<std::mutex> lock(s_MountMutex);

        for (auto& mp : s_Mounts) {
            if (mp.name == nameStr) {
                mp.backend = std::move(backend);
                s_Log.Info("Remounted '{}' (backend: {})", nameStr, mp.backend->GetName());
                return;
            }
        }

        s_Log.Info("Mounted '{}' (backend: {})", nameStr, backend->GetName());
        s_Mounts.push_back({nameStr, std::move(backend)});
    }

    void FileSystem::Unmount(std::string_view name) {
        std::lock_guard<std::mutex> lock(s_MountMutex);
        s_Mounts.erase(
            std::remove_if(s_Mounts.begin(), s_Mounts.end(),
                [&](const MountPoint& mp) { return mp.name == name; }),
            s_Mounts.end());
    }

    FileSystem::MountPoint* FileSystem::FindMount(std::string_view name) {
        for (auto& mp : s_Mounts) {
            if (mp.name == name) return &mp;
        }
        return nullptr;
    }

    std::pair<std::string, std::string> FileSystem::ParseVfsPath(const std::string& path) {
        auto colonPos = path.find(':');
        if (colonPos == std::string::npos || colonPos == 0)
            return {"", path};

        if (colonPos == 1 &&
            ((path[0] >= 'A' && path[0] <= 'Z') ||
             (path[0] >= 'a' && path[0] <= 'z'))) {
            return {"", path};
        }

        std::string mountName = path.substr(0, colonPos);
        std::string subPath   = path.substr(colonPos + 1);
        return {mountName, subPath};
    }

    IFilePtr FileSystem::OpenFile(const std::string& path) {
        auto [mountName, subPath] = ParseVfsPath(path);

        if (!mountName.empty()) {
            MountPoint* mp = FindMount(mountName);
            if (mp && mp->backend) {
                return mp->backend->OpenFile(subPath);
            }
        }

        // 非挂载点路径 → 回退到 basePath
        fs::path resolved = fs::absolute(fs::path(path));
        if (!resolved.empty()) {
            FILE* fp = nullptr;
        #ifdef _WIN32
            fopen_s(&fp, resolved.string().c_str(), "rb");
        #else
            fp = fopen(resolved.string().c_str(), "rb");
        #endif
            if (fp) {
                size_t size = 0;
                fseek(fp, 0, SEEK_END);
                size = static_cast<size_t>(ftell(fp));
                rewind(fp);
                return std::make_unique<OsFile>(fp, size);
            }
        }

        return nullptr;
    }

    std::string FileSystem::ResolvePath(const std::string& path) {
        if (path.empty()) return path;

        auto [mountName, subPath] = ParseVfsPath(path);

        if (!mountName.empty()) {
            MountPoint* mp = FindMount(mountName);
            if (mp) return path;  // VFS 路径 → 返回原路径（后续 I/O 通过 OpenFile 走 backend）
        }

        if (fs::path(path).is_absolute())
            return fs::path(path).lexically_normal().string();

        return (fs::path(GetBasePath()) / path).lexically_normal().string();
    }

    void FileSystem::SetBasePath(const std::string& path) {
        s_BasePath = fs::absolute(fs::path(path)).lexically_normal().string();
    }

    const std::string& FileSystem::GetBasePath() {
        if (s_BasePath.empty())
            s_BasePath = fs::current_path().string();
        return s_BasePath;
    }

    // ============================================================
    // 路径工具
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
        try { return fs::current_path().string(); } catch (...) { return "."; }
    }

    // ============================================================
    // 文件/目录查询
    // ============================================================

    bool FileSystem::Exists(const std::string& path) {
        auto [mountName, subPath] = ParseVfsPath(path);
        if (!mountName.empty()) {
            MountPoint* mp = FindMount(mountName);
            return mp && mp->backend && mp->backend->FileExists(subPath);
        }
        return fs::exists(fs::path(path));
    }

    bool FileSystem::IsDirectory(const std::string& path) {
        return fs::is_directory(fs::path(path));
    }

    bool FileSystem::IsFile(const std::string& path) {
        return fs::is_regular_file(fs::path(path));
    }

    uint64 FileSystem::GetFileSize(const std::string& path) {
        auto [mountName, subPath] = ParseVfsPath(path);
        if (!mountName.empty()) {
            MountPoint* mp = FindMount(mountName);
            if (mp && mp->backend) return mp->backend->GetFileSize(subPath);
            return 0;
        }
        try { return static_cast<uint64>(fs::file_size(fs::path(path))); }
        catch (...) { return 0; }
    }

    int64 FileSystem::GetLastWriteTime(const std::string& path) {
        try {
            auto ft = fs::last_write_time(fs::path(path));
            return std::chrono::duration_cast<std::chrono::seconds>(ft.time_since_epoch()).count();
        } catch (...) { return 0; }
    }

    bool FileSystem::CreateDirectory(const std::string& path) {
        try { return fs::create_directories(fs::path(path)); } catch (...) { return false; }
    }
    bool FileSystem::Remove(const std::string& path) {
        try { return fs::remove(fs::path(path)); } catch (...) { return false; }
    }
    bool FileSystem::RemoveAll(const std::string& path) {
        try { return fs::remove_all(fs::path(path)) > 0; } catch (...) { return false; }
    }
    bool FileSystem::Rename(const std::string& from, const std::string& to) {
        try { fs::rename(fs::path(from), fs::path(to)); return true; } catch (...) { return false; }
    }
    bool FileSystem::Copy(const std::string& from, const std::string& to) {
        try {
            fs::copy(fs::path(from), fs::path(to),
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            return true;
        } catch (...) { return false; }
    }

    // ============================================================
    // 目录扫描
    // ============================================================

    std::vector<FileEntry> FileSystem::ScanDirectory(
        const std::string& dirPath, const std::string& pattern, bool recursive)
    {
        std::vector<FileEntry> entries;
        fs::path dir(dirPath);
        if (!fs::exists(dir) || !fs::is_directory(dir)) return entries;

        auto matchesPattern = [](const std::string& name, const std::string& pat) {
            if (pat.empty() || pat == "*") return true;
            if (pat.size() > 1 && pat[0] == '*' && pat[1] == '.') {
                std::string ext = pat.substr(1);
                if (name.size() < ext.size()) return false;
                return name.substr(name.size() - ext.size()) == ext;
            }
            return name == pat;
        };

        auto makeEntry = [](const fs::directory_entry& e) -> FileEntry {
            FileEntry fe;
            fe.path = e.path().string();
            fe.name = e.path().filename().string();
            fe.extension = e.path().extension().string();
            if (!fe.extension.empty() && fe.extension[0] == '.')
                fe.extension.erase(0, 1);
            fe.isDirectory = e.is_directory();
            if (e.is_regular_file()) fe.size = static_cast<uint64>(e.file_size());
            return fe;
        };

        try {
            if (recursive) {
                for (const auto& entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied)) {
                    if (!matchesPattern(entry.path().filename().string(), pattern)) continue;
                    entries.push_back(makeEntry(entry));
                }
            } else {
                for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied)) {
                    if (!matchesPattern(entry.path().filename().string(), pattern)) continue;
                    entries.push_back(makeEntry(entry));
                }
            }
        } catch (const fs::filesystem_error& e) {
            s_Log.Error("ScanDirectory error: {}", e.what());
        }

        std::sort(entries.begin(), entries.end(),
            [](const FileEntry& a, const FileEntry& b) {
                if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
                return a.name < b.name;
            });
        return entries;
    }

    std::vector<FileEntry> FileSystem::ScanFiles(
        const std::string& dirPath, const std::string& pattern, bool recursive)
    {
        auto all = ScanDirectory(dirPath, pattern, recursive);
        all.erase(std::remove_if(all.begin(), all.end(),
            [](const FileEntry& e) { return e.isDirectory; }), all.end());
        return all;
    }

    // ============================================================
    // 同步 I/O — 通过 IFile 接口
    // ============================================================

    std::vector<uint8> FileSystem::ReadFile(const std::string& path) {
        auto file = OpenFile(path);
        if (!file) return {};

        size_t size = file->GetSize();
        std::vector<uint8> buffer(size);
        file->Seek(0);
        size_t read = file->Read(buffer.data(), size);
        if (read != size) buffer.resize(read);
        return buffer;
    }

    std::string FileSystem::ReadTextFile(const std::string& path) {
        auto data = ReadFile(path);
        if (data.empty()) return {};
        return std::string(reinterpret_cast<const char*>(data.data()), data.size());
    }

    bool FileSystem::WriteFile(const std::string& path, const std::vector<uint8>& data) {
        fs::path p{path};
        fs::create_directories(p.parent_path());
        std::ofstream file{p, std::ios::binary};
        if (!file.is_open()) return false;
        file.write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
        return file.good();
    }

    bool FileSystem::WriteTextFile(const std::string& path, const std::string& text) {
        fs::path p{path};
        fs::create_directories(p.parent_path());
        std::ofstream file{p};
        if (!file.is_open()) return false;
        file << text;
        return file.good();
    }

    bool FileSystem::AppendTextFile(const std::string& path, const std::string& text) {
        fs::path p{path};
        fs::create_directories(p.parent_path());
        std::ofstream file{p, std::ios::app};
        if (!file.is_open()) return false;
        file << text;
        return file.good();
    }

    // ============================================================
    // 异步 I/O — 使用 JobSystem（非 std::async）
    // ============================================================

    void FileSystem::ReadFileAsync(const std::string& path,
                                   FileCallback callback,
                                   IOPriority /*priority*/,
                                   bool dispatchToMainThread)
    {
        auto* js = JobSystem::Get();
        if (!js) {
            AsyncResult result;
            result.buffer = ReadFile(path);
            result.success = !result.buffer.empty();
            result.bytesTransferred = result.buffer.size();
            if (!result.success) result.errorMessage = "JobSystem not available";
            if (callback) callback(result);
            return;
        }

        s_AsyncPendingCount++;

        js->Schedule([path, callback = std::move(callback), dispatchToMainThread](uint32) {
            AsyncResult result;
            result.buffer = ReadFile(path);
            result.success = !result.buffer.empty();
            result.bytesTransferred = result.buffer.size();
            if (!result.success) result.errorMessage = "Read failed: " + path;

            s_AsyncPendingCount--;

            if (callback) {
                if (dispatchToMainThread) {
                    SubmitToMainThread([result, cb = std::move(callback)]() { cb(result); });
                } else {
                    callback(result);
                }
            }
        });
    }

    void FileSystem::WriteFileAsync(const std::string& path,
                                    const std::vector<uint8>& data,
                                    FileCallback callback,
                                    IOPriority /*priority*/)
    {
        auto* js = JobSystem::Get();
        if (!js) {
            AsyncResult result;
            result.success = WriteFile(path, data);
            result.bytesTransferred = data.size();
            if (callback) callback(result);
            return;
        }

        s_AsyncPendingCount++;

        auto dataCopy = std::make_shared<std::vector<uint8>>(data);

        js->Schedule([path, dataCopy, callback = std::move(callback)](uint32) {
            AsyncResult result;
            result.success = WriteFile(path, *dataCopy);
            result.bytesTransferred = dataCopy->size();
            if (!result.success) result.errorMessage = "Write failed: " + path;

            s_AsyncPendingCount--;

            if (callback) {
                SubmitToMainThread([result, cb = std::move(callback)]() { cb(result); });
            }
        });
    }

    void FileSystem::FlushAsync() {
        while (s_AsyncPendingCount.load() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // ============================================================
    // 主线程派发器
    // ============================================================

    void FileSystem::SubmitToMainThread(std::function<void()> task) {
        if (!task) return;
        std::lock_guard<std::mutex> lock(s_MainThreadMutex);
        s_MainThreadTasks.push(std::move(task));
    }

    void FileSystem::PollMainThreadTasks() {
        std::queue<std::function<void()>> pending;
        {
            std::lock_guard<std::mutex> lock(s_MainThreadMutex);
            std::swap(pending, s_MainThreadTasks);
        }

        while (!pending.empty()) {
            auto& task = pending.front();
            if (task) task();
            pending.pop();
        }
    }

    std::vector<uint8> FileSystem::ReadFileRaw(const std::string& realPath) {
        return ReadFile(realPath);
    }

} // namespace Engine