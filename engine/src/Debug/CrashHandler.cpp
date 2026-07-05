#include "Engine/Debug/CrashHandler.h"
#include "Engine/Debug/StackTrace.h"
#include "Engine/Debug/CrashContext.h"
#include "Engine/Debug/ScreenshotCapture.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/Memory/StackAllocator.h"
#include "Engine/Core/Resources/ResourcePoolAllocator.h"
#include "Engine/Core/Resources/ResourceManager.h"
#include "Engine/Core/Resources/ResourceRegistry.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

// OpenGL 截屏需要 — 但因崩溃时 OpenGL 上下文可能已损坏，
// 建议在正常运行中预捕获帧缓冲区到环形缓冲区供崩溃时使用。
// 此处保留占位，待后续实现。
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <vector>
#include <cstring>
#include <cstdio>

namespace Engine {

// ============================================================
// 静态成员
// ============================================================

std::string CrashHandler::s_ReportDir = "crashes";
bool        CrashHandler::s_Initialized = false;

#if defined(_WIN32)
static PVOID s_VeHandle = nullptr;
#endif

// 在 Application.cpp 中定义的子系统分配器
extern StackAllocator* g_SubsystemAllocator;

// Windows VEH 前向声明（在 Init 之前定义）
#if defined(_WIN32)
static LONG WINAPI VectoredExceptionHandler(EXCEPTION_POINTERS* ep);
#endif

// ============================================================
// 生命周期
// ============================================================

void CrashHandler::Init(const std::string& reportDir) {
    if (s_Initialized) return;
    s_ReportDir = reportDir;

    // 确保报告目录存在
#if defined(_WIN32)
    CreateDirectoryA(s_ReportDir.c_str(), nullptr);
#endif

#if defined(_WIN32)
    // VEH 优先级高于 SetUnhandledExceptionFilter
    s_VeHandle = AddVectoredExceptionHandler(1, VectoredExceptionHandler);
#endif

    s_Initialized = true;
    Log::Info("CrashHandler initialized (report dir: {})", s_ReportDir);
}

void CrashHandler::Shutdown() {
    if (!s_Initialized) return;

#if defined(_WIN32)
    if (s_VeHandle) {
        RemoveVectoredExceptionHandler(s_VeHandle);
        s_VeHandle = nullptr;
    }
#endif

    s_Initialized = false;
    Log::Info("CrashHandler shut down");
}

const std::string& CrashHandler::GetReportDir() {
    return s_ReportDir;
}

// ============================================================
// Windows VEH 处理（静态回调，非成员函数）
// ============================================================

#if defined(_WIN32)
static LONG WINAPI VectoredExceptionHandler(EXCEPTION_POINTERS* ep) {
    // 只处理致命异常
    switch (ep->ExceptionRecord->ExceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            break;
        default:
            return EXCEPTION_CONTINUE_SEARCH;
    }

    // 生成崩溃报告
    CrashHandler::OnCrash(ep);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

// ============================================================
// 核心 OnCrash
// ============================================================

void CrashHandler::OnCrash(void* context) {
    // 生成时间戳文件名
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << s_ReportDir << "/crash_"
       << std::put_time(std::gmtime(&tt), "%Y%m%d_%H%M%S");
    std::string base = ss.str();

    // ── 1. 写入 JSON 崩溃报告 ──
    WriteCrashReport(base + ".json");

    // ── 2. 写入 MiniDump (Windows) ──
#if defined(_WIN32)
    WriteMiniDump(base + ".dmp", context);
#endif

    // ── 3. 截屏 ──
    CaptureScreenshot(base + ".png");

    // ── 4. 分配器状态 ──
    DumpAllocators(base + "_allocators.txt");

    // ── 5. 强制刷盘所有日志 + 导出内存历史缓冲区 ──
    // 必须在 StackTrace 之后调用，因为 ForceFlushAndDump 可触及其他日志
    {
        Log::ForceFlushAndDump(base + "_last_logs.txt");
    }

    // ── 6. 额外堆栈跟踪文本 ──
    {
        std::ofstream f(base + "_stacktrace.txt");
        if (f) {
            f << StackTrace::CaptureAndFormat(2);
        }
    }

    // 日志（只要能写出来）
    Log::Error("!!! CRASH !!! Report written to {}", base);
}

// ============================================================
// 写入 JSON 报告
// ============================================================

void CrashHandler::WriteCrashReport(const std::string& path) {
    std::ofstream f(path);
    if (!f) return;

    // CrashContext
    f << CrashContext::CollectAll();

    // 堆栈跟踪追加到同一文件末尾
    f << ",\n\"stackTrace\": [\n";
    auto frames = StackTrace::Capture(2);
    for (size_t i = 0; i < frames.size(); ++i) {
        if (i) f << ",\n";
        f << "  { \"index\": " << i
          << ", \"symbol\": \"" << frames[i].symbol
          << "\", \"address\": \"0x"
          << std::hex << reinterpret_cast<uintptr_t>(frames[i].address)
          << "\" }";
    }
    f << "\n]\n}\n";
}

// ============================================================
// MiniDump (Windows)
// ============================================================

#if defined(_WIN32)
void CrashHandler::WriteMiniDump(const std::string& path, void* context) {
#if defined(_WIN32)
    // dbghelp.h 的 MiniDumpWriteDump — 若链接失败此函数无操作
    auto* ep = static_cast<EXCEPTION_POINTERS*>(context);
    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    MINIDUMP_EXCEPTION_INFORMATION mei = {};
    mei.ThreadId          = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers    = FALSE;

    // 使用动态加载以避免链接问题
    HMODULE dbgHelp = LoadLibraryA("dbghelp.dll");
    if (dbgHelp) {
        using MiniDumpFn = BOOL (WINAPI*)(HANDLE, DWORD, HANDLE,
                                          MINIDUMP_TYPE,
                                          MINIDUMP_EXCEPTION_INFORMATION*,
                                          MINIDUMP_USER_STREAM_INFORMATION*,
                                          MINIDUMP_CALLBACK_INFORMATION*);
        auto fn = (MiniDumpFn)GetProcAddress(dbgHelp, "MiniDumpWriteDump");
        if (fn) {
            fn(GetCurrentProcess(), GetCurrentProcessId(), hFile,
               (MINIDUMP_TYPE)(MiniDumpWithDataSegs |
               MiniDumpWithUnloadedModules |
               MiniDumpWithProcessThreadData |
               MiniDumpWithTokenInformation),
               ep ? &mei : nullptr, nullptr, nullptr);
        }
        FreeLibrary(dbgHelp);
    }
    CloseHandle(hFile);
#else
    (void)path; (void)context;
#endif
}
#endif

// ============================================================
// 截屏
// ============================================================

void CrashHandler::CaptureScreenshot(const std::string& path) {
    if (ScreenshotCapture::SaveLastFrame(path + ".tga")) {
        Log::Info("Screenshot saved to {}.tga", path);
    } else {
        Log::Warn("No pre-captured screenshot available");
    }
}

// ============================================================
// 分配器状态
// ============================================================

void CrashHandler::DumpAllocators(const std::string& path) {
    std::ofstream f(path);
    if (!f) return;

    f << "=== Allocator State ===\n";

    // StackAllocator
    if (g_SubsystemAllocator) {
        f << "\n--- StackAllocator (Subsystems) ---\n";
        f << "  Capacity: " << g_SubsystemAllocator->Capacity() << "\n";
        f << "  Used:     " << g_SubsystemAllocator->Used() << "\n";
        f << "  Remaining:" << g_SubsystemAllocator->Remaining() << "\n";
        f << "  Usage:    "
          << (100.0 * g_SubsystemAllocator->Used() / g_SubsystemAllocator->Capacity())
          << "%\n";
    } else {
        f << "\n  SubsystemAllocator: null\n";
    }

    // ResourcePoolAllocator
    auto* rm = ResourceManager::Get();
    if (rm) {
        auto stats = rm->GetRegistry().GetAllocator().GetGlobalStats();
        f << "\n--- ResourcePoolAllocator ---\n";
        f << "  Pools:       " << stats.poolCount << "\n";
        f << "  Total bytes: " << stats.totalBytes << "\n";
        f << "  Allocated:   " << stats.totalAllocated << "\n";
        f << "  Free:        " << stats.totalFree << "\n";
    } else {
        f << "\n  ResourceManager: null\n";
    }
}

} // namespace Engine
