/**
 * @file main.cpp
 * @brief 崩溃报告系统测试 — 故意触发崩溃验证报告生成
 *
 * 运行后程序会因访问空指针而崩溃，CrashHandler 应生成：
 *   crashes/crash_*.json
 *   crashes/crash_*.dmp      (Windows)
 *   crashes/crash_*_allocators.txt
 *   crashes/crash_*_stacktrace.txt
 */

#include <Engine/Core/Log.h>
#include <Engine/Debug/CrashHandler.h>
#include <Engine/Debug/CrashContext.h>
#include <Engine/Debug/StackTrace.h>
#include <cstdlib>
#include <cstdio>

using namespace Engine;

static void TriggerAccessViolation() {
    // 故意解引用空指针触发访问违例
    volatile int* p = nullptr;
    *p = 0xDEAD;
}

static void SetupContext() {
    CrashContext::SetLevel("CrashTest_Level1");
    CrashContext::SetPosition(123.0f, 456.0f, 789.0f);
    CrashContext::SetPlayerState("testing|crash");
    CrashContext::SetScriptStack({"test_crash.lua", "crash_helper.lua"});
    CrashContext::SetCustom("test_iteration", "1");

    CrashContext::Register("CrashTest_Provider", []() {
        return "crash_test_provider_data: ok";
    });
}

int main() {
    Log::Init("logs/crash_test.log");

    // 初始化崩溃报告系统
    CrashHandler::Init("crashes");
    Log::Info("CrashTest: about to crash...");

    // 设置一些上下文
    SetupContext();
    Log::Info("CrashTest: context set, triggering crash NOW");

    // 也测试一下 StackTrace 是否可以用
    auto trace = StackTrace::CaptureAndFormat(1);
    Log::Info("Pre-crash stack trace:\n{}", trace);

    // 触发崩溃
    TriggerAccessViolation();

    // 不会执行到这里
    Log::Info("CrashTest: survived crash! (unexpected)");
    CrashHandler::Shutdown();
    Log::Shutdown();
    return 0;
}
