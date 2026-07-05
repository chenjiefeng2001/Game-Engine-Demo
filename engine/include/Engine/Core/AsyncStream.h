#pragma once

/**
 * @file AsyncStream.h
 * @brief 异步文件流 — 支持分块读取、流式加载和后台预取
 *
 * 设计要点：
 *   - 文件打开后保持打开状态，多次调用 Read 共享同一文件句柄
 *   - 读请求通过队列提交，后台线程串行处理
 *   - 回调在后台线程中触发，注意线程安全
 *   - 路径经过 FileSystem::ResolvePath 解析，支持 VFS 挂载点
 *
 * 使用示例：
 * @code
 *   auto stream = AsyncStream::Open("assets:levels/level1.dat");
 *   if (!stream) return;
 *
 *   // 分块读取大文件
 *   stream->Read(0, 65536, [](bool ok, const uint8* data, uint64 bytes) {
 *       if (ok) ProcessFirstChunk(data, bytes);
 *   });
 *
 *   stream->Read(65536, 65536, [](bool ok, const uint8* data, uint64 bytes) {
 *       if (ok) ProcessSecondChunk(data, bytes);
 *   });
 *
 *   // 所有请求完成后关闭
 *   stream->Close();
 * @endcode
 */

#include "Engine/Types.h"
#include <string>
#include <functional>
#include <memory>
#include <cstdint>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <queue>

namespace Engine {

    class AsyncStream : public std::enable_shared_from_this<AsyncStream> {
    public:
        // ── 回调类型 ──
        using ReadCallback = std::function<void(bool success,
                                                const uint8* data,
                                                uint64 bytes)>;

        /** 缓存块大小（64KB） */
        static constexpr uint64 kBlockSize = 64 * 1024;

        AsyncStream() = default;
        ~AsyncStream();

        AsyncStream(const AsyncStream&) = delete;
        AsyncStream& operator=(const AsyncStream&) = delete;

        // ── 工厂 ──

        /**
         * @brief 以流模式打开文件
         * @param path 虚拟路径（经 FileSystem::ResolvePath 解析）
         * @return 流对象指针，失败返回 nullptr
         *
         * 文件句柄在后台保持打开，直到 Close() 或析构。
         */
        static std::shared_ptr<AsyncStream> Open(const std::string& path);

        // ── 读取 ──

        /**
         * @brief 提交异步读取请求
         * @param offset   文件偏移（字节）
         * @param size     请求读取的字节数
         * @param callback 完成回调
         *
         * 回调在后台线程中调用。若读取成功，data 指向内部缓冲区，
         * 在回调返回后失效，请及时拷贝。
         *
         * 文件打开后所有 Read 请求共享同一个文件句柄，
         * 请求串行执行，不会相互交错。
         */
        void Read(uint64 offset, uint64 size, ReadCallback callback);

        // ── 控制 ──

        /** 关闭流。等待所有待处理的读取完成。 */
        void Close();

        /** 取消所有待处理的读取请求。回调不会被调用。 */
        void Cancel();

        // ── 查询 ──

        bool IsOpen() const      { return m_Open; }
        uint64 GetSize() const   { return m_FileSize; }
        const std::string& GetPath() const { return m_Path; }

        /** 当前待处理的读取请求数 */
        uint32 GetPendingCount() const { return m_PendingCount.load(); }

    private:
        // 内部：实际打开文件 + 启动工作线程
        bool OpenInternal(const std::string& path);

        // 内部工作线程函数
        void WorkerThread();

        // ── 读请求结构 ──
        struct Request {
            uint64      offset;
            uint64      size;
            ReadCallback callback;
        };

        // ── 块缓存（避免重复磁盘读取） ──
        static constexpr uint32 kMaxCacheBlocks = 8;      // 最多缓存 8 块 (512KB)

        struct CacheBlock {
            uint64      fileOffset = UINT64_MAX; // 对应文件中的起始偏移
            std::vector<uint8> data;             // 缓存数据（大小 = kBlockSize）
            uint32      lastAccess  = 0;          // 时间戳（LRU 淘汰用）
        };

        // ── 状态 ──
        std::string     m_Path;
        uint64          m_FileSize = 0;
        std::atomic<bool> m_Open{false};
        std::atomic<bool> m_Cancelled{false};
        std::atomic<uint32> m_PendingCount{0};
        std::atomic<uint32> m_CacheHits{0};
        std::atomic<uint32> m_CacheMisses{0};

        // 文件句柄
        FILE* m_File = nullptr;

        // 线程与同步
        std::thread                     m_Worker;
        std::mutex                      m_Mutex;
        std::condition_variable         m_CV;
        std::queue<Request>             m_Requests;

        // 缓存（工作线程独占访问，无需互斥）
        std::vector<CacheBlock> m_Cache;
        uint32                  m_CacheTimestamp = 0;
    };

} // namespace Engine
