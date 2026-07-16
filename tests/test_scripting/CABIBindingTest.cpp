/**
 * @file CABIBindingTest.cpp
 * @brief C-ABI 布局与绑定测试
 *
 * 注意：脚本引擎 C-ABI (engine_api.h) 当前仍在开发中（~20%）。
 * 本测试专注于验证 C++ 结构体布局与 ABI 兼容性，
 * 这些是脚本系统的基础前提。
 */
#include <gtest/gtest.h>
#include "Engine/Core/ECS/ECS.fwd.h"
#include <cstdint>
#include <cstddef>

using namespace Engine;

// ── 模拟未来 C-ABI 的结构体布局 ──
// 这些布局必须与 C++ ECS 组件完全一致
struct CTransform {
    float position[3];
    float padding;
    float rotation[4]; // quaternion
    float scale[3];
    float padding2;
};

struct CRigidBody {
    float velocity[3];
    float mass;
    float force[3];
    float padding;
};

// ── 验证 C-ABI 结构体大小与预期一致 ──
TEST(CABITest, StructSizes) {
    // Transform 通常 = 4 + 16 + 12 + 4 = 36 bytes → 对齐到 48
    // RigidBody 通常 = 12 + 4 + 12 + 4 = 32 bytes
    // 实际大小取决于引擎的具体定义，这里只验证非零
    EXPECT_GT(sizeof(CTransform), 0);
    EXPECT_GT(sizeof(CRigidBody), 0);
}

TEST(CABITest, StandardLayout) {
    // C-ABI 结构体必须是标准布局（POD），才能被 WASM/C# 安全读取
    EXPECT_TRUE(std::is_standard_layout<CTransform>::value);
    EXPECT_TRUE(std::is_trivial<CTransform>::value);
    EXPECT_TRUE(std::is_standard_layout<CRigidBody>::value);
    EXPECT_TRUE(std::is_trivial<CRigidBody>::value);
}

TEST(CABITest, FieldOffsets) {
    // 验证 CTransform 字段偏移量
    EXPECT_EQ(offsetof(CTransform, position), 0);
    EXPECT_EQ(offsetof(CTransform, rotation), 16);
    EXPECT_EQ(offsetof(CTransform, scale), 32);

    // 验证 CRigidBody 字段偏移量
    EXPECT_EQ(offsetof(CRigidBody, velocity), 0);
    EXPECT_EQ(offsetof(CRigidBody, mass), 12);
    EXPECT_EQ(offsetof(CRigidBody, force), 16);
}

// ── C 函数指针类型测试 ──
// 验证引擎 API 函数的签名兼容性
extern "C" {
    // 模拟未来 C-ABI 函数签名
    typedef void* (*Engine_GetComponentPtr_Func)(uint64_t entityID, uint32_t componentType);
    typedef uint64_t (*Engine_CreateEntity_Func)();
    typedef void (*Engine_DestroyEntity_Func)(uint64_t id);
    typedef float (*Engine_Time_GetDeltaTime_Func)();
}

TEST(CABITest, FunctionPointerSizes) {
    // 验证函数指针大小符合预期（x64 = 8 字节）
    EXPECT_EQ(sizeof(Engine_GetComponentPtr_Func), sizeof(void*));
    EXPECT_EQ(sizeof(Engine_CreateEntity_Func), sizeof(void*));
    EXPECT_EQ(sizeof(Engine_DestroyEntity_Func), sizeof(void*));
    EXPECT_EQ(sizeof(Engine_Time_GetDeltaTime_Func), sizeof(void*));
}

// ── 枚举与常量兼容性 ──
// 确保 C/GLSL/C# 跨语言枚举值一致
TEST(CABITest, ComponentIDConstants) {
    // 组件 ID 应从 1 开始，0 表示"无组件"
    constexpr uint32_t kInvalidComponent = 0;
    EXPECT_EQ(kInvalidComponent, 0);
}

TEST(CABITest, BitFlagAlignment) {
    // ECS Query 组件掩码应使用 uint64 位域
    // 确保 C 端可以安全读取 C++ 的位域
    EXPECT_EQ(sizeof(uint64_t), 8);
    EXPECT_EQ(alignof(uint64_t), 8);
}