#pragma once

/**
 * @file StackTrace.h
 * @brief 堆栈跟踪 — 跨平台调用栈捕获与符号解析。
 *
 * Windows: RtlCaptureStackBackTrace + DbgHelp (SymFromAddr)
 * Linux:   backtrace() + dladdr + __cxa_demangle
 */

#include "Engine/Types.h"
#include <string>
#include <vector>

namespace Engine {

    struct StackFrame {
        void*   address   = nullptr;
        uint64  offset    = 0;
        std::string module;
        std::string symbol;
        std::string filename;
        int     line      = 0;
    };

    class StackTrace {
    public:
        /**
         * @brief 捕获当前调用栈
         * @param skipFrames  跳过的帧数（通常 1 跳过 Capture 自身）
         * @param maxFrames   最大捕获帧数
         * @return 堆栈帧列表
         */
        static std::vector<StackFrame> Capture(int skipFrames = 1, int maxFrames = 62);

        /** @brief 解析单个地址到符号名 */
        static std::string ResolveSymbol(void* address);

        /** @brief 将帧列表格式化为多行文本 */
        static std::string Format(const std::vector<StackFrame>& frames);

        /** @brief 快捷方式：捕获 + 格式化 */
        static std::string CaptureAndFormat(int skipFrames = 1, int maxFrames = 62);
    };

} // namespace Engine
