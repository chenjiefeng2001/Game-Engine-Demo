/**
 * @file main.cpp
 * @brief ECSTest 入口 — 测试 ECS 核心 + RHI/Vulkan/DX 渲染链路
 *
 * 测试内容：
 *   1. ECS 单元测试：Entity 创建/销毁, Component CRUD, Query, ECSBridge, 物理组件
 *   2. RHI 渲染测试：通过 OpenGL 后端的完整渲染循环
 *
 * 运行方式：
 *   构建 ECSTest 目标后直接运行可执行文件
 */

#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include "ECSTest.h"
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
    Engine::ECSTest app(factory);
    app.Run();
    return 0;
}