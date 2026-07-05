#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include "MarioDemoApp.h"
#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    Engine::OpenGLGraphicsFactory factory;
    Engine::MarioDemoApp app(factory);
    app.Run();
    return 0;
}
