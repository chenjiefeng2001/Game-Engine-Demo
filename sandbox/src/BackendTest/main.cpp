/**
 * @file main.cpp
 * @brief BackendTest 入口 — 测试 Vulkan / D3D12 / OpenGL 后端功能完整性
 *
 * 测试内容：
 *   1. Vulkan/D3D12 运行时可用性检测
 *   2. 设备创建与资源管理
 *   3. Buffer/Texture/PipelineState 创建
 *   4. CommandList 录制
 *   5. SwapChain（GL46 后端）
 *   6. PSO 缓存
 *   7. ECS ↔ RHI 集成
 *
 * 运行方式：
 *   构建 BackendTest 目标后直接运行
 */

#include "BackendTest.h"
#include <clocale>
#include <cstdlib>
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

    Engine::BackendTest app;
    app.Run();
    return 0;
}