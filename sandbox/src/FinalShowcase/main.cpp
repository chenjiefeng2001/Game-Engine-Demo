/**
 * @file main.cpp
 * @brief FinalShowcase 入口 — 渲染架构完全实现的验收场景
 *
 * 编译运行：
 *   cd build && cmake --build . --target FinalShowcase --config RelWithDebInfo
 *   ./sandbox/FinalShowcase/RelWithDebInfo/FinalShowcase.exe
 */

#include "FinalShowcaseApp.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/OpenGL/OpenGLGraphicsFactory.h"

int main() {
    // 初始化 JobSystem（Jolt Physics 多线程依赖）
    Engine::JobSystem::Init(0);

    // 创建 OpenGL 图形工厂
    Engine::OpenGLGraphicsFactory factory;
    Engine::FinalShowcaseApp app(factory);
    app.Run();

    Engine::JobSystem::Shutdown();
    return 0;
}