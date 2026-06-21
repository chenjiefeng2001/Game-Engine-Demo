#pragma once

/**
 * @file Log.h
 * @brief 引擎日志系统 — 基于 spdlog 的高性能结构化日志
 *
 * ============================================================
 * 设计要点（基于方案重构）：
 *   - 基于 spdlog，使用异步模式（线程池 + 队列）
 *   - 每个 Category 对应一个 spdlog::logger 实例
 *   - 宏 ENGINE_LOG_*(Category, ...) 自动捕获源码位置
 *   - 内置内存环形缓冲区 Sink，崩溃时自动 Dump
 *   - 与 CrashHandler 深度集成：崩溃时强制刷盘所有 Sink
 * ============================================================
 *
 * 使用方式：
 * @code
 *   // 1. 引擎全局日志（Category = "Engine"）
 *   ENGINE_LOG_INFO("Window created {}x{}", 800, 600);
 *
 *   // 2. 按子系统日志
 *   ENGINE_LOG_INFO("Physics", "Step dt={:.6f}s", dt);
 *   ENGINE_LOG_WARN("Audio", "Source {} underrun", sourceId);
 *
 *   // 3. 获取 Logger 对象（避免宏开销，适合关键路径）
 *   auto log = Log::GetLogger("Physics");
 *   log.Info("Step dt={:.6f}s", dt);
 *
 *   // 4. 初始化
 *   Log::Init("logs/engine.log", Log::Level::Trace);
 *
 *   // 5. 级别控制
 *   Log::SetLevel("Physics", Log::Level::Warn);
 * @endcode
 *
 * 崩溃集成：
 *   CrashHandler::OnCrash() 中应调用：
 *     Log::ForceFlushAndDump("crashes/last_logs.txt");
 *   这会强制刷盘所有文件 Sink 并导出内存环形缓冲区。
 */

#include "Engine/Types.h"
#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/common.h>
#include <source_location>

namespace Engine {

// 前向声明
class Logger;

// ============================================================
// 内存环形缓冲区 Sink 的前向声明（用于 Dump）
// ============================================================
class MemoryHistorySink;

class Log {
public:
    // ── 日志级别（映射到 spdlog 级别） ──
    enum class Level : uint8 {
        Trace    = 0,
        Debug    = 1,
        Info     = 2,
        Warn     = 3,
        Error    = 4,
        Critical = 5,
        Off      = 6
    };

    // ── 生命周期 ──
    static void Init(const std::string& filePath   = "logs/engine.log",
                     Level consoleLevel = Level::Trace,
                     Level fileLevel    = Level::Debug);
    static void Shutdown();

