/**
 * @file JobSystemStressTest.cpp
 * @brief JobSystem 压力 + Data Race 测试
 *
 * 测试重点：
 * - 1000 任务分派完整性
 * - ParallelFor 并行正确性
 * - 无死锁
 */
#include <gtest/gtest.h>
#include "Engine/Core/JobSystem.h"
#include <atomic>
#include <chrono>

using namespace Engine;

class JobSystemTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        JobSystem::Init(0); // 自动检测核心数
    }

    static void TearDownTestSuite() {
        JobSystem::Shutdown();
    }
};

TEST_F(JobSystemTest, Dispatch1000Tasks) {
    auto* js = JobSystem::Get();
    ASSERT_NE(js, nullptr);

    constexpr int kTaskCount = 1000;
    std::atomic<int32> counter{0};

    for (int i = 0; i < kTaskCount; ++i) {
        js->Dispatch([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    js->Wait(nullptr); // 等待所有任务
    EXPECT_EQ(counter.load(), kTaskCount);
}

TEST_F(JobSystemTest, ParallelForSummation) {
    auto* js = JobSystem::Get();
    constexpr int kSize = 10000;
    std::vector<int> data(kSize, 1);

    std::atomic<int64> sum{0};
    JobHandle handle = js->ParallelFor(0, kSize, [&](int32 i) {
        sum.fetch_add(data[i], std::memory_order_relaxed);
    });

    js->Wait(handle);
    EXPECT_EQ(sum.load(), kSize);
}

TEST_F(JobSystemTest, ParallelForPartialRange) {
    auto* js = JobSystem::Get();
    std::atomic<int32> count{0};

    JobHandle handle = js->ParallelFor(10, 20, [&](int32 i) {
        ASSERT_GE(i, 10);
        ASSERT_LT(i, 20);
        count.fetch_add(1, std::memory_order_relaxed);
    });

    js->Wait(handle);
    EXPECT_EQ(count.load(), 10);
}

TEST_F(JobSystemTest, NoDeadlockWithMultipleDispatches) {
    auto* js = JobSystem::Get();

    // 连续分派多个批次，验证不会死锁
    for (int batch = 0; batch < 10; ++batch) {
        std::atomic<int32> counter{0};
        for (int j = 0; j < 100; ++j) {
            js->Dispatch([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }
        js->Wait(nullptr);
        EXPECT_EQ(counter.load(), 100);
    }
}