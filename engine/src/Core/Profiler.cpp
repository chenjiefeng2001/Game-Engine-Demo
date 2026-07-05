/**
 * @file Profiler.cpp
 * @brief Profiler 初始化与运行时控制
 *
 * Tracy 工作原理：
 *   游戏（客户端）通过 UDP 8086 端口广播 profiling 数据。
 *   Tracy Server（桌面 GUI 程序）自动发现并连接客户端。
 *   无需手动"连接"——只要 Server 在运行，客户端即自动配对。
 */

#include "Engine/Profiler.h"
#include "Engine/Core/Log.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace Engine {

    void Profiler::Init() {
#if defined(ENGINE_ENABLE_PROFILING)
        PROFILE_SET_THREAD_NAME("MainThread");

        if (TracyIsConnected) {
            Log::Info("[Profiler] Tracy Profiler connected on port 8086");
        } else {
            Log::Info("[Profiler] Tracy Profiler active — broadcast on UDP 8086");
            Log::Info("[Profiler] Run Tracy Server GUI to capture profiling data");
        }
#else
        Log::Info("[Profiler] Profiler disabled — build with Debug config to enable");
#endif
    }

    bool Profiler::IsConnected() {
#if defined(ENGINE_ENABLE_PROFILING)
        return TracyIsConnected;
#else
        return false;
#endif
    }

    void Profiler::SetEnabled(bool) {
        // Tracy 运行时启停由 Server 控制，客户端无需操作
    }

    // ── 简要状态 ──

    std::string Profiler::GetStatus() {
#if defined(ENGINE_ENABLE_PROFILING)
        if (TracyIsConnected) {
            return "^2Tracy Profiler: Connected^7  (UDP port 8086)";
        }
        return "^3Tracy Profiler: Active^7  (broadcasting on UDP 8086)";
#else
        return "^9Profiler: Disabled^7  (needs Debug build)";
#endif
    }

    // ── 详细状态 (供控制台 profiler 命令) ──

    std::string Profiler::GetDetailedStatus() {
        std::string out;
#if defined(ENGINE_ENABLE_PROFILING)
        out += "^5=== Tracy Profiler ===^7\n";
        out += "  Build:         ^2ENABLED^7 (Debug)\n";
        out += "  Port:          ^5UDP 8086^7\n";

        if (TracyIsConnected) {
            out += "  Status:        ^2CONNECTED^7  ~ Tracy Server is capturing\n";
            out += "\n^3Tip:^7 Use ^5FrameMark^7 zones to analyze performance.\n";
            out += "       Commands: ^5profiler help^7 for more details.";
        } else {
            out += "  Status:        ^3BROADCASTING^7  ~ waiting for Server...\n";
            out += "\n^5To connect:^7\n";
            out += "  1. Download & run Tracy Server GUI\n";
            out += "  2. Server will auto-discover this game on localhost\n";
            out += "  3. Click the game entry in Tracy Server to start tracing\n";
            out += "\n  Tracy Server download:\n";
            out += "    https://github.com/wolfpld/tracy/releases/tag/v0.13.1\n";
        }
#else
        out += "^5=== Tracy Profiler ===^7\n";
        out += "  Build:         ^9DISABLED^7 (Release/Non-Debug)\n";
        out += "  To enable:     Build in Debug mode (CMake auto-defines\n";
        out += "                 ENGINE_ENABLE_PROFILING + TRACY_ENABLE)\n";
#endif
        return out;
    }

    // ── 使用帮助 ──

    std::string Profiler::GetHelp() {
        return
            "^5=== Profiler Console Commands ===^7\n"
            "  ^2profiler^7             Show full status + guide\n"
            "  ^2profiler status^7      Quick connection check\n"
            "  ^2profiler help^7        Show this help\n"
            "  ^2profiler server^7      Try to launch Tracy Server\n"
            "\n"
            "^5=== Code Macros ===^7\n"
            "  PROFILE_ZONE()            Per-function timing\n"
            "  PROFILE_ZONE_NAME(name)   Named zone\n"
            "  PROFILE_FRAME(name)       Frame boundary marker\n"
            "  PROFILE_SET_THREAD_NAME   Thread label\n"
            "\n"
            "^5=== Workflow ===^7\n"
            "  1. Start Tracy Server GUI (separate process)\n"
            "  2. Run this game (Debug build)\n"
            "  3. Game auto-broadcasts → Server discovers it\n"
            "  4. Click connect in Server to capture\n"
            "  5. View Flame Graph / Zone timing / Memory stats\n";
    }

    // ── 尝试启动 Server ──

    bool Profiler::LaunchServer() {
#ifdef _WIN32
        // 查找可能的 Tracy Server 路径
        const char* candidates[] = {
            "tracy\\profiler\\build\\win32\\Release\\Tracy.exe",
            "build\\tracy-server\\Release\\tracy-profiler.exe",
            "build\\tracy-server\\tracy-profiler.exe",
        };

        for (auto& path : candidates) {
            // 尝试从项目根目录拼接
            std::string fullPath = std::string("..\\..\\..\\") + path;
            HINSTANCE result = ShellExecuteA(nullptr, "open", fullPath.c_str(),
                                             nullptr, nullptr, SW_SHOWNORMAL);
            if ((intptr_t)result > 32) {
                return true;
            }

            // 尝试直接路径
            result = ShellExecuteA(nullptr, "open", path, nullptr, nullptr, SW_SHOWNORMAL);
            if ((intptr_t)result > 32) {
                return true;
            }
        }
#endif
        return false;
    }

} // namespace Engine
