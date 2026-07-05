#pragma once

#include <string>

namespace Engine {

    class CrashHandler {
    public:
        static void Init(const std::string& reportDir = "crashes");
        static void Shutdown();
        static const std::string& GetReportDir();

        /** @brief 生成崩溃报告（供平台异常处理器调用） */
        static void OnCrash(void* context);

    private:
        static void WriteCrashReport(const std::string& path);
        static void WriteMiniDump(const std::string& path, void* exceptionPointers);
        static void CaptureScreenshot(const std::string& path);
        static void DumpAllocators(const std::string& path);

        static std::string s_ReportDir;
        static bool        s_Initialized;
    };

} // namespace Engine
