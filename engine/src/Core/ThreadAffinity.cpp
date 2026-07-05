/**
 * @file ThreadAffinity.cpp
 * @brief 线程仿射平台实现 — Windows / Linux / macOS
 */

#include "Engine/Core/ThreadAffinity.h"
#include <thread>

// ═══════════════════════════════════════════════════════════
// Windows 实现
// ═══════════════════════════════════════════════════════════
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Engine {
namespace Threading {

    static thread_local ThreadCategory s_CurrentCategory = ThreadCategory::Worker;

    uint32 TopologyInfo::GetCoreCount() {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        return static_cast<uint32>(sysInfo.dwNumberOfProcessors);
    }

    uint32 TopologyInfo::GetPECoreCount() {
        // Windows: 使用 GetLogicalProcessorInformationEx 查询性能核数量
        // RelationProcessorCore 返回的物理核心数（不含 HT），EfficiencyClass 区分 P/E
        DWORD bufferSize = 0;
        GetLogicalProcessorInformationEx(RelationAll, nullptr, &bufferSize);
        if (bufferSize == 0) return GetCoreCount();

        std::vector<uint8> buffer(bufferSize);
        if (!GetLogicalProcessorInformationEx(RelationAll,
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
                &bufferSize)) {
            return GetCoreCount();
        }

        uint32 peCount = 0;
        uint8* ptr = buffer.data();
        uint8* end = ptr + bufferSize;

        while (ptr < end) {
            auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
            if (info->Relationship == RelationProcessorCore) {
                // EfficiencyClass 1 = P-Core, 2+ = E-Core (Intel)
                if (info->Processor.EfficiencyClass == 1 ||
                    info->Processor.EfficiencyClass == 0) {
                    peCount++;
                }
            }
            if (info->Size == 0) break;
            ptr += info->Size;
        }

        return peCount > 0 ? peCount : GetCoreCount();
    }

    uint32 TopologyInfo::GetECoreCount() {
        DWORD bufferSize = 0;
        GetLogicalProcessorInformationEx(RelationAll, nullptr, &bufferSize);
        if (bufferSize == 0) return 0;

        std::vector<uint8> buffer(bufferSize);
        if (!GetLogicalProcessorInformationEx(RelationAll,
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
                &bufferSize)) {
            return 0;
        }

        uint32 eCount = 0;
        uint8* ptr = buffer.data();
        uint8* end = ptr + bufferSize;

        while (ptr < end) {
            auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
            if (info->Relationship == RelationProcessorCore) {
                // EfficiencyClass >= 2 = E-Core (Intel)
                if (info->Processor.EfficiencyClass >= 2) {
                    eCount++;
                }
            }
            if (info->Size == 0) break;
            ptr += info->Size;
        }

        return eCount;
    }

    bool TopologyInfo::IsHybridArchitecture() {
        return GetECoreCount() > 0;
    }

    uint32 TopologyInfo::GetLLCCacheSize() {
        DWORD bufferSize = 0;
        GetLogicalProcessorInformationEx(RelationCache, nullptr, &bufferSize);
        if (bufferSize == 0) return 0;

        std::vector<uint8> buffer(bufferSize);
        if (!GetLogicalProcessorInformationEx(RelationCache,
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
                &bufferSize)) {
            return 0;
        }

        uint32 maxCache = 0;
        uint8* ptr = buffer.data();
        uint8* end = ptr + bufferSize;

        while (ptr < end) {
            auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
            if (info->Relationship == RelationCache &&
                info->Cache.Type == CacheUnified) {
                if (info->Cache.CacheSize > maxCache)
                    maxCache = static_cast<uint32>(info->Cache.CacheSize);
            }
            if (info->Size == 0) break;
            ptr += info->Size;
        }
        return maxCache;
    }

    // ── 仿射 API ──

    bool SetThreadAffinity(std::thread::native_handle_type handle, uint32 coreIndex) {
        DWORD_PTR mask = 1ULL << coreIndex;
        DWORD_PTR result = ::SetThreadAffinityMask(
            reinterpret_cast<HANDLE>(handle), mask);
        return result != 0;
    }

    bool SetThreadAffinityMask(std::thread::native_handle_type handle, uint64 coreMask) {
        DWORD_PTR result = ::SetThreadAffinityMask(
            reinterpret_cast<HANDLE>(handle), static_cast<DWORD_PTR>(coreMask));
        return result != 0;
    }

    void SetThreadIdealProcessor(uint32 coreIndex) {
        ::SetThreadIdealProcessor(GetCurrentThread(), static_cast<DWORD>(coreIndex));
    }

    // SEH 块必须与非展开对象隔离（C2712 workaround）
    static void SetThreadNameSEH(const char* name) {
        #pragma pack(push, 8)
        struct THREADNAME_INFO {
            DWORD  dwType;
            LPCSTR szName;
            DWORD  dwThreadID;
            DWORD  dwFlags;
        };
        #pragma pack(pop)

        THREADNAME_INFO info{};
        info.dwType = 0x1000;
        info.szName = name;
        info.dwThreadID = static_cast<DWORD>(-1);
        info.dwFlags = 0;

        __try {
            RaiseException(0x406D1388, 0,
                sizeof(info) / sizeof(ULONG_PTR),
                reinterpret_cast<ULONG_PTR*>(&info));
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void SetThreadName(const char* name) {
        // Wide-char 线程名称（Visual Studio / WinDbg 可见）
        int len = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
        if (len > 0) {
            std::wstring wideName(len, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, name, -1, wideName.data(), len);
            SetThreadDescription(GetCurrentThread(), wideName.c_str());
        }

        // 传统异常命名（旧版 VS 调试器兼容，独立函数隔离 SEH）
        SetThreadNameSEH(name);
    }

    /** 获取当前线程的原生句柄（跨平台） */
    std::thread::native_handle_type GetCurrentThreadHandle() {
        return GetCurrentThread();
    }

    ThreadCategory GetCurrentThreadCategory() noexcept {
        return s_CurrentCategory;
    }

    void SetCurrentThreadCategory(ThreadCategory cat) noexcept {
        s_CurrentCategory = cat;
    }

    uint32 GetRecommendedCore(ThreadCategory cat, uint32 workerIndex) {
        uint32 totalCores = TopologyInfo::GetCoreCount();
        uint32 peCount = TopologyInfo::GetPECoreCount();

        switch (cat) {
            case ThreadCategory::Main:
                return 0;
            case ThreadCategory::Render:
                return (totalCores > 1) ? 1 : 0;
            case ThreadCategory::Worker: {
                uint32 base = 2;
                if (peCount > 2) {
                    return (base + workerIndex) % peCount;
                }
                return (base + workerIndex) % totalCores;
            }
            case ThreadCategory::IO: {
                uint32 eCount = TopologyInfo::GetECoreCount();
                if (eCount > 0 && peCount > 0) {
                    return peCount + (workerIndex % eCount);
                }
                return workerIndex % totalCores;
            }
            default:
                return workerIndex % totalCores;
        }
    }

} // namespace Threading
} // namespace Engine

// ═══════════════════════════════════════════════════════════
// Linux 实现
// ═══════════════════════════════════════════════════════════
#elif defined(__linux__)
#include <pthread.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <string>
#include <vector>
#include <cstring>

namespace Engine {
namespace Threading {

    static thread_local ThreadCategory s_CurrentCategory = ThreadCategory::Worker;

    uint32 TopologyInfo::GetCoreCount() {
        int count = get_nprocs_conf();
        return static_cast<uint32>(count > 0 ? count : 1);
    }

    uint32 TopologyInfo::GetPECoreCount() {
        // Linux 无统一的 P/E 检测 API，返回总核心数
        return GetCoreCount();
    }

    uint32 TopologyInfo::GetECoreCount() {
        return 0;
    }

    bool TopologyInfo::IsHybridArchitecture() {
        return false;
    }

    uint32 TopologyInfo::GetLLCCacheSize() {
        // 从 /sys/devices/system/cpu/cpu0/cache/index3/size 读取
        FILE* f = fopen("/sys/devices/system/cpu/cpu0/cache/index3/size", "r");
        if (!f) return 0;
        char buf[32] = {};
        fgets(buf, sizeof(buf), f);
        fclose(f);
        // 解析 "8192K" 或 "8M" 格式
        uint32 kb = 0;
        if (strchr(buf, 'M')) {
            kb = static_cast<uint32>(atoi(buf) * 1024);
        } else {
            kb = static_cast<uint32>(atoi(buf));
        }
        return kb * 1024;
    }

    bool SetThreadAffinity(std::thread::native_handle_type handle, uint32 coreIndex) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(coreIndex, &cpuset);
        return pthread_setaffinity_np(handle, sizeof(cpu_set_t), &cpuset) == 0;
    }

    bool SetThreadAffinityMask(std::thread::native_handle_type handle, uint64 coreMask) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        for (uint32 i = 0; i < 64 && i < CPU_SETSIZE; ++i) {
            if (coreMask & (1ULL << i))
                CPU_SET(i, &cpuset);
        }
        return pthread_setaffinity_np(handle, sizeof(cpu_set_t), &cpuset) == 0;
    }

    void SetThreadIdealProcessor(uint32) {
        // Linux 无等效 API
    }

    void SetThreadName(const char* name) {
        pthread_setname_np(pthread_self(), name);
    }

    ThreadCategory GetCurrentThreadCategory() noexcept {
        return s_CurrentCategory;
    }

    void SetCurrentThreadCategory(ThreadCategory cat) noexcept {
        s_CurrentCategory = cat;
    }

    uint32 GetRecommendedCore(ThreadCategory cat, uint32 workerIndex) {
        uint32 totalCores = TopologyInfo::GetCoreCount();
        switch (cat) {
            case ThreadCategory::Main:   return 0;
            case ThreadCategory::Render: return (totalCores > 1) ? 1 : 0;
            case ThreadCategory::Worker: return (2 + workerIndex) % totalCores;
            case ThreadCategory::IO:     return workerIndex % totalCores;
            default: return workerIndex % totalCores;
        }
    }

} // namespace Threading
} // namespace Engine

