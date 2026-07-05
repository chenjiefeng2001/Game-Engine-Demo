#pragma once

/**
 * @file Profiler.h
 * @brief 引擎 Profiler 包装层 — 基于 Tracy Profiler 的零开销性能追踪
 *
 * 设计原则：
 *   - 当 ENGINE_ENABLE_PROFILING 定义时，宏展开为 Tracy 调用
 *   - 否则全部展开为空，零开销
 *   - 未来可无缝切换为其他 profiler 后端
 *
 * 使用方式：
 * @code
 *   void Physics::Step(float32 dt) {
 *       PROFILE_ZONE();            // 自动使用函数名
 *       PROFILE_ZONE_TEXT("dt=%f", dt); // 附加文本
 *       // ...
 *   }
 *
 *   // 每帧标记
 *   void Run() {
 *       while (running) {
 *           PROFILE_FRAME("MainLoop");
 *           // ...
 *       }
 *   }
 *
 *   // 内存追踪
 *   void* ptr = malloc(1024);
 *   PROFILE_MALLOC(ptr, 1024);
 * @endcode
 *
 * 构建要求：
 *   - 定义 ENGINE_ENABLE_PROFILING（Debug 默认开启）
 *   - 链接 tracy 库（编译 TracyClient.cpp）
 *   - 运行时可连接 Tracy Server（默认端口 8086）
 */

#include "Engine/Types.h"
#include <cstdint>

// ============================================================
// 条件编译开关
// ============================================================

#if defined(ENGINE_ENABLE_PROFILING)

// ── 禁止 Tracy ETW 系统跟踪 ──
// ETW 采样在 Windows 上会导致堆栈 Cookie 检测溢出崩溃。
// 禁用后不影响 Zone/Frame/Memory/GPU 等核心 profiling 功能。
#ifndef TRACY_NO_SYSTEM_TRACING
#define TRACY_NO_SYSTEM_TRACING
#endif

// ── 包含 Tracy 头 ──
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>

// ============================================================
// Zone 标记宏
// ============================================================

/// 自动以函数名创建 profiling zone
#define PROFILE_ZONE()             ZoneScoped

/// 命名 zone
#define PROFILE_ZONE_NAME(name)    ZoneScopedN(name)

/// 给当前 zone 附加文本（必须位于 zone 作用域内）
#define PROFILE_ZONE_TEXT(text) \
    ZoneText(text, strlen(text))

/// 给当前 zone 附加数值
#define PROFILE_ZONE_VALUE(val) \
    ZoneValue(val)

// ============================================================
// 帧标记宏
// ============================================================

/// 标记帧结束/开始（放在主循环末尾）
#define PROFILE_FRAME(name)        FrameMark

/// 标记指定名称的帧（多帧上下文）
#define PROFILE_FRAME_NAMED(name)  FrameMarkNamed(name)

/// 标记帧开始（替代 PROFILE_FRAME 的另一种方式）
#define PROFILE_FRAME_START        FrameMarkStart("MainLoop")
#define PROFILE_FRAME_END          FrameMarkEnd("MainLoop")

// ============================================================
// 内存追踪宏
// ============================================================

/// 追踪内存分配
#define PROFILE_MALLOC(ptr, size)  TracyAlloc(ptr, size)

/// 追踪内存释放
#define PROFILE_FREE(ptr)          TracyFree(ptr)

// ============================================================
// GPU 追踪（OpenGL）
// ============================================================

/// GPU context 初始化（在 OpenGL 上下文创建后调用一次）
#define PROFILE_GPU_CONTEXT        TracyGpuContext

/// GPU zone 标记（需要 OpenGL 上下文）
#define PROFILE_GPU_ZONE(name)     TracyGpuZone(name)

/// GPU 帧标记（在每帧渲染后调用，收集 GPU 查询结果）
#define PROFILE_GPU_COLLECT        TracyGpuCollect

// ============================================================
// 线程命名（在工作线程入口调用）
// ============================================================

#define PROFILE_SET_THREAD_NAME(name) \
    tracy::SetThreadName(name)

// ============================================================
// 运行时控制（可由控制台命令调用）
// ============================================================

/// 是否已连接到 Tracy Server
#define PROFILE_IS_CONNECTED()     TracyIsConnected

#else // !ENGINE_ENABLE_PROFILING

// ── 全部展开为空，零开销 ──

#define PROFILE_ZONE()                    ((void)0)
#define PROFILE_ZONE_NAME(name)           ((void)0)
#define PROFILE_ZONE_TEXT(text)           ((void)0)
#define PROFILE_ZONE_VALUE(val)           ((void)0)
#define PROFILE_FRAME(name)               ((void)0)
#define PROFILE_FRAME_NAMED(name)         ((void)0)
#define PROFILE_FRAME_START               ((void)0)
#define PROFILE_FRAME_END                 ((void)0)
#define PROFILE_MALLOC(ptr, size)         ((void)0)
#define PROFILE_FREE(ptr)                 ((void)0)
#define PROFILE_GPU_CONTEXT               ((void)0)
#define PROFILE_GPU_ZONE(name)            ((void)0)
#define PROFILE_GPU_COLLECT               ((void)0)
#define PROFILE_SET_THREAD_NAME(name)     ((void)0)
#define PROFILE_IS_CONNECTED()            false

#endif // ENGINE_ENABLE_PROFILING

// ============================================================
// Profiler 运行时控制 API（始终可用，无论是否启用 profiling）
// ============================================================

namespace Engine {

    /// Profiler 管理器 — 控制台集成 + 运行时状态
    class Profiler {
    public:
        /** 初始化 profiler：线程命名 + 日志 */
        static void Init();

        /// 是否已连接到 Tracy Server
        static bool IsConnected();

        /// 启用/禁用 profiling（仅在连接后有效）
        static void SetEnabled(bool enabled);

        /// 获取 profiling 状态（单行简要）
        static std::string GetStatus();

        /// 获取详细状态报告（多行，含操作指引）
        static std::string GetDetailedStatus();

        /// 获取控制台使用帮助
        static std::string GetHelp();

        /** 尝试启动 Tracy Server（如果路径已配置） */
        static bool LaunchServer();
    };

} // namespace Engine
