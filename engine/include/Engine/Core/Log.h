#pragma once

/**
 * @file Log.h
 * @brief 引擎日志系统 — 基于 spdlog 的结构化日志
 *
 * 使用方式：
 * @code
 *   // 基本日志（自动带 [Engine] 标签）
 *   Log::Info("Window created {}x{}", 800, 600);
 *   Log::Warn("Texture '{}' not found, using fallback", path);
 *   Log::Error("Failed to load shader: {}", error);
 *
 *   // 按子系统标签
 *   Log::Get("Physics")->Info("Step dt={:.6f}s", dt);
 *   Log::Get("Audio" )->Warn("Source {} underrun", sourceId);
 *
 *   // 初始化时设置日志级别和输出
 *   Log::Init("logs/engine.log", Log::Level::Trace);
 *
 *   // 在作用域中自动缩进
 *   {
 *       Log::ScopeIndent _("Resource loading:");
 *       Log::Info("Texture: {}", path);
 *       Log::Info("Shader: {}", shaderPath);
 *   }  // 自动取消缩进
 * @endcode
 *
 * 设计原则：
 *   - 基于 spdlog，利用其异步写入、多线程安全、文件轮转能力
 *   - 引擎代码统一调用 Log::Info(...)，不直接使用 spdlog API
 *   - 可逐个子系统独立设置日志级别
 *   - Debug 模式下自动附加源码文件 + 行号
 */

#include "Engine/Types.h"
#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

namespace Engine {

// 前向声明（Log::GetLogger 返回 Logger 值类型）
class Logger;

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
    static void Init(const std::string& filePath = "logs/engine.log",
                     Level consoleLevel = Level::Trace,
                     Level fileLevel    = Level::Debug);
    static void Shutdown();

    // ── 全局日志（使用 fmt::format_string 支持 C++20 编译期格式检查） ──

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

    // ── 按子系统获取独立 Logger ──

    /**
     * @brief 获取或创建指定名称的子系统 Logger
     * @param name 子系统名称，如 "Physics"、"Audio"
     * @return spdlog::logger 指针
     *
     * 首次调用时自动创建，后续复用。
     * 每个 Logger 可独立设置日志级别：
     * @code
     *   Log::Get("Physics")->set_level(spdlog::level::warn);
     * @endcode
     */
    static std::shared_ptr<spdlog::logger> Get(const std::string& name);

    // ── 便捷宏（自动包含文件名和行号） ──

    // 非关键路径使用宏（自动附加源码位置）
    // 关键路径直接用 Log::Info(...) 避免宏开销

    // ── 日志级别控制 ──

    static void SetLevel(Level level);
    static void SetLevel(const std::string& loggerName, Level level);

    // ── 刷新 ──

    /** 强制刷新所有日志到磁盘 */
    static void Flush();

    // ── 获取模块 Logger（不暴露 spdlog 类型） ──

    /**
     * @brief 获取或创建指定名称的模块 Logger
     * @param name 模块名称，如 "ResourceManager"、"Audio"
     * @return Logger 值对象（轻量，可安全复制）
     *
     * @code
     *   auto log = Log::GetLogger("Physics");
     *   log.Info("Step dt={:.6f}s", dt);
     *   log.Warn("Body {} out of bounds", bodyId);
     * @endcode
     */
    static Logger GetLogger(const std::string& name);

    // ── 内部 ──
    static std::shared_ptr<spdlog::logger> GetDefaultLogger();

private:
    static bool s_Initialized;
    static std::shared_ptr<spdlog::logger> s_DefaultLogger;
};

// ============================================================
// Logger — 轻量日志中间层，不暴露 spdlog 类型
//
// 设计要点：
//   - 值类型，可安全复制、移动
//   - 懒初始化：构造时不访问 spdlog，首次输出时才获取 logger
//   - 因此可在静态初始化阶段安全创建（如文件作用域 static Logger）
// ============================================================

class Logger {
public:
    Logger() = default;

    /// 按名称创建 Logger（懒初始化）
    explicit Logger(const std::string& name) : m_Name(name) {}

    // ── 日志输出 ──

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

    // ── 控制 ──

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