// ═══════════════════════════════════════════════════════════
// macOS 实现（有限支持）
// ═══════════════════════════════════════════════════════════
#elif defined(__APPLE__)
#include <pthread.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <string>
#include <cstring>

namespace Engine {
namespace Threading {

    static thread_local ThreadCategory s_CurrentCategory = ThreadCategory::Worker;

    uint32 TopologyInfo::GetCoreCount() {
        int32 count = 0;
        size_t size = sizeof(count);
        sysctlbyname("hw.logicalcpu", &count, &size, nullptr, 0);
        return static_cast<uint32>(count > 0 ? count : 1);
    }

    uint32 TopologyInfo::GetPECoreCount() {
        int32 count = 0;
        size_t size = sizeof(count);
        sysctlbyname("hw.perflevel0.logicalcpu", &count, &size, nullptr, 0);
        return static_cast<uint32>(count > 0 ? count : GetCoreCount());
    }

    uint32 TopologyInfo::GetECoreCount() {
        int32 count = 0;
        size_t size = sizeof(count);
        sysctlbyname("hw.perflevel1.logicalcpu", &count, &size, nullptr, 0);
        return static_cast<uint32>(count > 0 ? count : 0);
    }

    bool TopologyInfo::IsHybridArchitecture() {
        return GetECoreCount() > 0;
    }

