/**
 * @file EntityManagerTest.cpp
 * @brief ECS EntityManager 单元测试
 *
 * 测试重点：
 * - 实体创建/销毁/唯一 ID
 * - 组件添加/获取/删除
 * - 大规模实体压力 (10K)
 * - 迭代器失效测试
 */
#include <gtest/gtest.h>
#include "Engine/Core/ECS/ECS.h"
#include <memory>

using namespace Engine;

// 测试用组件
struct Position {
    float x = 0, y = 0, z = 0;
};

struct Velocity {
    float vx = 0, vy = 0, vz = 0;
};

TEST(ECSBasicTest, CreateEntityHasUniqueID) {
    EntityManager em;
    EntityHandle e1 = em.CreateEntity();
    EntityHandle e2 = em.CreateEntity();
    EXPECT_NE(e1.Index(), e2.Index());
    EXPECT_TRUE(em.IsAlive(e1));
    EXPECT_TRUE(em.IsAlive(e2));
}

TEST(ECSBasicTest, DestroyEntity) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();
    EXPECT_TRUE(em.IsAlive(e));

    em.DestroyEntity(e);
    EXPECT_FALSE(em.IsAlive(e));
}

TEST(ECSBasicTest, AddAndGetComponent) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();

    Position& pos = em.AddComponent<Position>(e);
    pos.x = 10.0f;
    pos.y = 20.0f;
    pos.z = 30.0f;

    Position* retrieved = em.GetComponent<Position>(e);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_FLOAT_EQ(retrieved->x, 10.0f);
    EXPECT_FLOAT_EQ(retrieved->y, 20.0f);
    EXPECT_FLOAT_EQ(retrieved->z, 30.0f);
}

TEST(ECSBasicTest, RemoveComponent) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();
    em.AddComponent<Position>(e);
    EXPECT_TRUE(em.HasComponent<Position>(e));

    em.RemoveComponent<Position>(e);
    EXPECT_FALSE(em.HasComponent<Position>(e));
}

TEST(ECSBasicTest, GetComponentReturnsNullForMissing) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();

    Position* pos = em.GetComponent<Position>(e);
    EXPECT_EQ(pos, nullptr);
}

TEST(ECSBasicTest, DefaultComponentConstructor) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();
    Position& pos = em.AddComponent<Position>(e);

    // Position 的成员应被值初始化（float = 0.0f）
    EXPECT_FLOAT_EQ(pos.x, 0.0f);
    EXPECT_FLOAT_EQ(pos.y, 0.0f);
    EXPECT_FLOAT_EQ(pos.z, 0.0f);
}

TEST(ECSBasicTest, MultipleComponents) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();

    em.AddComponent<Position>(e);
    em.AddComponent<Velocity>(e);

    Position* pos = em.GetComponent<Position>(e);
    Velocity* vel = em.GetComponent<Velocity>(e);
    ASSERT_NE(pos, nullptr);
    ASSERT_NE(vel, nullptr);
}

TEST(ECSBasicTest, QueryEntitiesWithComponent) {
    EntityManager em;

    // 创建 10 个实体，前 5 个带 Position+Velocity，后 5 个只有 Position
    std::vector<EntityHandle> entities;
    for (int i = 0; i < 10; ++i) {
        EntityHandle e = em.CreateEntity();
        em.AddComponent<Position>(e);
        if (i < 5) {
            em.AddComponent<Velocity>(e);
        }
        entities.push_back(e);
    }

    // 查询所有带 Position 的实体
    auto query = em.Query().With<Position>().Build();
    EXPECT_TRUE(query.IsValid());
    EXPECT_GT(query.Size(), 0);

    // 查询同时带 Position + Velocity 的实体
    auto queryBoth = em.Query().With<Position>().With<Velocity>().Build();
    EXPECT_GT(queryBoth.Size(), 0);
}

TEST(ECSBasicTest, QueryExcludeComponent) {
    EntityManager em;
    for (int i = 0; i < 10; ++i) {
        EntityHandle e = em.CreateEntity();
        em.AddComponent<Position>(e);
        if (i < 3) {
            em.AddComponent<Velocity>(e);
        }
    }

    // 查询带 Position 但不带 Velocity 的实体
    auto query = em.Query().With<Position>().Without<Velocity>().Build();
    EXPECT_GT(query.Size(), 0);
}

TEST(ECSBasicTest, EntityCount) {
    EntityManager em;
    EXPECT_EQ(em.GetEntityCount(), 0);

    EntityHandle e1 = em.CreateEntity();
    EntityHandle e2 = em.CreateEntity();
    EXPECT_EQ(em.GetEntityCount(), 2);

    em.DestroyEntity(e1);
    EXPECT_EQ(em.GetEntityCount(), 1);
}

// 大规模压力测试
TEST(ECSBasicTest, TenThousandEntities) {
    EntityManager em;
    std::vector<EntityHandle> entities;
    entities.reserve(10000);

    for (int i = 0; i < 10000; ++i) {
        EntityHandle e = em.CreateEntity();
        em.AddComponent<Position>(e);
        entities.push_back(e);
    }

    EXPECT_EQ(em.GetEntityCount(), 10000);

    // 验证所有实体存在
    for (auto& e : entities) {
        EXPECT_NE(em.GetComponent<Position>(e), nullptr);
    }

    // 删除所有
    for (auto& e : entities) {
        em.DestroyEntity(e);
    }
    EXPECT_EQ(em.GetEntityCount(), 0);
}