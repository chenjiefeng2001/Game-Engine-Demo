/**
 * @file Profiler.cpp
 * @brief Profiler 初始化与运行时控制
 *
 * 注意：TracyClient.cpp 已在 CMake 中作为独立编译单元加入，
 * 此处不重复包含，避免重复定义 Tracy 内部符号。
 */

#include "Engine/Profiler.h"
#include "Engine/Core/Log.h"


namespace Engine {

    void Profiler::Init() {
#if defined(ENGINE_ENABLE_PROFILING)
        PROFILE_SET_THREAD_NAME("MainThread");

        if (TracyIsConnected) {
            Log::Info("[Profiler] Tracy Profiler connected (port 8086)");
        } else {
            Log::Info("[Profiler] Tracy Profiler active — waiting for Tracy Server on port 8086");
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

    std::string Profiler::GetStatus() {
#if defined(ENGINE_ENABLE_PROFILING)
        if (TracyIsConnected) {
            return "^2Tracy Profiler: Connected^7  (port 8086)";
        }
        return "^3Tracy Profiler: Active^7  (run Tracy Server to capture)";
#else
        return "^9Profiler: Disabled^7  (build with Debug config)";
#endif
    }

} // namespace Engine
