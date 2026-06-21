/**
 * @file Log.cpp
 * @brief 引擎日志系统实现 — 基于 spdlog 异步模式 + 内存历史 Sink
 *
 * 架构说明：
 *   - 异步模式：使用 spdlog::async_factory，线程池处理所有日志写入
 *   - Sink 链：每个 logger 共享三个 sink
 *     (1) stdout_color_sink — 控制台输出（带颜色）
 *     (2) rotating_file_sink — 文件轮转输出
 *     (3) MemoryHistorySink — 内存环形缓冲区，供崩溃时 Dump
 *   - 崩溃集成：通过 ForceFlushAndDump() 强制刷盘 + 导出内存日志
 */

#include "Engine/Core/Log.h"
#include "Engine/ConsoleLog.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/async.h>
#include <spdlog/common.h>
#include <spdlog/details/thread_pool.h>
#include <memory>
#include <mutex>
#include <vector>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>

namespace Engine {

// ============================================================
// MemoryHistorySink — 内存环形缓冲区 Sink
//
// 容量：kHistorySize 条日志（默认 2048）
// 用途：在崩溃时 Dump 到文件，帮助诊断最后时刻的上下文
// 线程安全：使用 std::mutex 保护环形缓冲区写入
// ============================================================

constexpr size_t kHistorySize = 2048;

struct HistoryEntry {
    spdlog::level::level_enum level;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
};

class MemoryHistorySink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    MemoryHistorySink() : m_Entries(kHistorySize), m_Head(0), m_Count(0) {}

    /** 导出所有历史日志到文件 */
    void DumpToFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::ofstream f(path);
        if (!f) return;

        f << "=== Engine Log History Dump ===\n";
        f << "Entries: " << m_Count << " / " << kHistorySize << "\n\n";

        // 从最旧条目开始遍历
        size_t start = (m_Count < kHistorySize) ? 0 : m_Head;
        size_t count = (std::min)(m_Count, kHistorySize);

        static const char* kLevelNames[] = {
            "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL", "OFF"
        };

        for (size_t i = 0; i < count; ++i) {
            size_t idx = (start + i) % kHistorySize;
            const auto& entry = m_Entries[idx];

            // 格式化时间戳
            auto tt = std::chrono::system_clock::to_time_t(entry.timestamp);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                entry.timestamp.time_since_epoch()) % 1000;

            std::tm tm;
#if defined(_WIN32)
            gmtime_s(&tm, &tt);
#else
            gmtime_r(&tt, &tm);
#endif

            char timeBuf[32];
            std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tm);

            int levelIdx = static_cast<int>(entry.level);
            const char* levelName = (levelIdx >= 0 && levelIdx < 7)
                ? kLevelNames[levelIdx] : "?????";

            f << "[" << timeBuf << "." << std::setw(3) << std::setfill('0') << ms.count() << "]"
              << "[" << levelName << "] "
              << entry.message << "\n";
        }

        f << "\n=== End of History Dump ===\n";
    }

    /** 获取条目总数 */
    size_t GetCount() {
        std::lock_guard<std::mutex> lock(mutex_);
        return m_Count;
    }

    /** 获取容量 */
    constexpr size_t GetCapacity() const { return kHistorySize; }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        std::lock_guard<std::mutex> lock(mutex_);

        // 格式化消息
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        std::string text(formatted.data(), formatted.size());

        // 写入环形缓冲区
        size_t idx = m_Head;
        m_Entries[idx].level     = msg.level;
        m_Entries[idx].message   = std::move(text);
        m_Entries[idx].timestamp = std::chrono::system_clock::now();

        m_Head = (m_Head + 1) % kHistorySize;
        if (m_Count < kHistorySize) {
            ++m_Count;
        }
    }

    void flush_() override {
        // MemoryHistorySink 不需要 flush
    }

private:
    std::vector<HistoryEntry> m_Entries;
    mutable size_t m_Head;
    mutable size_t m_Count;
};

// ============================================================
// ConsoleLogSink — 将日志转发到 ImGui 控制台
// ============================================================

class ConsoleLogSink final : public spdlog::sinks::base_sink<std::mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        LogLevel clLevel;
        switch (msg.level) {
            case spdlog::level::warn:    clLevel = LogLevel::Warn;  break;
            case spdlog::level::err:
            case spdlog::level::critical: clLevel = LogLevel::Error; break;
            default:                     clLevel = LogLevel::Info;  break;
        }

        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        std::string text(formatted.data(), formatted.size());

        ConsoleLog::Instance().Log(clLevel, text);
    }

    void flush_() override {
        // ConsoleLog 无需显式 flush
    }
};

// ============================================================
// 静态成员
// ============================================================

bool Log::s_Initialized = false;
std::shared_ptr<spdlog::logger> Log::s_DefaultLogger = nullptr;
std::shared_ptr<MemoryHistorySink> Log::s_MemorySink = nullptr;
static std::mutex s_LoggerMutex;

// ============================================================
// 生命周期
// ============================================================

