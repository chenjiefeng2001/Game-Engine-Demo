#include "Engine/Core/AsyncStream.h"
#include "Engine/Core/FileSystem.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <algorithm>

// 跨平台文件 I/O 适配
#ifdef _WIN32
#  define FSEEK64 _fseeki64
#  define FTELL64 _ftelli64
#else
#  define FSEEK64 fseeko
#  define FTELL64 ftello
#endif

namespace Engine {

    // ============================================================
    // 工厂方法
    // ============================================================

    std::shared_ptr<AsyncStream> AsyncStream::Open(const std::string& path) {
        auto stream = std::make_shared<AsyncStream>();
        if (!stream->OpenInternal(path)) return nullptr;
        return stream;
    }

    // ============================================================
    // 析构
    // ============================================================

    AsyncStream::~AsyncStream() {
        Close();
    }

    // ============================================================
    // 内部：打开文件 + 启动工作线程
    // ============================================================

    bool AsyncStream::OpenInternal(const std::string& path) {
        std::string realPath = FileSystem::ResolvePath(path);

        FILE* file = nullptr;
#ifdef _WIN32
        if (fopen_s(&file, realPath.c_str(), "rb") != 0) file = nullptr;
#else
        file = fopen(realPath.c_str(), "rb");
#endif
        if (!file) {
            std::cerr << "[AsyncStream] Failed to open: " << realPath << std::endl;
            return false;
        }

        FSEEK64(file, 0, SEEK_END);
        m_FileSize = static_cast<uint64>(FTELL64(file));
        FSEEK64(file, 0, SEEK_SET);

        m_File = file;
        m_Path = realPath;
        m_Open = true;

        // 初始化缓存槽
        m_Cache.resize(kMaxCacheBlocks);
        for (auto& block : m_Cache) {
            block.data.resize(kBlockSize);
            block.fileOffset = UINT64_MAX;
        }

        // 启动工作线程
        m_Worker = std::thread(&AsyncStream::WorkerThread, this);

        std::cout << "[AsyncStream] Opened: " << realPath
                  << " (" << m_FileSize << " bytes, cache="
                  << (kMaxCacheBlocks * kBlockSize / 1024) << "KB)"
                  << std::endl;
        return true;
    }

    // ============================================================
    // 提交读取请求
    // ============================================================

