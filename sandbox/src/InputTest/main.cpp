#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include "InputTestApp.h"

int main() {
    Engine::OpenGLGraphicsFactory factory;
    Engine::InputTestApp app(factory);
    app.Run();
    return 0;
}