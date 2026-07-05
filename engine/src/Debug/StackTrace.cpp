#include "Engine/Debug/StackTrace.h"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdio>
#include <algorithm>

// ============================================================
// Windows 实现 — RtlCaptureStackBackTrace + DbgHelp
// ============================================================

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>

#pragma comment(lib, "dbghelp.lib")

// RtlCaptureStackBackTrace 动态获取
static void* s_RtlCapturePtr = nullptr;

namespace Engine {

    // 延迟初始化符号引擎（仅一次）
    static bool InitSym() {
        static bool inited = false;
        static bool ok = false;
        if (!inited) {
            inited = true;

            // 动态获取 RtlCaptureStackBackTrace
            HMODULE ntdll = GetModuleHandleA("ntdll.dll");
            if (ntdll)
                s_RtlCapturePtr = (void*)GetProcAddress(ntdll, "RtlCaptureStackBackTrace");

            HANDLE proc = GetCurrentProcess();
            SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
            ok = SymInitialize(proc, nullptr, TRUE);
        }
        return ok && s_RtlCapturePtr != nullptr;
    }

    // 函数指针类型
    typedef USHORT (__stdcall* CaptureFunc)(ULONG, ULONG, PVOID*, PULONG);

    std::vector<StackFrame> StackTrace::Capture(int skipFrames, int maxFrames) {
        std::vector<StackFrame> frames;
        if (!InitSym()) return frames;

        auto fn = (CaptureFunc)s_RtlCapturePtr;
        void* addresses[62] = {};
        ULONG framesToSkip = static_cast<ULONG>(skipFrames + 1);
        ULONG framesToCapture = static_cast<ULONG>(maxFrames > 62 ? 62 : maxFrames);
        USHORT count = fn(framesToSkip, framesToCapture, addresses, nullptr);

        frames.reserve(count);
        for (USHORT i = 0; i < count; ++i) {
            StackFrame sf;
            sf.address = addresses[i];
            sf.symbol  = ResolveSymbol(addresses[i]);
            frames.push_back(std::move(sf));
        }
        return frames;
    }

    std::string StackTrace::ResolveSymbol(void* address) {
        if (!address) return {};

        HANDLE proc = GetCurrentProcess();
        alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 256] = {};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen   = 255;

        DWORD64 addr = reinterpret_cast<DWORD64>(address);
        DWORD64 disp  = 0;
        char    name[512] = {};

        if (SymFromAddr(proc, addr, &disp, sym)) {
            std::snprintf(name, sizeof(name), "%s+0x%llX", sym->Name, (unsigned long long)disp);
        } else {
            std::snprintf(name, sizeof(name), "0x%llX", (unsigned long long)addr);
        }

        // 尝试解析行号
        IMAGEHLP_LINE64 lineInfo = {};
        lineInfo.SizeOfStruct = sizeof(lineInfo);
        DWORD lineDisp = 0;
        if (SymGetLineFromAddr64(proc, addr, &lineDisp, &lineInfo)) {
            std::snprintf(name + std::strlen(name), sizeof(name) - std::strlen(name),
                          " (%s:%lu)", lineInfo.FileName, lineInfo.LineNumber);
        }

        return std::string(name);
    }

    std::string StackTrace::Format(const std::vector<StackFrame>& frames) {
        std::ostringstream oss;
        for (size_t i = 0; i < frames.size(); ++i) {
            oss << "  #" << std::setw(2) << i << "  "
                << frames[i].symbol
                << "  [0x" << std::hex
                << reinterpret_cast<uintptr_t>(frames[i].address) << "]\n";
        }
        return oss.str();
    }

    std::string StackTrace::CaptureAndFormat(int skipFrames, int maxFrames) {
        return Format(Capture(skipFrames + 1, maxFrames));
    }

} // namespace Engine

// ============================================================
// Linux / macOS 实现 — backtrace() + dladdr + __cxa_demangle
// ============================================================

#elif defined(__linux__) || defined(__APPLE__)

#include <execinfo.h>
#include <dlfcn.h>
#include <cxxabi.h>
#include <cstdlib>

namespace Engine {

    std::vector<StackFrame> StackTrace::Capture(int skipFrames, int maxFrames) {
        std::vector<StackFrame> frames;
        void* addresses[128] = {};
        int count = backtrace(addresses, std::min(maxFrames, 126));
        if (count <= 0) return frames;

        frames.reserve(static_cast<size_t>(count));
        for (int i = skipFrames + 1; i < count; ++i) {
            StackFrame sf;
            sf.address = addresses[i];

            Dl_info info = {};
            if (dladdr(addresses[i], &info)) {
                if (info.dli_sname) {
                    // C++ name demangling
                    int status = 0;
                    char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
                    sf.symbol = demangled ? demangled : info.dli_sname;
                    if (demangled) std::free(demangled);
                }
                if (info.dli_fname) sf.module = info.dli_fname;
                sf.offset = reinterpret_cast<uintptr_t>(addresses[i])
                          - reinterpret_cast<uintptr_t>(info.dli_saddr);
            }

            frames.push_back(std::move(sf));
        }
        return frames;
    }

    std::string StackTrace::ResolveSymbol(void* address) {
        Dl_info info = {};
        if (dladdr(address, &info) && info.dli_sname) {
            int status = 0;
            char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
            std::string result = demangled ? demangled : info.dli_sname;
            if (demangled) std::free(demangled);
            return result;
        }
        return {};
    }

    std::string StackTrace::Format(const std::vector<StackFrame>& frames) {
        std::ostringstream oss;
        for (size_t i = 0; i < frames.size(); ++i) {
            oss << "  #" << i << "  " << frames[i].symbol
                << " [" << frames[i].module << "+0x"
                << std::hex << frames[i].offset << "]\n";
        }
        return oss.str();
    }

    std::string StackTrace::CaptureAndFormat(int skipFrames, int maxFrames) {
        return Format(Capture(skipFrames + 1, maxFrames));
    }

} // namespace Engine

#endif
