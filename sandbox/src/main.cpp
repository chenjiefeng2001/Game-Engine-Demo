#include <Engine/Application.h>
#include <Engine/OpenGL/OpenGLGraphicsFactory.h>   // ← 新增 include

int main() {
    Engine::OpenGLGraphicsFactory factory;           // ← 取消注释
    Engine::Application app(factory);
    app.Run();
    return 0;
}