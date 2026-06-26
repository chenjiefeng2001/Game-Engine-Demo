#pragma once

/**
 * @file ConsoleLog.h
 * @brief 环形缓冲区日志系统 — 轻量级控制台日志，支持 Info/Warn/Error 级别
 *
 * 使用方式：
 * @code
 *   LOG_INFO("Hello World");
 *   LOG_WARN("Something suspicious");
 *   LOG_ERROR("Something failed: " + std::to_string(code));
 * @endcode
 *
 * 配合 ConsolePanel 在 ImGui 中显示。
 *
 * ── 线程安全 ──
 * 非线程安全。预期仅在主线程使用。若需线程安全请在 Log() 内加锁。
 *
 * ── 架构说明 ──
 * 使用 Meyer's Singleton（static 局部变量）避免静态初始化顺序问题。
 * m_Buffer 使用 unique_ptr<LogEntry[]> 在堆上分配，避免全局/静态对象的
 * ASan redzone 保护与 MSVC std::string constexpr 初始化之间的冲突。
 * （全局 redzone 中的对象若包含 std::array<LogEntry,N> 且 LogEntry::message
 *  是 std::string，MSVC ASan 会在 `eh vector ctor iterator` 循环中误报
 *  global-buffer-overflow。）
 */

#include "Engine/Types.h"
#include <array>
#include <ctime>
#include <memory>
#include <string>

namespace Engine {

// ============================================================
// 日志级别
// ============================================================
enum class LogLevel : uint8 {
  Info = 0,
  Warn = 1,
  Error = 2,
  Command = 3, ///< 用户输入的命令回显
  COUNT
};

/// 日志级别的显示名称
inline const char *LogLevelName(LogLevel level) {
  switch (level) {
  case LogLevel::Info:
    return "INFO";
  case LogLevel::Warn:
    return "WARN";
  case LogLevel::Error:
    return "ERROR";
  case LogLevel::Command:
    return "CMD";
  default:
    return "????";
  }
}

/// 日志级别的 ImGui 颜色编码（ABGR）
inline uint32 LogLevelColor(LogLevel level) {
  switch (level) {
  case LogLevel::Info:
    return 0xFFAAAAAA; // 浅灰
  case LogLevel::Warn:
    return 0xFF00CCFF; // 橙黄
  case LogLevel::Error:
    return 0xFF3333FF; // 红
  case LogLevel::Command:
    return 0xFF88CC00; // 亮绿
  default:
    return 0xFFFFFFFF;
  }
}

// ============================================================
// 单条日志条目
// ============================================================
struct LogEntry {
  LogLevel level = LogLevel::Info;
  double timestamp = 0.0; ///< 自程序启动以来的秒数
  char message[1024]; ///< 日志内容（固定缓冲区，避开 std::string 的 ASan
                      ///< 初始化冲突）
};

// ============================================================
// 环形缓冲区日志系统（Meyer's 单例，堆分配缓冲区）
// ============================================================
class ConsoleLog {
public:
  /// 环形缓冲区容量
  static constexpr uint32 kBufferSize = 512;

  /// 获取单例引用（Meyer's Singleton）
  static ConsoleLog &Instance() {
    static ConsoleLog instance;
    return instance;
  }

  /// 写入一条日志
  void Log(LogLevel level, const std::string &message);

  /// 清空所有日志
  void Clear();

  // ── 数据访问（供 ConsolePanel 使用） ──

  /// 获取底层环形缓冲区指针
  const LogEntry *GetBuffer() const { return m_Buffer.get(); }

  /// 获取有效条目数（≤ kBufferSize）
  uint32 GetCount() const { return m_Count; }

  /// 获取最旧条目的索引（用于遍历）
  uint32 GetStartIndex() const { return m_StartIndex; }

  /// 获取缓冲区容量
  uint32 GetCapacity() const { return kBufferSize; }

  // ── 控制台日志持久化路径 ──

  /** 获取日志文件持久化路径（空字符串表示未启用） */
  const std::string &GetLogPath() const { return m_LogPath; }

  /** 设置日志文件持久化路径（空字符串=禁用持久化） */
  void SetLogPath(const std::string &path) { m_LogPath = path; }

private:
  ConsoleLog() : m_Buffer(new LogEntry[kBufferSize]) {
    // 堆分配的数组，避开全局 redzone 的 ASan 保护问题
  }
  ~ConsoleLog() = default;
  ConsoleLog(const ConsoleLog &) = delete;
  ConsoleLog &operator=(const ConsoleLog &) = delete;

  std::unique_ptr<LogEntry[]> m_Buffer; // 堆分配，避开全局 redzone
  uint32 m_StartIndex = 0;
  uint32 m_Count = 0;
  std::string m_LogPath;
};

// ── 编译时一致性断言 ──
static_assert(ConsoleLog::kBufferSize == 512,
              "ConsoleLog::kBufferSize must be exactly 512 across all "
              "translation units.");

} // namespace Engine

// ============================================================
// 便捷宏定义
// ============================================================

/// 记录一条 INFO 级别日志
#define LOG_INFO(msg)                                                          \
  Engine::ConsoleLog::Instance().Log(Engine::LogLevel::Info, (msg))

/// 记录一条 WARN 级别日志
#define LOG_WARN(msg)                                                          \
  Engine::ConsoleLog::Instance().Log(Engine::LogLevel::Warn, (msg))

/// 记录一条 ERROR 级别日志
#define LOG_ERROR(msg)                                                         \
  Engine::ConsoleLog::Instance().Log(Engine::LogLevel::Error, (msg))
