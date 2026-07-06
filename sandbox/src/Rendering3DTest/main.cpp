/**
 * @file main.cpp
 * @brief Rendering3DTest 入口 — Headless RHI 后端验证 + ECS 性能测试
 *
 * 默认模式：遍历所有可用后端（Vulkan, D3D12）执行：
 *   1. 设备创建 + 初始化
 *   2. Buffer/CommandList/PSO 基础管线验证
 *   3. GPU Fill+Readback (Vulkan) / 命令提交 (D3D12)
 *   4. ECS 10,000 实体创建 + 查询迭代性能
 *
 * 命令行参数：
 *   --vulkan        仅测试 Vulkan 后端
 *   --d3d12         仅测试 D3D12 后端
 */

#include "Rendering3DTest.h"
#include <clocale>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::setlocale(LC_ALL, "en_US.UTF-8");

    // Headless 后端验证（默认）
    Engine::Rendering3DTest test;
    test.RunAll();

    return 0;
}
