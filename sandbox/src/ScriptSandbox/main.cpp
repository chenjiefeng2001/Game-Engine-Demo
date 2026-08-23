/**
 * @file main.cpp
 * @brief ScriptSandbox — Scripting v1 产品验收沙盒
 */

#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include "ScriptSandboxApp.h"
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
    Engine::Sandbox::ScriptSandboxApp app(factory);
    app.Run();
    return 0;
}
