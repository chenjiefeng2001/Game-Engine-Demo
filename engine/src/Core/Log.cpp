/**
 * @file Log.cpp
 * @brief 引擎日志系统实现 — 包装 spdlog
 *
 * 初始化时创建两个 sink：
 *   - 控制台 sink（带颜色，stdout）
 *   - 文件 sink（轮转文件，按大小切分）
 *
 * 全局 Log::Info(...) 使用默认 logger（标签为 "Engine"）。
 * Log::Get("Subsystem") 返回或创建指定名称的 logger。
 */

#include "Engine/Core/Log.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/async.h>
#include <memory>
#include <mutex>

namespace Engine {

// ============================================================
// 静态成员
// ============================================================

bool Log::s_Initialized = false;
std::shared_ptr<spdlog::logger> Log::s_DefaultLogger = nullptr;
static std::mutex s_LoggerMutex;

// ============================================================
// 生命周期
// ============================================================

void Log::Init(const std::string& filePath, Level consoleLevel, Level fileLevel) {
    if (s_Initialized) return;

    try {
        // ── 创建控制台 sink（带颜色） ──
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(static_cast<spdlog::level::level_enum>(consoleLevel));
        consoleSink->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");

        // ── 创建文件 sink（轮转，最大 5MB，保留 3 个备份） ──
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            filePath, 5 * 1024 * 1024, 3);
        fileSink->set_level(static_cast<spdlog::level::level_enum>(fileLevel));
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v");

        // ── 创建默认 logger（标签 "Engine"） ──
        s_DefaultLogger = std::make_shared<spdlog::logger>(
            "Engine", spdlog::sinks_init_list{consoleSink, fileSink});
        s_DefaultLogger->set_level(spdlog::level::trace);
        s_DefaultLogger->flush_on(spdlog::level::warn);  // warn 及以上立即刷盘

        // 注册为 spdlog 默认 logger
        spdlog::set_default_logger(s_DefaultLogger);

        s_Initialized = true;

        Info("Log system initialized -> {}", filePath);

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "[Log] Initialization failed: " << ex.what() << std::endl;
    }
}

void Log::Shutdown() {
    if (!s_Initialized) return;

    Info("Log system shutting down...");
    s_DefaultLogger.reset();
    spdlog::shutdown();
    s_Initialized = false;
}

// ============================================================
// 获取 Logger
// ============================================================

std::shared_ptr<spdlog::logger> Log::GetDefaultLogger() {
    if (!s_DefaultLogger) {
        // 懒初始化：如果用户未调用 Init，使用默认配置
        Init();
    }
    return s_DefaultLogger;
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
        logger = std::make_shared<spdlog::logger>(name,
            s_DefaultLogger->sinks().begin(),
            s_DefaultLogger->sinks().end());
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
    spdlog::default_logger_raw()->flush();
}

} // namespace Engine
