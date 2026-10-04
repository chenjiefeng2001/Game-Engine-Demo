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

// Deliberately never registered: pins the behaviour of a component type that
// has no ComponentMeta, which is the path guarded in AddComponentRaw.
struct UnregisteredComponent {
    float value = 0.0f;
};

// Position and Velocity are test-local types. ComponentRegistry does not
// register them automatically, so the suite registers them once. Whether an
// unregistered type ought to be accepted at all is a separate open question.
class ECSBasicTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        RegisterComponentType<Position>();
        RegisterComponentType<Velocity>();
    }
};

TEST_F(ECSBasicTest, CreateEntityHasUniqueID) {
    EntityManager em;
    EntityHandle e1 = em.CreateEntity();
    EntityHandle e2 = em.CreateEntity();
    EXPECT_NE(e1.Index(), e2.Index());
    EXPECT_TRUE(em.IsAlive(e1));
    EXPECT_TRUE(em.IsAlive(e2));
}

TEST_F(ECSBasicTest, DestroyEntity) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();
    EXPECT_TRUE(em.IsAlive(e));

    em.DestroyEntity(e);
    EXPECT_FALSE(em.IsAlive(e));
}

TEST_F(ECSBasicTest, AddAndGetComponent) {
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

TEST_F(ECSBasicTest, RemoveComponent) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();
    em.AddComponent<Position>(e);
    EXPECT_TRUE(em.HasComponent<Position>(e));

    em.RemoveComponent<Position>(e);
    EXPECT_FALSE(em.HasComponent<Position>(e));
}

TEST_F(ECSBasicTest, GetComponentReturnsNullForMissing) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();

    Position* pos = em.GetComponent<Position>(e);
    EXPECT_EQ(pos, nullptr);
}

TEST_F(ECSBasicTest, DefaultComponentConstructor) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();
    Position& pos = em.AddComponent<Position>(e);

    // Position 的成员应被值初始化（float = 0.0f）
    EXPECT_FLOAT_EQ(pos.x, 0.0f);
    EXPECT_FLOAT_EQ(pos.y, 0.0f);
    EXPECT_FLOAT_EQ(pos.z, 0.0f);
}

TEST_F(ECSBasicTest, MultipleComponents) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();

    em.AddComponent<Position>(e);
    em.AddComponent<Velocity>(e);

    Position* pos = em.GetComponent<Position>(e);
    Velocity* vel = em.GetComponent<Velocity>(e);
    ASSERT_NE(pos, nullptr);
    ASSERT_NE(vel, nullptr);
}

TEST_F(ECSBasicTest, QueryEntitiesWithComponent) {
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

TEST_F(ECSBasicTest, QueryExcludeComponent) {
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

TEST_F(ECSBasicTest, EntityCount) {
    EntityManager em;
    EXPECT_EQ(em.GetEntityCount(), 0);

    EntityHandle e1 = em.CreateEntity();
    EntityHandle e2 = em.CreateEntity();
    EXPECT_EQ(em.GetEntityCount(), 2);

    em.DestroyEntity(e1);
    EXPECT_EQ(em.GetEntityCount(), 1);
}

// 大规模压力测试
TEST_F(ECSBasicTest, TenThousandEntities) {
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

// ── 未注册类型的行为契约 ──────────────────────────────────
// AddComponentRaw 拒绝没有 ComponentMeta 的类型。AddComponent 的返回类型是
// T&，无法表达失败，因此返回一个 per-thread fallback；调用方用
// HasComponent<T>() 判定。这些用例固定该契约，防止其回退成未定义行为。

TEST_F(ECSBasicTest, AddUnregisteredComponent_ToBareEntity_LeavesItUnchanged) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();

    em.AddComponent<UnregisteredComponent>(e);

    EXPECT_FALSE(em.HasComponent<UnregisteredComponent>(e));
    EXPECT_EQ(em.GetComponent<UnregisteredComponent>(e), nullptr);
}

TEST_F(ECSBasicTest, AddUnregisteredComponent_PreservesRegisteredComponents) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();
    em.AddComponent<Position>(e).x = 7.0f;

    em.AddComponent<UnregisteredComponent>(e);

    EXPECT_FALSE(em.HasComponent<UnregisteredComponent>(e));
    ASSERT_NE(em.GetComponent<Position>(e), nullptr);
    EXPECT_FLOAT_EQ(em.GetComponent<Position>(e)->x, 7.0f);
}

TEST_F(ECSBasicTest, AddUnregisteredComponent_ReturnedReferenceIsWritable) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();

    UnregisteredComponent& ref = em.AddComponent<UnregisteredComponent>(e);
    ref.value = 3.0f;

    // 引用本身可用且指向有效存储；组件并未挂到实体上。
    EXPECT_FLOAT_EQ(ref.value, 3.0f);
    EXPECT_FALSE(em.HasComponent<UnregisteredComponent>(e));
}

TEST_F(ECSBasicTest, AddUnregisteredComponent_DoesNotAutoRegister) {
    EntityManager em;
    EntityHandle e = em.CreateEntity();

    em.AddComponent<UnregisteredComponent>(e);

    // 守卫不得顺带自动注册：重复添加仍然失败，说明没有生成 ComponentMeta。
    EXPECT_EQ(
        GetComponentMetaByTypeID(ComponentType<UnregisteredComponent>::ID()),
        nullptr
    );
    em.AddComponent<UnregisteredComponent>(e);
    EXPECT_FALSE(em.HasComponent<UnregisteredComponent>(e));
}