    void AsyncStream::Read(uint64 offset, uint64 size, ReadCallback callback) {
        if (!m_Open) {
            if (callback) callback(false, nullptr, 0);
            return;
        }

        if (offset >= m_FileSize) {
            if (callback) callback(false, nullptr, 0);
            return;
        }
        if (offset + size > m_FileSize)
            size = m_FileSize - offset;

        m_PendingCount++;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Requests.push({offset, size, std::move(callback)});
        }
        m_CV.notify_one();
    }

    // ============================================================
    // 控制
    // ============================================================

    void AsyncStream::Close() {
        if (!m_Open.exchange(false)) return;

        // 唤醒工作线程使其退出
        m_CV.notify_all();

        if (m_Worker.joinable())
            m_Worker.join();

        if (m_File) {
            std::fclose(m_File);
            m_File = nullptr;
        }

        // 清空待处理队列（唤醒后 WorkerThread 会处理剩余请求 + 退出）
        std::cout << "[AsyncStream] Closed: " << m_Path
                  << " (cache hits=" << m_CacheHits.load()
                  << ", misses=" << m_CacheMisses.load() << ")"
                  << std::endl;
    }

    void AsyncStream::Cancel() {
        m_Cancelled = true;
        std::lock_guard<std::mutex> lock(m_Mutex);
        while (!m_Requests.empty()) {
            auto& req = m_Requests.front();
            if (req.callback) req.callback(false, nullptr, 0);
            m_PendingCount--;
            m_Requests.pop();
        }
        m_PendingCount = 0;
    }

    // ============================================================
    // 缓存查找（在工作线程中调用）
    // ============================================================

    namespace {
        // 检查 [reqOff, reqOff+reqSize) 是否完全包含在缓存块 [cachedOff, cachedOff+kBlockSize) 中
        bool IsCoveredByCache(uint64 reqOff, uint64 reqSize,
                              uint64 cachedOff) {
            if (cachedOff == UINT64_MAX) return false;
            return reqOff >= cachedOff &&
                   reqOff + reqSize <= cachedOff + AsyncStream::kBlockSize;
        }
    }

    // ============================================================
    // 后台工作线程
    // ============================================================

    void AsyncStream::WorkerThread() {
        while (m_Open && !m_Cancelled) {
            Request req;

            // ── 等待请求 ──
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_CV.wait(lock, [this]() {
                    return !m_Requests.empty() || !m_Open;
                });

                if (!m_Open) break;
                if (m_Requests.empty()) continue;

                req = std::move(m_Requests.front());
                m_Requests.pop();
            }

            // ── 读取数据（优先走缓存） ──
            AsyncStream::ReadCallback callback = std::move(req.callback);
            uint64 reqOff = req.offset;
            uint64 reqSize = req.size;

            if (!m_File) {
                m_PendingCount--;
                if (callback) callback(false, nullptr, 0);
                continue;
            }

            // 准备输出缓冲区（暂存读出的数据）
            std::vector<uint8> output(reqSize);
            uint64 bytesRead = 0;

            // 逐块读取（每次读取最多一个 kBlockSize 的缓存块）
            while (bytesRead < reqSize) {
                uint64 currentOff = reqOff + bytesRead;
                uint64 remaining  = reqSize - bytesRead;
                uint64 readSize   = std::min(remaining, kBlockSize);

                // ── 尝试从缓存命中 ──
                bool cacheHit = false;
                for (auto& block : m_Cache) {
                    if (IsCoveredByCache(currentOff, readSize, block.fileOffset)) {
                        // 缓存命中
                        uint64 cacheOff = currentOff - block.fileOffset;
                        std::memcpy(output.data() + bytesRead,
                                    block.data.data() + cacheOff,
                                    static_cast<size_t>(readSize));
                        block.lastAccess = ++m_CacheTimestamp;
                        bytesRead += readSize;
                        cacheHit = true;
                        m_CacheHits++;
                        break;
                    }
                }

                if (cacheHit) continue;

                // ── 缓存未命中：从磁盘读取整个缓存块 ──
                m_CacheMisses++;

                // 计算要读取的缓存块范围（对齐到块边界）
                uint64 blockStart = (currentOff / kBlockSize) * kBlockSize;
                uint64 blockEnd   = std::min(blockStart + kBlockSize, m_FileSize);
                uint64 blockSize  = blockEnd - blockStart;

                // 寻找要淘汰的缓存槽（LRU：选 lastAccess 最小的）
                uint32 oldestTime = UINT32_MAX;
                int    evictIndex = 0;
                for (int i = 0; i < (int)m_Cache.size(); ++i) {
                    if (m_Cache[i].fileOffset == UINT64_MAX) { // 空槽
                        evictIndex = i;
                        break;
                    }
                    if (m_Cache[i].lastAccess < oldestTime) {
                        oldestTime = m_Cache[i].lastAccess;
                        evictIndex = i;
                    }
                }

                // 从磁盘读取
                FSEEK64(m_File, static_cast<int64_t>(blockStart), SEEK_SET);
                size_t actual = std::fread(m_Cache[evictIndex].data.data(), 1,
                                           static_cast<size_t>(blockSize), m_File);

                m_Cache[evictIndex].fileOffset = blockStart;
                m_Cache[evictIndex].lastAccess = ++m_CacheTimestamp;

                // 将请求的数据从新缓存块拷贝到输出缓冲区
                uint64 copyOff = currentOff - blockStart;
                uint64 copySize = std::min(readSize, static_cast<uint64>(actual) - copyOff);
                if (copySize > 0) {
                    std::memcpy(output.data() + bytesRead,
                                m_Cache[evictIndex].data.data() + copyOff,
                                static_cast<size_t>(copySize));
                    bytesRead += copySize;
                } else {
                    break; // 磁盘读取失败
                }
            }

            m_PendingCount--;

            // ── 回调 ──
            if (callback) {
                callback(bytesRead == reqSize, output.data(), bytesRead);
            }
        }
    }

} // namespace Engine
