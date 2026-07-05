/**
 * @file main.cpp
 * @brief 自制物理引擎 Bare2DPhysicsWorld 纯控制台测试入口
 *
 * 运行方式：直接编译执行，全部测试通过则返回 0。
 * 输出格式：每个测试通过/失败信息 + 最终汇总。
 */

#include "BarePhysicsTestApp.h"

int main() {
    Engine::BarePhysicsTestApp app;
    bool allPassed = app.RunAllTests();

    std::cout << "\n========================================" << std::endl;
    if (allPassed) {
        std::cout << "  ALL TESTS PASSED!" << std::endl;
    } else {
        std::cout << "  SOME TESTS FAILED!" << std::endl;
    }
    std::cout << "========================================" << std::endl;

    return allPassed ? 0 : 1;
}