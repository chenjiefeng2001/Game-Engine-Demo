/**
 * @file ComponentLifecycleTest.cpp
 * @brief ECS 组件生命周期测试（生命周期管理已在 EntityManager 内部处理）
 *
 * 注意：当前 ECS 实现中组件的构造/析构由 Archetype Chunk 管理。
 * 本测试只保留基础验证。
 */
#include <gtest/gtest.h>
#include "Engine/Core/ECS/ECS.h"

using namespace Engine;

TEST(ECSComponentLifecycle, ArchetypeMigrationOnAdd) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();

    // 添加 Position 后，实体迁移到带 Position 的 Archetype
    em.AddComponent<Position>(e);
    EXPECT_NE(em.GetComponent<Position>(e), nullptr);

    // 添加 Velocity 后，实体迁移到带 Position+Velocity 的 Archetype
    em.AddComponent<Velocity>(e);
    EXPECT_NE(em.GetComponent<Position>(e), nullptr);
    EXPECT_NE(em.GetComponent<Velocity>(e), nullptr);
}

TEST(ECSComponentLifecycle, RemoveAndReAdd) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();
    em.AddComponent<Position>(e);

    em.RemoveComponent<Position>(e);
    EXPECT_EQ(em.GetComponent<Position>(e), nullptr);

    // 重新添加
    em.AddComponent<Position>(e);
    EXPECT_NE(em.GetComponent<Position>(e), nullptr);
}