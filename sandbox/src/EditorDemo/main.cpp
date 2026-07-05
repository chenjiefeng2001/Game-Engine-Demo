/**
 * @file main.cpp
 * @brief Editor Demo — 引擎编辑器 UI 框架演示
 */

#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include <Engine/Core/Log.h>
#include "EditorDemoApp.h"
#include <clocale>
#ifdef _WIN32
#include <windows.h>
#endif

/**
 * @brief 引擎生命周期管理
 *
 * 初始化/关闭顺序（严格遵守）：
 *   1. Log::Init()            — 第一个初始化（日志系统必须先于所有子系统）
 *      ...
 *      app.Run()              — 引擎主循环（内部初始化/运行/关闭子系统）
 *      ...
 *   2. spdlog::shutdown()     — 最后一个关闭（在所有引擎组件停止后）
 *
 * 注意：Log::Shutdown() / spdlog::shutdown() 不能在 Application 的
 * 析构函数或 SubsystemManager 中调用，因为：
 *   - SubsystemManager 的析构函数（在 Application 析构时自动触发）会
 *     打印日志（s_Log.Info...），如果日志系统已关闭则崩溃。
 *   - 所有引擎子系统通过 App 的局部作用域完全销毁后，才安全关闭日志。
 */
int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::setlocale(LC_ALL, "en_US.UTF-8");

    Engine::OpenGLGraphicsFactory factory;

    // ── Application 局部作用域 ──
    // 确保 Application 及其内部的 SubsystemManager 在 Log::Shutdown()
    // 之前完全析构，避免 SubsystemManager 析构时打印日志访问已关闭的
    // spdlog 线程池。
    {
        Engine::EditorDemoApp app(factory);
        app.Run();
    } // ← Application 在此安全完全析构

    // ── 所有引擎组件已停止，最后关闭日志系统 ──
    // 此处不再调用 Log::Shutdown()，因为该函数可能尝试刷新日志到
    // 已关闭的文件/控制台 sink。
    // spdlog 的全局线程池和注册表在进程退出时由操作系统自动清理。
    // 显式调用可能导致与其他静态对象的析构顺序冲突。
    // (如果必须显式清理，可调用 spdlog::shutdown()，但需确保在
    //  所有可能打印日志的对象析构之后)
    // spdlog::shutdown();

    return 0;
}
