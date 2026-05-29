/**
 * @file main.cpp
 * @brief 系统功能综合测试 — 入口点
 *
 * 测试日志、Job 系统、计时器、混合驱动、Adaptive 模式。
 * 运行约 30 秒后自动退出。
 * 所有测试结果通过 Log 和 ImGui 面板展示。
 */

#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include "SystemTestApp.h"
#include <clocale>
#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::setlocale(LC_ALL, "en_US.UTF-8");

    Engine::OpenGLGraphicsFactory factory;
    Engine::SystemTestApp app(factory);
    app.Run();
    return 0;
}
