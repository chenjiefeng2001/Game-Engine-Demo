#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include "SpriteBatchTest.h"

int main() {
    Engine::OpenGLGraphicsFactory factory;
    Engine::SpriteBatchTest app(factory);
    app.Run();
    return 0;
}