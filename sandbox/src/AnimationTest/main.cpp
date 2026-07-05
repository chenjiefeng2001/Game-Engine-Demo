/**
 * @file main.cpp
 * @brief 动画系统测试入口点
 *
 * 编译运行:
 *   cmake --build build --target AnimationTest
 *   ./build/sandbox/AnimationTest/AnimationTest.exe
 */

#include "AnimationTestApp.h"
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    bool allPassed = Engine::AnimationTestApp::RunAll();

    if (allPassed) {
        std::cout << "✓ 所有动画测试通过!" << std::endl;
        return 0;
    } else {
        std::cout << "✗ 部分动画测试失败!" << std::endl;
        return 1;
    }
}
