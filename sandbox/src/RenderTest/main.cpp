/**
 * @file main.cpp
 * @brief RenderTest 入口 — 渲染画面验证 + ImGui 调试菜单
 *
 * 目的：验证 OpenGL 后端是否能够正确渲染 3D 画面，
 *       并通过 ImGui 内置调试菜单实时监控渲染状态。
 *
 * 控制：
 *   WASD/QE     — 移动相机
 *   右键拖拽    — 旋转视角
 *   F3          — 切换性能窗口
 *   Escape      — 退出
 */

#include "RenderTestApp.h"
#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include <Engine/Core/JobSystem.h>
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

    // 初始化 JobSystem（Jolt Physics 依赖它进行多线程模拟）
    Engine::JobSystem::Init(0);  // 0 = 自动检测核心数

    Engine::OpenGLGraphicsFactory factory;
    Engine::RenderTestApp app(factory);
    app.Run();

    Engine::JobSystem::Shutdown();

    return 0;
}