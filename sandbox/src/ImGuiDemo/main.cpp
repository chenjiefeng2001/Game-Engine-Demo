/**
 * @file main.cpp
 * @brief ImGuiDemo — 演示 Dear ImGui 集成
 *
 * 功能：
 *   - 显示 ImGui 内置的示例窗口（ShowDemoWindow）
 *   - 显示引擎信息面板（帧率、窗口尺寸）
 */

#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include "ImGuiDemoApp.h"
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
    Engine::ImGuiDemoApp app(factory);
    app.Run();
    return 0;
}