    // ── 全局日志（直接调用，不捕获 source_loc） ──
    template<typename... Args>
    static void Trace(fmt::format_string<Args...> fmtStr, Args&&... args) {
        GetDefaultLogger()->trace(fmtStr, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void Debug(fmt::format_string<Args...> fmtStr, Args&&... args) {
        GetDefaultLogger()->debug(fmtStr, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void Info(fmt::format_string<Args...> fmtStr, Args&&... args) {
        GetDefaultLogger()->info(fmtStr, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void Warn(fmt::format_string<Args...> fmtStr, Args&&... args) {
        GetDefaultLogger()->warn(fmtStr, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void Error(fmt::format_string<Args...> fmtStr, Args&&... args) {
        GetDefaultLogger()->error(fmtStr, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void Critical(fmt::format_string<Args...> fmtStr, Args&&... args) {
        GetDefaultLogger()->critical(fmtStr, std::forward<Args>(args)...);
    }

    // ── 带 source_loc 的日志（由宏调用） ──

    template<typename... Args>
    static void TraceLoc(const std::source_location& loc,
                         fmt::format_string<Args...> fmtStr, Args&&... args) {
        GetDefaultLogger()->log(spdlog::source_loc{
            loc.file_name(), static_cast<int>(loc.line()), loc.function_name()},
            spdlog::level::trace, fmtStr, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void DebugLoc(const std::source_location& loc,
                         fmt::format_string<Args...> fmtStr, Args&&... args) {
        GetDefaultLogger()->log(spdlog::source_loc{
            loc.file_name(), static_cast<int>(loc.line()), loc.function_name()},
            spdlog::level::debug, fmtStr, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void InfoLoc(const std::source_location& loc,
                        fmt::format_string<Args...> fmtStr, Args&&... args) {
        GetDefaultLogger()->log(spdlog::source_loc{
            loc.file_name(), static_cast<int>(loc.line()), loc.function_name()},
            spdlog::level::info, fmtStr, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void WarnLoc(const std::source_location& loc,
                        fmt::format_string<Args...> fmtStr, Args&&... args) {
        GetDefaultLogger()->log(spdlog::source_loc{
            loc.file_name(), static_cast<int>(loc.line()), loc.function_name()},
            spdlog::level::warn, fmtStr, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void ErrorLoc(const std::source_location& loc,
                         fmt::format_string<Args...> fmtStr, Args&&... args) {
        GetDefaultLogger()->log(spdlog::source_loc{
            loc.file_name(), static_cast<int>(loc.line()), loc.function_name()},
            spdlog::level::err, fmtStr, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void CriticalLoc(const std::source_location& loc,
                            fmt::format_string<Args...> fmtStr, Args&&... args) {
        GetDefaultLogger()->log(spdlog::source_loc{
            loc.file_name(), static_cast<int>(loc.line()), loc.function_name()},
            spdlog::level::critical, fmtStr, std::forward<Args>(args)...);
    }

    // ── 子系统 Logger（带 source_loc） ──

    template<typename... Args>
    static void LogToCategory(const std::string& category,
                              spdlog::level::level_enum level,
                              const std::source_location& loc,
                              fmt::format_string<Args...> fmtStr, Args&&... args) {
        auto logger = Get(category);
        if (logger) {
            logger->log(spdlog::source_loc{
                loc.file_name(), static_cast<int>(loc.line()), loc.function_name()},
                level, fmtStr, std::forward<Args>(args)...);
        }
    }

    // ── 按子系统获取独立 Logger ──

    /**
     * @brief 获取或创建指定名称的子系统 Logger
     * @param name 子系统名称，如 "Physics"、"Audio"
     * @return spdlog::logger 指针
     */
    static std::shared_ptr<spdlog::logger> Get(const std::string& name);

    // ── 日志级别控制 ──

    static void SetLevel(Level level);
    static void SetLevel(const std::string& loggerName, Level level);

    // ── 状态查询 ──

    /** 检查日志系统是否已初始化 */
    static bool IsInitialized() { return s_Initialized; }

    // ── 刷新 ──

    /** 强制刷新默认 logger 到磁盘 */
    static void Flush();

    /**
     * @brief 强制刷新所有 logger 到磁盘（供 CrashHandler 使用）
     *
     * 遍历所有已注册的 logger，对每个 logger 调用 flush()。
     * 同时调用 spdlog::details::thread_pool 的 flush_all() 确保
     * 异步队列中的待处理日志全部写入。
     */
    static void ForceFlushAll();

    /**
     * @brief 强制刷盘并导出内存历史缓冲区（供 CrashHandler 使用）
     * @param dumpPath 导出的文件路径
     *
     * 在崩溃时调用此函数：
     * 1. 强制刷新所有文件 Sink
     * 2. 导出 MemoryHistorySink 的环形缓冲区内容到文件
     */
    static void ForceFlushAndDump(const std::string& dumpPath);

    // ── 获取模块 Logger（不暴露 spdlog 类型） ──

    static Logger GetLogger(const std::string& name);

    // ── 内部 ──
    static std::shared_ptr<spdlog::logger> GetDefaultLogger();
    static std::shared_ptr<MemoryHistorySink> GetMemoryHistorySink();

private:
    static bool s_Initialized;
    static std::shared_ptr<spdlog::logger> s_DefaultLogger;
    static std::shared_ptr<MemoryHistorySink> s_MemorySink;
};

// ============================================================
// Logger — 轻量日志中间层
// ============================================================

class Logger {
public:
    Logger() = default;
    explicit Logger(const std::string& name) : m_Name(name) {}

    template<typename... Args>
    void Trace(fmt::format_string<Args...> fmt, Args&&... args) {
        EnsureLogger();
        if (m_Logger) m_Logger->trace(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Debug(fmt::format_string<Args...> fmt, Args&&... args) {
        EnsureLogger();
        if (m_Logger) m_Logger->debug(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Info(fmt::format_string<Args...> fmt, Args&&... args) {
        EnsureLogger();
        if (m_Logger) m_Logger->info(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Warn(fmt::format_string<Args...> fmt, Args&&... args) {
        EnsureLogger();
        if (m_Logger) m_Logger->warn(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Error(fmt::format_string<Args...> fmt, Args&&... args) {
        EnsureLogger();
        if (m_Logger) m_Logger->error(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Critical(fmt::format_string<Args...> fmt, Args&&... args) {
        EnsureLogger();
        if (m_Logger) m_Logger->critical(fmt, std::forward<Args>(args)...);
    }

    void SetLevel(Log::Level level) {
        EnsureLogger();
        if (m_Logger)
            m_Logger->set_level(static_cast<spdlog::level::level_enum>(level));
    }

    bool IsValid() const { return m_Logger != nullptr || !m_Name.empty(); }
    const std::string& GetName() const { return m_Name; }

private:
    void EnsureLogger() {
        if (!m_Logger && !m_Name.empty()) {
            m_Logger = Log::Get(m_Name);
        }
    }

    std::string m_Name;
    std::shared_ptr<spdlog::logger> m_Logger;
};

} // namespace Engine

// ============================================================
// ENGINE_LOG 宏 — 引擎级结构化日志（自动捕获源码位置）
//
// 设计原则：
//   - Category 是显式必选的第一参数，例如 "Physics"、"Audio"
//   - 内部通过 spdlog::source_loc 传递文件名、行号和函数名
//   - 使用 do { ... } while(0) 确保宏语法安全
//
// 使用方式：
//   // 指定 Category
//   ENGINE_LOG_INFO("Renderer", "Created shader: {}", path);
//   ENGINE_LOG_WARN("Audio",    "Source {} underrun", srcId);
//   ENGINE_LOG_ERROR("Physics", "Body {} out of bounds", bodyId);
// ============================================================

#define ENGINE_LOG_TRACE(category, ...) \
    do { \
        if (auto _l_ = ::Engine::Log::Get(category)) { \
            auto _loc_ = std::source_location::current(); \
            _l_->log(spdlog::source_loc{_loc_.file_name(), static_cast<int>(_loc_.line()), _loc_.function_name()}, \
                     spdlog::level::trace, __VA_ARGS__); \
        } \
    } while (0)

#define ENGINE_LOG_DEBUG(category, ...) \
    do { \
        if (auto _l_ = ::Engine::Log::Get(category)) { \
            auto _loc_ = std::source_location::current(); \
            _l_->log(spdlog::source_loc{_loc_.file_name(), static_cast<int>(_loc_.line()), _loc_.function_name()}, \
                     spdlog::level::debug, __VA_ARGS__); \
        } \
    } while (0)

#define ENGINE_LOG_INFO(category, ...) \
    do { \
        if (auto _l_ = ::Engine::Log::Get(category)) { \
            auto _loc_ = std::source_location::current(); \
            _l_->log(spdlog::source_loc{_loc_.file_name(), static_cast<int>(_loc_.line()), _loc_.function_name()}, \
                     spdlog::level::info, __VA_ARGS__); \
        } \
    } while (0)

#define ENGINE_LOG_WARN(category, ...) \
    do { \
        if (auto _l_ = ::Engine::Log::Get(category)) { \
            auto _loc_ = std::source_location::current(); \
            _l_->log(spdlog::source_loc{_loc_.file_name(), static_cast<int>(_loc_.line()), _loc_.function_name()}, \
                     spdlog::level::warn, __VA_ARGS__); \
        } \
    } while (0)

#define ENGINE_LOG_ERROR(category, ...) \
    do { \
        if (auto _l_ = ::Engine::Log::Get(category)) { \
            auto _loc_ = std::source_location::current(); \
            _l_->log(spdlog::source_loc{_loc_.file_name(), static_cast<int>(_loc_.line()), _loc_.function_name()}, \
                     spdlog::level::err, __VA_ARGS__); \
        } \
    } while (0)

#define ENGINE_LOG_CRITICAL(category, ...) \
    do { \
        if (auto _l_ = ::Engine::Log::Get(category)) { \
            auto _loc_ = std::source_location::current(); \
            _l_->log(spdlog::source_loc{_loc_.file_name(), static_cast<int>(_loc_.line()), _loc_.function_name()}, \
                     spdlog::level::critical, __VA_ARGS__); \
        } \
    } while (0)
