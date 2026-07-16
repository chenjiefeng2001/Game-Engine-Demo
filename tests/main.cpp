/**
 * @file main.cpp
 * @brief 全引擎测试套件入口 — Google Test
 *
 * 当前 ASan 配置说明（CMakeLists.txt 中已设置 ENABLE_ASAN=OFF 默认）：
 * - Windows: /fsanitize=address
 * - CRT 内存泄漏检测：无 ASan 时使用 _CrtDumpMemoryLeaks()
 */
#include <gtest/gtest.h>

#if defined(_MSC_VER) && !defined(__clang__)
#include <crtdbg.h>
struct LeakDetector {
    LeakDetector() {
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    }
};
static LeakDetector s_LeakDetector;
#endif

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}