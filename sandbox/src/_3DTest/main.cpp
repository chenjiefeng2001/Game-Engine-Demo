#include "_3DTestApp.h"
#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdlib>
#include <iostream>

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    try {
        Engine::OpenGLGraphicsFactory factory;
        Engine::_3DTestApp app(factory);
        app.Run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
