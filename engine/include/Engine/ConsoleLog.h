#pragma once

/**
 * @file ConsoleLog.h
 * @brief LOG_* 宏兼容垫片（GP1-DX S4a 日志单缓冲收敛）
 *
 * 原本这里是一套独立的 512 槽环形缓冲（ConsoleLog 单例）：
 *   - 全仓零读者（GetBuffer/GetCount/GetStartIndex 从未被 UI 渲染）
 *   - SetLogPath 无任何落盘实现（虚假功能）
 *   - 与 ConsolePanel 私有缓冲构成双缓冲分叉，且宏日志绕过 spdlog
 *     导致永不上屏（docs/GP1-DX-State-Layer-Analysis.md §1.3）
 *
 * 现状：LOG_* 宏改道 spdlog 默认 logger → PanelBridgeSink → ConsolePanel，
 * 全进程唯一日志显示面。头文件路径与宏签名保持不变，既有调用点零改动。
 *
 * 线程安全：spdlog logger 自身线程安全；异步模式下写入经后台线程池。
 */

#include "Engine/Types.h"
#include "Engine/Core/Log.h"
#include <spdlog/spdlog.h>
#include <string>

namespace Engine {

// 日志级别枚举保留（历史 API 面兼容；新代码请直接用 spdlog/ENGINE_LOG_*）
enum class LogLevel : uint8 {
  Info = 0,
  Warn = 1,
  Error = 2,
  Command = 3, ///< 用户输入的命令回显
  COUNT
};

inline const char *LogLevelName(LogLevel level) {
  switch (level) {
  case LogLevel::Info:   return "INFO";
  case LogLevel::Warn:   return "WARN";
  case LogLevel::Error:  return "ERROR";
  case LogLevel::Command:return "CMD";
  default:               return "????";
  }
}

inline uint32 LogLevelColor(LogLevel level) {
  switch (level) {
  case LogLevel::Info:   return 0xFFAAAAAA;
  case LogLevel::Warn:   return 0xFF00CCFF;
  case LogLevel::Error:  return 0xFF3333FF;
  case LogLevel::Command:return 0xFF88CC00;
  default:               return 0xFFFFFFFF;
  }
}

} // namespace Engine

// ============================================================
// 便捷宏 —— 统一走 spdlog 默认 logger（"{}" 包裹避免消息内花括号
// 被当作 fmt 占位符解析）
// ============================================================

#define LOG_INFO(msg)                                                          \
  do {                                                                         \
    if (auto _cl_ = ::Engine::Log::GetDefaultLogger())                         \
      _cl_->log(spdlog::level::info, "{}", (msg));                             \
  } while (0)

#define LOG_WARN(msg)                                                          \
  do {                                                                         \
    if (auto _cl_ = ::Engine::Log::GetDefaultLogger())                         \
      _cl_->log(spdlog::level::warn, "{}", (msg));                             \
  } while (0)

#define LOG_ERROR(msg)                                                         \
  do {                                                                         \
    if (auto _cl_ = ::Engine::Log::GetDefaultLogger())                         \
      _cl_->log(spdlog::level::err, "{}", (msg));                              \
  } while (0)
