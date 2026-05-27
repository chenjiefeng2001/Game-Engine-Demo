/**
 * @file main.cpp
 * @brief 序列化测试入口点
 *
 * 编译运行：
 *   cmake --build build --target SerializationTest
 *   ./build/sandbox/SerializationTest.exe
 */

#include "SerializationTest.h"
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    bool allPassed = Engine::SerializationTest::RunAll();

    std::cout << std::endl;
    if (allPassed) {
        std::cout << "✓ 所有测试通过!" << std::endl;
        return 0;
    } else {
        std::cout << "✗ 部分测试失败!" << std::endl;
        return 1;
    }
}
