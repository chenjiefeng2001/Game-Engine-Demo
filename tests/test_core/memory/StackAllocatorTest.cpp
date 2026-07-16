/**
 * @file StackAllocatorTest.cpp
 * @brief StackAllocator 内存分配器单元测试
 *
 * 测试重点：
 * - 对齐分配正确性
 * - Marker 回退 (FreeTo)
 * - 用量追踪
 * - 超出预算返回 nullptr
 */
#include <gtest/gtest.h>
#include "Engine/Core/Memory/StackAllocator.h"

using namespace Engine;

class StackAllocatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator = new StackAllocator(1024 * 1024); // 1MB
    }

    void TearDown() override {
        // 检查回收：StackAllocator 析构自动释放所有内存
        // 验证没有内存泄漏的唯一方式是 ASan 或 CRT 检测
        delete allocator;
    }

    StackAllocator* allocator = nullptr;
};

TEST_F(StackAllocatorTest, AlignedAllocation) {
    void* ptr = allocator->Allocate(64, 16); // 16 字节对齐
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 16, 0);
}

TEST_F(StackAllocatorTest, MultipleAllocations) {
    void* a = allocator->Allocate(128, 4);
    void* b = allocator->Allocate(256, 8);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a, b);
}

TEST_F(StackAllocatorTest, MarkerRollback) {
    void* a = allocator->Allocate(64, 4);
    ASSERT_NE(a, nullptr);

    size_t marker = allocator->GetMarker();

    void* b = allocator->Allocate(128, 4);
    ASSERT_NE(b, nullptr);

    allocator->FreeTo(marker);

    // 再次分配应复用 b 的空间
    void* c = allocator->Allocate(128, 4);
    EXPECT_EQ(c, b);
}

TEST_F(StackAllocatorTest, UsedBytesTracking) {
    EXPECT_EQ(allocator->Used(), 0);

    allocator->Allocate(100, 4);
    EXPECT_GE(allocator->Used(), 100);

    allocator->Reset();
    EXPECT_EQ(allocator->Used(), 0);
}

TEST_F(StackAllocatorTest, OverAllocationReturnsNull) {
    // 尝试分配超出预算 — 应返回 nullptr
    void* ptr = allocator->Allocate(2 * 1024 * 1024, 4);
    EXPECT_EQ(ptr, nullptr);
}

TEST_F(StackAllocatorTest, AllocateZeroBytes) {
    void* ptr = allocator->Allocate(0, 4);
    // 大小为零的分配可能返回 nullptr 或有效指针
    // 只要不崩溃即可
    SUCCEED();
}

TEST_F(StackAllocatorTest, CapacityAndRemaining) {
    EXPECT_EQ(allocator->Capacity(), 1024 * 1024);
    EXPECT_EQ(allocator->Remaining(), 1024 * 1024);

    allocator->Allocate(512, 4);
    EXPECT_EQ(allocator->Remaining(), 1024 * 1024 - 512);
}

TEST_F(StackAllocatorTest, ResetReusesMemory) {
    void* a = allocator->Allocate(256, 4);
    ASSERT_NE(a, nullptr);

    allocator->Reset();

    void* b = allocator->Allocate(256, 4);
    EXPECT_EQ(a, b); // Reset 后应复用相同地址
}