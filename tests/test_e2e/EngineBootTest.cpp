/**
 * @file EngineBootTest.cpp
 * @brief 子系统生命周期端到端测试
 *
 * 测试重点：
 * - JobSystem 初始化/销毁
 * - GLFW 无窗口上下文创建
 * - 多个子系统并行初始化不冲突
 */
#include <gtest/gtest.h>
#include "Engine/Core/JobSystem.h"
#include "Engine/Core/Log.h"
#include <GLFW/glfw3.h>
#include <memory>

using namespace Engine;

static Logger s_Log("E2ETest");

// ── JobSystem 生命周期 ──
TEST(EngineE2ETest, JobSystemBootAndShutdown) {
    EXPECT_NO_FATAL_FAILURE({
        JobSystem::Init(0);
        EXPECT_NE(JobSystem::Get(), nullptr);
        JobSystem::Shutdown();
    });
}

TEST(EngineE2ETest, JobSystemDoubleInitIsSafe) {
    // 重复初始化不应崩溃
    JobSystem::Init(0);
    JobSystem::Init(0); // 第二次 init 应 no-op
    JobSystem::Shutdown();
}

TEST(EngineE2ETest, JobSystemDispatchBeforeShutdown) {
    JobSystem::Init(2);
    auto* js = JobSystem::Get();

    std::atomic<int32> counter{0};
    for (int i = 0; i < 100; ++i) {
        js->Dispatch([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }
    js->Wait(nullptr);
    EXPECT_EQ(counter.load(), 100);

    JobSystem::Shutdown();
}

// ── GLFW Headless 上下文 ──
class GLFWHeadlessTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        s_Initialized = glfwInit();
        ASSERT_TRUE(s_Initialized);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }

    static void TearDownTestSuite() {
        if (s_Initialized) glfwTerminate();
    }

    void SetUp() override {
        s_Window = glfwCreateWindow(1, 1, "E2E Headless", nullptr, nullptr);
        if (!s_Window) GTEST_SKIP() << "Cannot create GLFW window (no GPU?)";
        glfwMakeContextCurrent(s_Window);
    }

    void TearDown() override {
        if (s_Window) { glfwDestroyWindow(s_Window); s_Window = nullptr; }
    }

    static bool s_Initialized;
    static GLFWwindow* s_Window;
};

bool GLFWHeadlessTest::s_Initialized = false;
GLFWwindow* GLFWHeadlessTest::s_Window = nullptr;

TEST_F(GLFWHeadlessTest, CreateAndDestroy) {
    // 窗口已创建
    ASSERT_NE(s_Window, nullptr);
    EXPECT_FALSE(glfwWindowShouldClose(s_Window));
}

TEST_F(GLFWHeadlessTest, GetGLVersion) {
    // 验证 GL 上下文版本
    const char* version = (const char*)glGetString(GL_VERSION);
    ASSERT_NE(version, nullptr);
    s_Log.Info("GL Version: {}", version);
}

// ── 多子系统并行 ──
TEST(EngineE2ETest, LoggerAndJobSystemCoexist) {
    // 验证日志和 JobSystem 能同时工作
    EXPECT_NO_FATAL_FAILURE({
        JobSystem::Init(0);
        s_Log.Info("Logger works with JobSystem active");

        std::atomic<int32> counter{0};
        auto* js = JobSystem::Get();
        js->Dispatch([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
        js->Wait(nullptr);
        EXPECT_EQ(counter.load(), 1);

        JobSystem::Shutdown();
    });
}