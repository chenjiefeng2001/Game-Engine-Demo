#include <Engine/Application.h>
#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
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
    Engine::Application app(factory);
    app.Run();
    return 0;
}