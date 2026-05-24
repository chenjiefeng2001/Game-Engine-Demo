#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include "PhysicsTestApp.h"
#include <clocale>
#ifdef _WIN32
#include <windows.h>
#endif

int main() {
    // 设置控制台输出为 UTF-8，避免中文乱码
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::setlocale(LC_ALL, "en_US.UTF-8");

    Engine::OpenGLGraphicsFactory factory;
    Engine::PhysicsTestApp app(factory);
    app.Run();
    return 0;
}