    uint32 TopologyInfo::GetLLCCacheSize() {
        int64 size = 0;
        size_t sz = sizeof(size);
        sysctlbyname("hw.l3cachesize", &size, &sz, nullptr, 0);
        return static_cast<uint32>(size);
    }

    bool SetThreadAffinity(std::thread::native_handle_type, uint32) {
        // macOS: thread_policy_set 可设置 affinity tag，但功能有限
        // 此处返回 true 表示无操作成功（不做绑定但也不报错）
        return true;
    }

    bool SetThreadAffinityMask(std::thread::native_handle_type, uint64) {
        return true;
    }

    void SetThreadIdealProcessor(uint32) {}

    void SetThreadName(const char* name) {
        pthread_setname_np(name);
    }

    ThreadCategory GetCurrentThreadCategory() noexcept {
        return s_CurrentCategory;
    }

    void SetCurrentThreadCategory(ThreadCategory cat) noexcept {
        s_CurrentCategory = cat;
    }

    uint32 GetRecommendedCore(ThreadCategory cat, uint32 workerIndex) {
        uint32 totalCores = TopologyInfo::GetCoreCount();
        switch (cat) {
            case ThreadCategory::Main:   return 0;
            case ThreadCategory::Render: return (totalCores > 1) ? 1 : 0;
            case ThreadCategory::Worker: return (2 + workerIndex) % totalCores;
            case ThreadCategory::IO:     return workerIndex % totalCores;
            default: return workerIndex % totalCores;
        }
    }

} // namespace Threading
} // namespace Engine

// ═══════════════════════════════════════════════════════════
// Fallback 未知平台
// ═══════════════════════════════════════════════════════════
#else
namespace Engine {
namespace Threading {

    static thread_local ThreadCategory s_CurrentCategory = ThreadCategory::Worker;

    uint32 TopologyInfo::GetCoreCount() { return std::thread::hardware_concurrency(); }
    uint32 TopologyInfo::GetPECoreCount() { return GetCoreCount(); }
    uint32 TopologyInfo::GetECoreCount() { return 0; }
    bool TopologyInfo::IsHybridArchitecture() { return false; }
    uint32 TopologyInfo::GetLLCCacheSize() { return 0; }

    bool SetThreadAffinity(std::thread::native_handle_type, uint32) { return false; }
    bool SetThreadAffinityMask(std::thread::native_handle_type, uint64) { return false; }
    void SetThreadIdealProcessor(uint32) {}
    void SetThreadName(const char*) {}

    ThreadCategory GetCurrentThreadCategory() noexcept { return s_CurrentCategory; }
    void SetCurrentThreadCategory(ThreadCategory cat) noexcept { s_CurrentCategory = cat; }

    uint32 GetRecommendedCore(ThreadCategory, uint32 workerIndex) {
        return workerIndex % std::max(1u, TopologyInfo::GetCoreCount());
    }

} // namespace Threading
} // namespace Engine
#endif