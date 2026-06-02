/**
 * @file main.cpp
 * @brief 3D 动画演示入口点
 *
 * 编译运行:
 *   cmake --build build --target AnimationDemo
 *   ./build/sandbox/AnimationDemo/AnimationDemo.exe
 */

#include "AnimationDemoApp.h"
#include <Engine/Core/Log.h>
#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    Engine::Log::Init();

    try {
        Engine::OpenGLGraphicsFactory factory;
        Engine::AnimationDemoApp app(factory);
        app.Run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        Engine::Log::Shutdown();
        return EXIT_FAILURE;
    }

    Engine::Log::Shutdown();
    return 0;
}