void Log::Init(const std::string& filePath, Level consoleLevel, Level fileLevel) {
    if (s_Initialized) return;

    try {
        // ── 1. 初始化 spdlog 异步线程池 ──
        // 队列大小：8192 条，1 个后台工作线程
        // 溢出策略：block（阻塞调用者直到队列有空位）
        spdlog::init_thread_pool(8192, 1);

        // ── 2. 创建内存历史 Sink（在所有 Sink 之前创建，确保可用） ──
        s_MemorySink = std::make_shared<MemoryHistorySink>();
        s_MemorySink->set_level(spdlog::level::trace);

        // ── 3. 创建控制台 sink（带颜色） ──
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(static_cast<spdlog::level::level_enum>(consoleLevel));
        consoleSink->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");

        // ── 4. 创建文件 sink（轮转，最大 5MB，保留 3 个备份） ──
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            filePath, 5 * 1024 * 1024, 3);
        fileSink->set_level(static_cast<spdlog::level::level_enum>(fileLevel));
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [%s:%#] %v");

        // ── 5. 创建 ImGui 控制台 sink ──
        auto imGuiSink = std::make_shared<ConsoleLogSink>();
        imGuiSink->set_level(spdlog::level::trace);

        // ── 6. 创建异步默认 logger（标签 "Engine"） ──
        // 使用 async_factory 让所有日志通过线程池异步写入
        std::vector<spdlog::sink_ptr> sinks = {
            consoleSink, fileSink, imGuiSink, s_MemorySink
        };

        s_DefaultLogger = std::make_shared<spdlog::logger>(
            "Engine", sinks.begin(), sinks.end());
        s_DefaultLogger->set_level(spdlog::level::trace);
        s_DefaultLogger->flush_on(spdlog::level::warn);

        // 注册为 spdlog 默认 logger
        spdlog::set_default_logger(s_DefaultLogger);

        s_Initialized = true;

        // 使用宏记录初始化日志
        ENGINE_LOG_INFO("Engine", "Log system initialized (async mode) -> {}", filePath);

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "[Log] Initialization failed: " << ex.what() << std::endl;
    }
}

void Log::Shutdown() {
    if (!s_Initialized) return;

    ENGINE_LOG_INFO("Engine", "Log system shutting down...");

    // 先强制刷盘所有待处理日志
    ForceFlushAll();

    s_DefaultLogger.reset();
    s_MemorySink.reset();
    spdlog::shutdown();
    s_Initialized = false;
}

// ============================================================
// 获取 Logger
// ============================================================

std::shared_ptr<spdlog::logger> Log::GetDefaultLogger() {
    if (!s_DefaultLogger) {
        Init();
    }
    return s_DefaultLogger;
}

Logger Log::GetLogger(const std::string& name) {
    return Logger(name);
}

std::shared_ptr<spdlog::logger> Log::Get(const std::string& name) {
    // 先查找是否已有
    auto logger = spdlog::get(name);
    if (logger) return logger;

    // 不存在则创建：从默认 logger 复制 sink 配置
    std::lock_guard<std::mutex> lock(s_LoggerMutex);

    // 再次检查（双重检查锁定模式）
    logger = spdlog::get(name);
    if (logger) return logger;

    if (s_DefaultLogger) {
        // 复制默认 logger 的 sinks 创建新 logger
        logger = std::make_shared<spdlog::async_logger>(
            name,
            s_DefaultLogger->sinks().begin(),
            s_DefaultLogger->sinks().end(),
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);
        logger->set_level(s_DefaultLogger->level());
        logger->flush_on(s_DefaultLogger->flush_level());
        spdlog::register_logger(logger);
    }

    return logger;
}

// ============================================================
// 日志级别控制
// ============================================================

void Log::SetLevel(Level level) {
    if (s_DefaultLogger) {
        s_DefaultLogger->set_level(static_cast<spdlog::level::level_enum>(level));
    }
}

void Log::SetLevel(const std::string& loggerName, Level level) {
    auto logger = spdlog::get(loggerName);
    if (logger) {
        logger->set_level(static_cast<spdlog::level::level_enum>(level));
    }
}

// ============================================================
// 刷新
// ============================================================

void Log::Flush() {
    if (s_DefaultLogger) {
        s_DefaultLogger->flush();
    }
}

void Log::ForceFlushAll() {
    // 1. 刷新默认 logger
    if (s_DefaultLogger) {
        s_DefaultLogger->flush();
    }

    // 2. 遍历所有已注册的 logger 并刷新
    // 对于 async_logger，flush() 内部会向线程池发送 async_msg_type::flush 消息，
    // 确保异步队列中所有待处理日志被写入对应的 sinks。
    spdlog::apply_all([](const std::shared_ptr<spdlog::logger>& logger) {
        logger->flush();
    });
}

void Log::ForceFlushAndDump(const std::string& dumpPath) {
    // 1. 先强制刷盘所有文件 Sink
    ForceFlushAll();

    // 2. 导出内存历史 Sink 的内容到文件
    if (s_MemorySink) {
        s_MemorySink->DumpToFile(dumpPath);
    }
}

std::shared_ptr<MemoryHistorySink> Log::GetMemoryHistorySink() {
    return s_MemorySink;
}

} // namespace Engine