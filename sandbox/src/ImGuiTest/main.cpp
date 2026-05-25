/**
 * @file main.cpp
 * @brief ImGui Test — 综合测试 UI 开关、实体属性热重载、输入抢占、内存泄漏
 */

#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include "ImGuiTestApp.h"
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
    Engine::ImGuiTestApp app(factory);
    app.Run();
    return 0;
}
