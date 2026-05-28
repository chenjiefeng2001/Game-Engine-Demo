/**
 * @file main.cpp
 * @brief Editor Demo — 引擎编辑器 UI 框架演示
 */

#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include "EditorDemoApp.h"
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
    Engine::EditorDemoApp app(factory);
    app.Run();
    return 0;
}
