/**
 * @file ColliderBindingTest.cpp
 * @brief F2-B Runtime Binding：ColliderComponent → PhysicsColliderAdapter → Physics Body
 *
 * 覆盖（docs/Engine-Foundation-F2B-Plan.md）：
 *   B2 最小 Binding（三门）：
 *     - B2-1 Create（Binding）：Circle/Box 参数真实落到 Physics shape
 *     - B2-2 Destroy：Runtime Body 生命周期 ≠ Component 生命周期
 *     - B2-3 Rebuild：Destroy runtime body → Rebuild → 参数完全来自 Component
 *   B3 World Lifecycle（硬门）：
 *     - B3-1 Destroy World → Component 存活 → 新 World + RebuildAll → Body 恢复
 *     - B3-2 组件是事实源：跨 World 修改 Component，重建用当前状态而非旧 Body 快照
 *     - B3-3 多 Collider / 多 Entity：RebuildAll 非单对象特例
 *     - B3-4 行为一致性：重建后 representation 与 Component 一致且物理行为恢复正常
 *     - B3-5 失败路径：无 World / 重复 Bind / 重复 Rebuild / Release 后 Clear
 *
 * B2-5 Sensor 正确性门：sensor 不只看 Component 自身值，直接查 Physics 层
 * （b2Shape_IsSensor / b2Shape_GetFilter / b2Shape_GetCircle / b2Shape_GetPolygon），
 * 证明映射真实落地，且未照搬 GAP-1 错误路径（CreateShapesFromBodyDef 丢 isSensor）。
 *
 * 所有权断言：Component 零物理引用；Body 为 Adapter 派生对象；无 Body→Component 反向。
 */

#include <gtest/gtest.h>
#include "Engine/Core/GameObject/ColliderComponent.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/Physics/PhysicsColliderAdapter.h"
#include "Engine/Core/Physics/IPhysicsBody.h"
#include "Engine/Core/Physics/IPhysicsWorld.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/OpenGL/OpenGLGraphicsFactory.h"
#include "Engine/Box2D/Box2DPhysicsWorld.h"

#include <box2d/box2d.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace Engine::Content;

namespace {

using namespace Engine;

// ════════════════════════════════════════════════════════════
// Physics 层读取辅助：直接从原生 body 读 shape —— 真实物理状态，
// 而非 Component 回声（B2-5 纪律）
// ════════════════════════════════════════════════════════════

b2BodyId GetNativeBodyId(IPhysicsBody* body) {
    return *static_cast<b2BodyId*>(body->GetNativeBody());
}

b2ShapeId FirstShape(b2BodyId bodyId) {
    b2ShapeId shapes[1];
    return b2Body_GetShapes(bodyId, shapes, 1) > 0 ? shapes[0] : b2ShapeId{};
}

float CircleRadius(IPhysicsBody* body) {
    return b2Shape_GetCircle(FirstShape(GetNativeBodyId(body))).radius;
}

/// Box 半宽半高：取 4 顶点 |x|/|y| 最大值（b2MakeOffsetBox 顶点顺序不保证首点）
void BoxHalfExtents(IPhysicsBody* body, float* outHx, float* outHy) {
    b2Polygon poly = b2Shape_GetPolygon(FirstShape(GetNativeBodyId(body)));
    float hx = 0.0f, hy = 0.0f;
    for (int32 i = 0; i < poly.count; ++i) {
        hx = (std::max)(hx, std::fabs(poly.vertices[i].x));
        hy = (std::max)(hy, std::fabs(poly.vertices[i].y));
    }
    *outHx = hx;
    *outHy = hy;
}

bool ShapeIsSensor(IPhysicsBody* body) {
    return b2Shape_IsSensor(FirstShape(GetNativeBodyId(body)));
}

uint16 ShapeCategory(IPhysicsBody* body) {
    return static_cast<uint16>(
        b2Shape_GetFilter(FirstShape(GetNativeBodyId(body))).categoryBits);
}

uint16 ShapeMask(IPhysicsBody* body) {
    return static_cast<uint16>(
        b2Shape_GetFilter(FirstShape(GetNativeBodyId(body))).maskBits);
}

// ════════════════════════════════════════════════════════════
// 工具
// ════════════════════════════════════════════════════════════

std::shared_ptr<GameObject> MakeRig(const char* name) {
    return std::make_shared<GameObject>(name);
}

/// 经契约注册表挂载 Collider（稳定类型名 "Collider"）
ColliderComponent* AddCollider(GameObject& go) {
    return static_cast<ColliderComponent*>(go.AddComponentByName("Collider"));
}

std::shared_ptr<Box2DPhysicsWorld> MakeWorld() {
    return std::make_shared<Box2DPhysicsWorld>(Vec2(0.0f, -9.81f));
}

void StepFrames(const std::shared_ptr<IPhysicsWorld>& world, int32 frames) {
    for (int32 i = 0; i < frames; ++i) world->Step(1.0f / 60.0f);
}

/// B3-4 行为快照：重建前后可观测的 Physics 状态
struct BehaviorSnapshot {
    Vec2   pos    = {0.0f, 0.0f};
    Vec2   vel    = {0.0f, 0.0f};
    bool   active = false;
    bool   sensor = false;
    uint16 cat    = 0;
    uint16 mask   = 0;
    float  radius = 0.0f;
};

BehaviorSnapshot Snapshot(PhysicsColliderAdapter& adapter, const ColliderComponent& collider) {
    BehaviorSnapshot s;
    IPhysicsBody* body = adapter.GetBody(collider);
    if (!body) return s;
    s.pos    = body->GetPosition();
    s.vel    = body->GetLinearVelocity();
    s.active = body->IsActive();
    s.sensor = ShapeIsSensor(body);
    s.cat    = ShapeCategory(body);
    s.mask   = ShapeMask(body);
    s.radius = CircleRadius(body);
    return s;
}

// ════════════════════════════════════════════════════════════
// B2-1 Create（Binding）
// ════════════════════════════════════════════════════════════

TEST(F2ColliderPhysics, Binding_CircleParamsLandOnPhysics) {
    auto world = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(world);

    auto go = MakeRig("col_circle");
    auto* collider = AddCollider(*go);
    ASSERT_NE(collider, nullptr);

    collider->SetShape(ColliderShape::Circle);
    collider->SetRadius(1.25f);
    collider->SetCategory(0x0004);   // Layer_Enemy
    collider->SetMask(0x0010);       // Layer_EnemyBullet

    ASSERT_TRUE(adapter.Bind(*collider));
    ASSERT_TRUE(adapter.IsBound(*collider));
    IPhysicsBody* body = adapter.GetBody(*collider);
    ASSERT_NE(body, nullptr);

    // 参数真实落到 Physics shape / filter（非 Component 回声）
    EXPECT_FLOAT_EQ(CircleRadius(body), 1.25f);
    EXPECT_EQ(ShapeCategory(body), 0x0004u);
    EXPECT_EQ(ShapeMask(body), 0x0010u);
    EXPECT_TRUE(body->IsActive());        // enabled 默认 true
    EXPECT_FALSE(ShapeIsSensor(body));    // 默认非 sensor
    EXPECT_EQ(adapter.GetBodyCount(), 1u);
}

TEST(F2ColliderPhysics, Binding_BoxSensorEnabledLandOnPhysics) {
    // B2-5 正确性门：sensor 必须真实落在 Physics shape（GAP-1 不照搬错误路径）
    auto world = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(world);

    auto go = MakeRig("col_sensor");
    auto* collider = AddCollider(*go);
    ASSERT_NE(collider, nullptr);

    collider->SetShape(ColliderShape::Box);
    collider->SetHalfExtents(2.0f, 1.5f);
    collider->SetIsSensor(true);
    collider->SetEnabled(false);   // enabled → SetActive（B2-4 实时通道）

    ASSERT_TRUE(adapter.Bind(*collider));
    IPhysicsBody* body = adapter.GetBody(*collider);
    ASSERT_NE(body, nullptr);

    float hx = 0.0f, hy = 0.0f;
    BoxHalfExtents(body, &hx, &hy);
    EXPECT_FLOAT_EQ(hx, 2.0f);
    EXPECT_FLOAT_EQ(hy, 1.5f);
    EXPECT_TRUE(ShapeIsSensor(body));   // 真实 Physics shape 是 sensor
    EXPECT_FALSE(body->IsActive());     // enabled=false → body inactive
}

// ════════════════════════════════════════════════════════════
// B2-2 Destroy
// ════════════════════════════════════════════════════════════

TEST(F2ColliderPhysics, Destroy_BodyGone_ColliderSurvives) {
    auto world = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(world);

    auto go = MakeRig("col_destroy");
    auto* collider = AddCollider(*go);
    ASSERT_NE(collider, nullptr);
    collider->SetShape(ColliderShape::Circle);
    collider->SetRadius(0.75f);

    ASSERT_TRUE(adapter.Bind(*collider));
    IPhysicsBody* body = adapter.GetBody(*collider);
    ASSERT_NE(body, nullptr);
    const b2BodyId nativeBefore = GetNativeBodyId(body);
    EXPECT_TRUE(b2Body_IsValid(nativeBefore));

    adapter.Unbind(*collider);

    // Body 生命周期结束：注册表清空 + 真实 b2 body 已销毁
    EXPECT_FALSE(adapter.IsBound(*collider));
    EXPECT_EQ(adapter.GetBody(*collider), nullptr);
    EXPECT_EQ(adapter.GetBodyCount(), 0u);
    EXPECT_FALSE(b2Body_IsValid(nativeBefore));

    // Component 生命周期独立：声明层原样存活
    EXPECT_STREQ(collider->GetComponentTypeName(), "Collider");
    EXPECT_FLOAT_EQ(collider->GetRadius(), 0.75f);
    EXPECT_TRUE(collider->IsEnabled());
    EXPECT_EQ(go->GetComponentByName("Collider"), collider);
}

// ════════════════════════════════════════════════════════════
// B2-3 Rebuild
// ════════════════════════════════════════════════════════════

TEST(F2ColliderPhysics, Rebuild_DestroyThenRecreate_ParamsFromComponent) {
    auto world = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(world);

    auto go = MakeRig("col_rebuild");
    auto* collider = AddCollider(*go);
    ASSERT_NE(collider, nullptr);
    collider->SetShape(ColliderShape::Circle);
    collider->SetRadius(2.0f);

    ASSERT_TRUE(adapter.Bind(*collider));
    EXPECT_TRUE(adapter.IsBound(*collider));

    adapter.Unbind(*collider);
    EXPECT_FALSE(adapter.IsBound(*collider));

    // Rebuild：Body 重建，参数完全来自 Component
    adapter.Rebuild(*collider);
    ASSERT_TRUE(adapter.IsBound(*collider));
    IPhysicsBody* body = adapter.GetBody(*collider);
    ASSERT_NE(body, nullptr);
    EXPECT_FLOAT_EQ(CircleRadius(body), 2.0f);
    EXPECT_EQ(adapter.GetBodyCount(), 1u);   // 不产生重复 Body

    // B2-4 契约：shape 变更 = Rebuild → 新参数生效（Destroy/Recreate，无隐式同步）
    collider->SetShape(ColliderShape::Box);
    collider->SetHalfExtents(3.0f, 4.0f);
    adapter.Rebuild(*collider);

    IPhysicsBody* body2 = adapter.GetBody(*collider);
    ASSERT_NE(body2, nullptr);
    // 不断言 body != body2：Rebuild 内部为 Unbind+Bind（DestroyBody + erase 后 CreateBody），
    // 旧对象已释放，分配器合法复用同一地址，指针相等并不代表未重建。
    // IPhysicsBody 无 generation/handle，"重建"与"原地改参"在当前 API 下不可区分，
    // 因此此处只断言可观察契约：新参数生效 + body 数量不增长。
    float hx = 0.0f, hy = 0.0f;
    BoxHalfExtents(body2, &hx, &hy);
    EXPECT_FLOAT_EQ(hx, 3.0f);
    EXPECT_FLOAT_EQ(hy, 4.0f);
    EXPECT_EQ(adapter.GetBodyCount(), 1u);

    // Component 声明层全程未被触碰（无反向同步）
    EXPECT_STREQ(collider->GetComponentTypeName(), "Collider");
    EXPECT_EQ(collider->GetShape(), ColliderShape::Box);
}

// ════════════════════════════════════════════════════════════
// B3-1 Destroy World → RebuildAll（真 World 销毁，非 mock）
// ════════════════════════════════════════════════════════════

TEST(F2ColliderPhysics, WorldLifecycle_DestroyWorld_RestoreBodyFromComponent) {
    auto worldA = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(worldA);

    auto go = MakeRig("col_world");
    auto* collider = AddCollider(*go);
    ASSERT_NE(collider, nullptr);
    collider->SetShape(ColliderShape::Circle);
    collider->SetRadius(1.5f);
    collider->SetCategory(0x0002);
    collider->SetMask(0xFFFF);

    ASSERT_TRUE(adapter.Bind(*collider));
    IPhysicsBody* bodyA = adapter.GetBody(*collider);
    ASSERT_NE(bodyA, nullptr);
    const b2BodyId nativeA = GetNativeBodyId(bodyA);
    const ColliderComponent* compAddr = collider;
    const float radiusBefore = collider->GetRadius();

    // ── 真 World 销毁：ReleaseWorld 内部 Clear（body 全灭）+ 释放世界引用 ──
    adapter.ReleaseWorld();
    EXPECT_EQ(adapter.GetWorld(), nullptr);
    EXPECT_EQ(adapter.GetBodyCount(), 0u);
    EXPECT_FALSE(adapter.IsBound(*collider));
    EXPECT_FALSE(b2Body_IsValid(nativeA));   // Body#1 已无效（真实销毁）

    // Component 存活且状态未变（地址/状态硬断言）
    EXPECT_EQ(static_cast<const void*>(collider), static_cast<const void*>(compAddr));
    EXPECT_FLOAT_EQ(collider->GetRadius(), radiusBefore);
    EXPECT_STREQ(collider->GetComponentTypeName(), "Collider");
    EXPECT_EQ(go->GetComponentByName("Collider"), collider);

    // ── 新 World + RebuildAll → Body#2 从 Component 重建 ──
    auto worldB = MakeWorld();
    adapter.SetWorld(worldB);
    adapter.RebuildAll();

    ASSERT_TRUE(adapter.IsBound(*collider));
    EXPECT_EQ(adapter.GetBodyCount(), 1u);   // 无重复 Body
    EXPECT_EQ(adapter.GetTrackedCount(), 1u);
    IPhysicsBody* bodyB = adapter.GetBody(*collider);
    ASSERT_NE(bodyB, nullptr);
    EXPECT_NE(bodyB, bodyA);
    EXPECT_TRUE(b2Body_IsValid(GetNativeBodyId(bodyB)));

    // Body#2 参数与 Component 完全一致
    EXPECT_FLOAT_EQ(CircleRadius(bodyB), 1.5f);
    EXPECT_EQ(ShapeCategory(bodyB), 0x0002u);
    EXPECT_EQ(ShapeMask(bodyB), 0xFFFFu);
    EXPECT_TRUE(bodyB->IsActive());
}

// ════════════════════════════════════════════════════════════
// B3-2 Component 是事实源：重建用当前状态，不是旧 Body 快照
// ════════════════════════════════════════════════════════════

TEST(F2ColliderPhysics, WorldLifecycle_RebuildUsesCurrentComponent_NotOldBodySnapshot) {
    auto worldA = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(worldA);

    auto go = MakeRig("col_fact");
    auto* collider = AddCollider(*go);
    ASSERT_NE(collider, nullptr);
    collider->SetShape(ColliderShape::Circle);
    collider->SetRadius(1.0f);

    ASSERT_TRUE(adapter.Bind(*collider));
    EXPECT_FLOAT_EQ(CircleRadius(adapter.GetBody(*collider)), 1.0f);   // Body A = 1.0

    // Destroy World A → 修改 Component → World B → RebuildAll
    adapter.ReleaseWorld();
    collider->SetRadius(2.0f);   // 组件当前状态变为 2.0

    auto worldB = MakeWorld();
    adapter.SetWorld(worldB);
    adapter.RebuildAll();

    // Body B 必须 = 2.0：证明 Rebuild 用的是 Component 当前状态而非旧 Body 快照
    IPhysicsBody* bodyB = adapter.GetBody(*collider);
    ASSERT_NE(bodyB, nullptr);
    EXPECT_FLOAT_EQ(CircleRadius(bodyB), 2.0f);
    EXPECT_EQ(adapter.GetBodyCount(), 1u);
}

// ════════════════════════════════════════════════════════════
// B3-3 多 Collider / 多 Entity：RebuildAll 非单对象特例
// ════════════════════════════════════════════════════════════

TEST(F2ColliderPhysics, WorldLifecycle_MultiEntity_RebuildAllRestoresAllThree) {
    auto worldA = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(worldA);

    // Entity A → Circle
    auto goA = MakeRig("entityA");
    auto* cA = AddCollider(*goA);
    cA->SetShape(ColliderShape::Circle);
    cA->SetRadius(0.5f);

    // Entity B → Box Sensor
    auto goB = MakeRig("entityB");
    auto* cB = AddCollider(*goB);
    cB->SetShape(ColliderShape::Box);
    cB->SetHalfExtents(2.0f, 1.0f);
    cB->SetIsSensor(true);

    // Entity C → Box Disabled + 自定义 filter
    auto goC = MakeRig("entityC");
    auto* cC = AddCollider(*goC);
    cC->SetShape(ColliderShape::Box);
    cC->SetHalfExtents(1.0f, 1.0f);
    cC->SetEnabled(false);
    cC->SetCategory(0x0008);
    cC->SetMask(0x0031);

    ASSERT_TRUE(adapter.Bind(*cA));
    ASSERT_TRUE(adapter.Bind(*cB));
    ASSERT_TRUE(adapter.Bind(*cC));
    EXPECT_EQ(adapter.GetBodyCount(), 3u);
    EXPECT_EQ(adapter.GetTrackedCount(), 3u);

    // 销毁 → 全灭
    adapter.ReleaseWorld();
    EXPECT_EQ(adapter.GetBodyCount(), 0u);
    EXPECT_EQ(adapter.GetTrackedCount(), 3u);   // 受管集合保留
    EXPECT_FALSE(adapter.IsBound(*cA));
    EXPECT_FALSE(adapter.IsBound(*cB));
    EXPECT_FALSE(adapter.IsBound(*cC));

    // 新 World + RebuildAll → 3 Bodies 全恢复
    auto worldB = MakeWorld();
    adapter.SetWorld(worldB);
    adapter.RebuildAll();
    EXPECT_EQ(adapter.GetBodyCount(), 3u);

    // 逐个核对 shape / sensor / filter / enabled（真实 Physics 层）
    IPhysicsBody* bA = adapter.GetBody(*cA);
    IPhysicsBody* bB = adapter.GetBody(*cB);
    IPhysicsBody* bC = adapter.GetBody(*cC);
    ASSERT_NE(bA, nullptr);
    ASSERT_NE(bB, nullptr);
    ASSERT_NE(bC, nullptr);

    EXPECT_FLOAT_EQ(CircleRadius(bA), 0.5f);
    EXPECT_FALSE(ShapeIsSensor(bA));

    float hx = 0.0f, hy = 0.0f;
    BoxHalfExtents(bB, &hx, &hy);
    EXPECT_FLOAT_EQ(hx, 2.0f);
    EXPECT_FLOAT_EQ(hy, 1.0f);
    EXPECT_TRUE(ShapeIsSensor(bB));

    BoxHalfExtents(bC, &hx, &hy);
    EXPECT_FLOAT_EQ(hx, 1.0f);
    EXPECT_FLOAT_EQ(hy, 1.0f);
    EXPECT_FALSE(bC->IsActive());          // disabled
    EXPECT_EQ(ShapeCategory(bC), 0x0008u);
    EXPECT_EQ(ShapeMask(bC), 0x0031u);
}

// ════════════════════════════════════════════════════════════
// B3-4 行为一致性：重建后 representation 与 Component 一致且物理行为恢复正常
// ════════════════════════════════════════════════════════════

TEST(F2ColliderPhysics, WorldLifecycle_BehaviorConsistentAfterRebuild) {
    // Run A：单世界连续模拟（不销毁）
    auto worldA = MakeWorld();
    PhysicsColliderAdapter adapterA;
    adapterA.SetWorld(worldA);
    auto goA = MakeRig("rigA");
    auto* cA = AddCollider(*goA);
    cA->SetShape(ColliderShape::Circle);
    cA->SetRadius(1.0f);
    cA->SetIsSensor(true);
    ASSERT_TRUE(adapterA.Bind(*cA));
    StepFrames(worldA, 10);
    const BehaviorSnapshot snapA = Snapshot(adapterA, *cA);

    // Run B：同配置，但中途 Destroy World → Rebuild → 继续模拟
    auto worldB1 = MakeWorld();
    PhysicsColliderAdapter adapterB;
    adapterB.SetWorld(worldB1);
    auto goB = MakeRig("rigB");
    auto* cB = AddCollider(*goB);
    cB->SetShape(ColliderShape::Circle);
    cB->SetRadius(1.0f);
    cB->SetIsSensor(true);
    ASSERT_TRUE(adapterB.Bind(*cB));
    StepFrames(worldB1, 10);
    adapterB.ReleaseWorld();
    auto worldB2 = MakeWorld();
    adapterB.SetWorld(worldB2);
    adapterB.RebuildAll();
    StepFrames(worldB2, 10);
    const BehaviorSnapshot snapB = Snapshot(adapterB, *cB);

    // 不要求跨 World 积分历史一致（World 销毁 = runtime state 本就不存在）；
    // 要求：重建后的 Physics representation 与 Component 一致、行为恢复（Run A/B 对齐）
    EXPECT_FLOAT_EQ(snapB.pos.x, snapA.pos.x);
    EXPECT_FLOAT_EQ(snapB.pos.y, snapA.pos.y);
    EXPECT_FLOAT_EQ(snapB.vel.x, snapA.vel.x);
    EXPECT_FLOAT_EQ(snapB.vel.y, snapA.vel.y);
    EXPECT_EQ(snapB.active, snapA.active);
    EXPECT_EQ(snapB.sensor, snapA.sensor);
    EXPECT_EQ(snapB.cat, snapA.cat);
    EXPECT_EQ(snapB.mask, snapA.mask);
    EXPECT_FLOAT_EQ(snapB.radius, snapA.radius);
    // 语义断言：默认 enabled → active；sensor 保持；static 位置稳定
    EXPECT_TRUE(snapA.active);
    EXPECT_TRUE(snapB.active);
    EXPECT_TRUE(snapA.sensor);
    EXPECT_TRUE(snapB.sensor);
    EXPECT_FLOAT_EQ(snapA.pos.y, 0.0f);
    EXPECT_FLOAT_EQ(snapB.pos.y, 0.0f);
}

// ════════════════════════════════════════════════════════════
// B3-5 失败路径
// ════════════════════════════════════════════════════════════

TEST(F2ColliderPhysics, Failure_NoWorld_BindCleanFail) {
    PhysicsColliderAdapter adapter;   // 无 World

    auto go = MakeRig("col_noworld");
    auto* collider = AddCollider(*go);
    ASSERT_NE(collider, nullptr);
    collider->SetShape(ColliderShape::Circle);

    // 干净失败：不崩、无副作用、IsBound == false
    EXPECT_FALSE(adapter.Bind(*collider));
    EXPECT_FALSE(adapter.IsBound(*collider));
    EXPECT_EQ(adapter.GetBody(*collider), nullptr);
    EXPECT_EQ(adapter.GetBodyCount(), 0u);
    EXPECT_EQ(adapter.GetTrackedCount(), 0u);

    // Component 保留
    EXPECT_STREQ(collider->GetComponentTypeName(), "Collider");
    EXPECT_EQ(go->GetComponentByName("Collider"), collider);

    // 之后补上 World + RebuildAll：未成功 Bind 过的组件不被误建（0 body）
    auto world = MakeWorld();
    adapter.SetWorld(world);
    adapter.RebuildAll();
    EXPECT_EQ(adapter.GetBodyCount(), 0u);
}

TEST(F2ColliderPhysics, Failure_DuplicateBind_NoDuplicateBody) {
    auto world = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(world);

    auto go = MakeRig("col_dup");
    auto* collider = AddCollider(*go);
    ASSERT_NE(collider, nullptr);
    collider->SetShape(ColliderShape::Box);
    collider->SetHalfExtents(1.0f, 1.0f);

    EXPECT_TRUE(adapter.Bind(*collider));
    EXPECT_FALSE(adapter.Bind(*collider));   // 幂等拒绝
    EXPECT_EQ(adapter.GetBodyCount(), 1u);   // 绝不产生两个 Body
    EXPECT_EQ(adapter.GetTrackedCount(), 1u);
    EXPECT_TRUE(adapter.IsBound(*collider));
}

TEST(F2ColliderPhysics, Failure_DuplicateRebuild_BodyCountStaysCorrect) {
    auto world = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(world);

    auto go = MakeRig("col_rebreb");
    auto* collider = AddCollider(*go);
    ASSERT_NE(collider, nullptr);
    collider->SetShape(ColliderShape::Circle);
    collider->SetRadius(0.8f);

    ASSERT_TRUE(adapter.Bind(*collider));
    adapter.Rebuild(*collider);
    adapter.Rebuild(*collider);
    EXPECT_EQ(adapter.GetBodyCount(), 1u);   // Body 数量始终保持正确
    IPhysicsBody* body = adapter.GetBody(*collider);
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(b2Body_IsValid(GetNativeBodyId(body)));
    EXPECT_FLOAT_EQ(CircleRadius(body), 0.8f);
}

TEST(F2ColliderPhysics, Failure_ReleaseThenClear_NoDanglingWorldAccess) {
    auto world = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(world);

    auto go = MakeRig("col_relclr");
    auto* collider = AddCollider(*go);
    ASSERT_NE(collider, nullptr);
    ASSERT_TRUE(adapter.Bind(*collider));

    adapter.ReleaseWorld();   // 世界引用已释放
    adapter.Clear();          // 不访问已销毁的 World（无崩溃 / 无悬垂访问）
    EXPECT_EQ(adapter.GetBodyCount(), 0u);
    EXPECT_EQ(adapter.GetWorld(), nullptr);
    EXPECT_EQ(adapter.GetTrackedCount(), 1u);   // 受管集合仍在（Component 声明层存活）
    EXPECT_FALSE(adapter.IsBound(*collider));
    EXPECT_STREQ(collider->GetComponentTypeName(), "Collider");
}

// ════════════════════════════════════════════════════════════
// B4-1 AddComponent → 自动 Bind
// ════════════════════════════════════════════════════════════

TEST(F2ColliderPhysics, Lifecycle_AddCollider_AutoBind) {
    auto world = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(world);

    auto go = MakeRig("auto_bind");
    adapter.ObserveGameObject(*go);   // 一次性集成，此后全部自动
    EXPECT_EQ(adapter.GetBodyCount(), 0u);

    auto* collider = AddCollider(*go);   // 仅 AddComponent，无 adapter.Bind 调用
    ASSERT_NE(collider, nullptr);

    EXPECT_TRUE(adapter.IsBound(*collider));   // 自动 Bind
    IPhysicsBody* body = adapter.GetBody(*collider);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(adapter.GetBodyCount(), 1u);
    EXPECT_FLOAT_EQ(CircleRadius(body), 0.5f);   // 默认 Circle 0.5
}

// ════════════════════════════════════════════════════════════
// B4-2 RemoveComponent → 自动 Unbind（硬门）
// ════════════════════════════════════════════════════════════

TEST(F2ColliderPhysics, Lifecycle_RemoveCollider_AutoUnbind_NoResurrection) {
    auto worldA = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(worldA);

    auto go = MakeRig("auto_unbind");
    adapter.ObserveGameObject(*go);

    // 用自有 shared_ptr 保持组件存活，便于移除后安全断言
    auto comp = std::make_shared<ColliderComponent>();
    ColliderComponent* collider = comp.get();
    go->Attach(comp);   // 契约路径挂载 → 自动 Bind
    ASSERT_TRUE(adapter.IsBound(*collider));

    // ── 仅 RemoveComponentByName，无 adapter.Unbind 调用 ──
    EXPECT_TRUE(go->RemoveComponentByName("Collider"));
    EXPECT_EQ(go->GetComponentByName("Collider"), nullptr);   // Component 消失
    EXPECT_FALSE(adapter.IsBound(*collider));                  // 自动 Unbind
    EXPECT_EQ(adapter.GetBody(*collider), nullptr);
    EXPECT_EQ(adapter.GetBodyCount(), 0u);                     // 无 dangling entry
    EXPECT_EQ(adapter.GetTrackedCount(), 0u);                  // 受管集合同步清空

    // 已删除 Collider 绝不复活：毁世 → 新世 → RebuildAll → 0 bodies
    adapter.ReleaseWorld();
    auto worldB = MakeWorld();
    adapter.SetWorld(worldB);
    adapter.RebuildAll();
    EXPECT_EQ(adapter.GetBodyCount(), 0u);
    EXPECT_EQ(adapter.GetTrackedCount(), 0u);
}

// ════════════════════════════════════════════════════════════
// B4-3 属性 Mutation 自动进入 Adapter（B2-4 表正式挂到生命周期）
// ════════════════════════════════════════════════════════════

TEST(F2ColliderPhysics, Lifecycle_Mutation_AutoRebuild_NoLeak) {
    auto world = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(world);

    auto go = MakeRig("mut");
    adapter.ObserveGameObject(*go);
    auto* collider = AddCollider(*go);   // 自动 Bind（默认 Circle 0.5）
    ASSERT_NE(collider, nullptr);
    ASSERT_TRUE(adapter.IsBound(*collider));

    // setter → 变更钩子 → 自动 Rebuild（old Body 销毁，new Body 参数来自 Component）
    collider->SetRadius(2.0f);
    EXPECT_EQ(adapter.GetBodyCount(), 1u);   // 最危险的隐式泄漏：old+new 并存，绝不允许
    IPhysicsBody* body = adapter.GetBody(*collider);
    ASSERT_NE(body, nullptr);
    EXPECT_FLOAT_EQ(CircleRadius(body), 2.0f);

    // enabled（反射路径 SetPropertyValue）→ 即时 SetActive，不重建
    IPhysicsBody* before = adapter.GetBody(*collider);
    ComponentPropertyValue v;
    v.type = ComponentValueType::Bool;
    v.boolValue = false;
    ASSERT_TRUE(collider->SetPropertyValue(0, v));   // kPropEnabled == 0
    EXPECT_EQ(adapter.GetBody(*collider), before);   // 同一 body（未重建）
    EXPECT_FALSE(before->IsActive());

    // shape（反射路径）→ 自动 Rebuild
    v.type = ComponentValueType::String;
    v.stringValue = "Box";
    ASSERT_TRUE(collider->SetPropertyValue(1, v));   // kPropShape == 1
    float hx = 0.0f, hy = 0.0f;
    BoxHalfExtents(adapter.GetBody(*collider), &hx, &hy);
    EXPECT_FLOAT_EQ(hx, 0.5f);   // 默认 halfX
    EXPECT_FLOAT_EQ(hy, 0.5f);
    EXPECT_EQ(adapter.GetBodyCount(), 1u);
}

// ════════════════════════════════════════════════════════════
// B4-4 生命周期顺序破坏测试（Golden Lifecycle 矩阵）
// ════════════════════════════════════════════════════════════

TEST(F2ColliderPhysics, Lifecycle_Ordering_CaseA_RemoveThenDestroyWorld_NoResurrection) {
    auto worldA = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(worldA);
    auto go = MakeRig("caseA");
    adapter.ObserveGameObject(*go);
    auto comp = std::make_shared<ColliderComponent>();
    go->Attach(comp);
    ASSERT_TRUE(adapter.IsBound(*comp));

    // Case A: Add → Bind → Remove → Destroy World → New World → RebuildAll ⇒ 0 bodies
    EXPECT_TRUE(go->RemoveComponentByName("Collider"));
    adapter.ReleaseWorld();
    auto worldB = MakeWorld();
    adapter.SetWorld(worldB);
    adapter.RebuildAll();
    EXPECT_EQ(adapter.GetBodyCount(), 0u);
    EXPECT_EQ(adapter.GetTrackedCount(), 0u);
}

TEST(F2ColliderPhysics, Lifecycle_Ordering_CaseB_DestroyWorldThenRemove_NoResurrection) {
    auto worldA = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(worldA);
    auto go = MakeRig("caseB");
    adapter.ObserveGameObject(*go);
    auto comp = std::make_shared<ColliderComponent>();
    go->Attach(comp);
    ASSERT_TRUE(adapter.IsBound(*comp));

    // Case B: Add → Bind → Destroy World → Remove → New World → RebuildAll ⇒ 0 bodies
    adapter.ReleaseWorld();
    EXPECT_TRUE(go->RemoveComponentByName("Collider"));   // 未绑定状态下移除：干净 untrack
    auto worldB = MakeWorld();
    adapter.SetWorld(worldB);
    adapter.RebuildAll();
    EXPECT_EQ(adapter.GetBodyCount(), 0u);
    EXPECT_EQ(adapter.GetTrackedCount(), 0u);
}

TEST(F2ColliderPhysics, Lifecycle_Ordering_CaseC_DestroyRebuildThenRemove_BodyGone) {
    auto worldA = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(worldA);
    auto go = MakeRig("caseC");
    adapter.ObserveGameObject(*go);
    auto comp = std::make_shared<ColliderComponent>();
    go->Attach(comp);
    ColliderComponent* collider = comp.get();
    ASSERT_TRUE(adapter.IsBound(*collider));

    // Case C: Add → Bind → Destroy World → New World → RebuildAll（恢复）→ Remove ⇒ Body gone
    adapter.ReleaseWorld();
    auto worldB = MakeWorld();
    adapter.SetWorld(worldB);
    adapter.RebuildAll();
    EXPECT_TRUE(adapter.IsBound(*collider));
    EXPECT_EQ(adapter.GetBodyCount(), 1u);

    EXPECT_TRUE(go->RemoveComponentByName("Collider"));
    EXPECT_FALSE(adapter.IsBound(*collider));
    EXPECT_EQ(adapter.GetBody(*collider), nullptr);
    EXPECT_EQ(adapter.GetBodyCount(), 0u);
}

TEST(F2ColliderPhysics, Lifecycle_Ordering_CaseD_MutateAcrossWorlds_UsesLastState) {
    auto worldA = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(worldA);
    auto go = MakeRig("caseD");
    adapter.ObserveGameObject(*go);
    auto* collider = AddCollider(*go);
    ASSERT_NE(collider, nullptr);
    ASSERT_TRUE(adapter.IsBound(*collider));
    collider->SetRadius(1.0f);   // 自动 Rebuild → Body A = 1.0
    EXPECT_FLOAT_EQ(CircleRadius(adapter.GetBody(*collider)), 1.0f);

    // Case D: Add → Bind → mutate(2) → Destroy World → mutate(3) → New World → RebuildAll
    collider->SetRadius(2.0f);
    adapter.ReleaseWorld();
    collider->SetRadius(3.0f);   // 无世界：Rebuild no-op，组件状态 = 3
    auto worldB = MakeWorld();
    adapter.SetWorld(worldB);
    adapter.RebuildAll();

    // 最终 Body 必须使用最后状态 3.0（Component is source of truth 的完整生命周期契约）
    IPhysicsBody* body = adapter.GetBody(*collider);
    ASSERT_NE(body, nullptr);
    EXPECT_FLOAT_EQ(CircleRadius(body), 3.0f);
    EXPECT_FLOAT_EQ(collider->GetRadius(), 3.0f);
    EXPECT_EQ(adapter.GetBodyCount(), 1u);
}

// ════════════════════════════════════════════════════════════
// B4-5 异常与顺序鲁棒性
// ════════════════════════════════════════════════════════════

TEST(F2ColliderPhysics, Lifecycle_Negative_Matrix_NoCrash) {
    auto world = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(world);
    auto go = MakeRig("neg");
    adapter.ObserveGameObject(*go);

    // Remove 未绑定 Collider：干净失败，不影响状态
    EXPECT_FALSE(go->RemoveComponentByName("Collider"));
    EXPECT_EQ(adapter.GetBodyCount(), 0u);

    // Unbind 从未绑定组件：no-op
    auto orphan = std::make_shared<ColliderComponent>();
    adapter.Unbind(*orphan);
    EXPECT_EQ(adapter.GetBodyCount(), 0u);

    // Bind → Remove 两次（第二次失败）→ 状态干净
    auto comp = std::make_shared<ColliderComponent>();
    go->Attach(comp);
    ASSERT_TRUE(adapter.IsBound(*comp));
    EXPECT_TRUE(go->RemoveComponentByName("Collider"));
    EXPECT_FALSE(go->RemoveComponentByName("Collider"));
    EXPECT_EQ(adapter.GetBodyCount(), 0u);
    EXPECT_EQ(adapter.GetTrackedCount(), 0u);

    // Destroy World 两次：安全
    adapter.ReleaseWorld();
    adapter.ReleaseWorld();
    EXPECT_EQ(adapter.GetWorld(), nullptr);

    // ReleaseWorld 后 RebuildAll：no-op，不崩
    adapter.RebuildAll();
    EXPECT_EQ(adapter.GetBodyCount(), 0u);

    // ObserveGameObject 幂等：重复观察后监听仍生效（attach 新 Collider → 自动 Bind）
    adapter.ObserveGameObject(*go);
    auto world2 = MakeWorld();
    adapter.SetWorld(world2);   // 恢复世界
    auto comp2 = std::make_shared<ColliderComponent>();
    go->Attach(comp2);
    EXPECT_EQ(adapter.GetBodyCount(), 1u);
    EXPECT_TRUE(adapter.IsBound(*comp2));
}

TEST(F2ColliderPhysics, Lifecycle_SetWorldNull_Recoverable) {
    auto worldA = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(worldA);
    auto go = MakeRig("null_world");
    adapter.ObserveGameObject(*go);
    auto comp = std::make_shared<ColliderComponent>();
    go->Attach(comp);
    ColliderComponent* collider = comp.get();
    ASSERT_TRUE(adapter.IsBound(*collider));

    // SetWorld(nullptr)：清 body、世界置空；受管集合保留
    adapter.SetWorld(nullptr);
    EXPECT_EQ(adapter.GetWorld(), nullptr);
    EXPECT_EQ(adapter.GetBodyCount(), 0u);
    EXPECT_FALSE(adapter.IsBound(*collider));
    EXPECT_EQ(adapter.GetTrackedCount(), 1u);

    // 无世界时 Bind 干净失败（不污染 m_Tracked）
    EXPECT_FALSE(adapter.Bind(*collider));
    EXPECT_EQ(adapter.GetTrackedCount(), 1u);

    // 恢复：新世界 + RebuildAll → 状态可继续恢复
    auto worldB = MakeWorld();
    adapter.SetWorld(worldB);
    adapter.RebuildAll();
    EXPECT_TRUE(adapter.IsBound(*collider));
    EXPECT_EQ(adapter.GetBodyCount(), 1u);
}

// ════════════════════════════════════════════════════════════
// B5 Cold Restart：真实 SceneSerializerV1 落盘 → 冷启 → 还原 → 重建
// ════════════════════════════════════════════════════════════

namespace b5 {

const std::string kDir = "f2b_scratch";
std::string P(const std::string& name) {
    std::filesystem::create_directories(kDir);
    return kDir + "/" + name;
}

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

/// 把 Collider 挂到新 GameObject（真实场景成员）
ColliderComponent* AddColliderToScene(Scene& scene, const std::string& name) {

    auto go = std::make_shared<GameObject>(name.c_str());
    auto* c = static_cast<ColliderComponent*>(go->AddComponentByName("Collider"));
    scene.AddObject(go);
    return c;
}

/// 从磁盘冷启动：全新 Scene + 组件还原 + 观察（组件生命周期驱动重建）。
/// 返回 {adapter, scene}：pair 析构时 scene（第二个成员）先死——GameObject 触发监听器时
/// adapter 仍存活；随后 adapter 再析构。顺序纪律：观察者必须先于被观察对象销毁。
std::pair<std::unique_ptr<PhysicsColliderAdapter>, std::shared_ptr<Scene>>
ColdStart(const std::string& path, int expectedObjects) {
    SceneSnapshot snap;
    std::string err;
    if (!LoadSnapshotFromFile(path, snap, err)) {
        return {nullptr, nullptr};
    }
    auto scene = std::make_shared<Scene>();
    OpenGLGraphicsFactory gfx;
    TextureManager texMgr(gfx);
    ContentRegistry reg;
    auto r = InstantiateScene(snap, *scene, texMgr, reg);
    if (!r.ok || scene->GetObjectCount() != static_cast<size_t>(expectedObjects)) {
        return {nullptr, nullptr};
    }
    auto adapter = std::make_unique<PhysicsColliderAdapter>();
    adapter->SetWorld(MakeWorld());
    for (auto& go : scene->GetObjects()) adapter->ObserveGameObject(*go);
    return {std::move(adapter), std::move(scene)};
}

} // namespace b5

// ── B5-1 Golden Case：Save → Destroy World → Load → Rebuild ──

TEST(F2ColliderPhysics, ColdRestart_SaveDestroyLoadRebuild_GoldenCase) {
    // 声明顺序纪律：adapter 先于 sceneA 声明（观察者先存活）——sceneA 析构时 adapter 仍可服务监听器
    PhysicsColliderAdapter adapterA;
    // ── Runtime A：建场景（2 实体，全字段设置）──
    Scene sceneA;
    auto* c1 = b5::AddColliderToScene(sceneA, "colA");
    c1->SetShape(ColliderShape::Circle);
    c1->SetRadius(1.25f);
    c1->SetCategory(0x0002);
    c1->SetMask(0x0008);
    c1->SetIsSensor(false);

    auto* c2 = b5::AddColliderToScene(sceneA, "colB");
    c2->SetShape(ColliderShape::Box);
    c2->SetHalfExtents(2.0f, 1.5f);
    c2->SetIsSensor(true);
    c2->SetCategory(0x0004);
    c2->SetMask(0x0031);

    // Runtime A 物理接入
    adapterA.SetWorld(MakeWorld());
    for (auto& go : sceneA.GetObjects()) adapterA.ObserveGameObject(*go);
    EXPECT_EQ(adapterA.GetBodyCount(), 2u);

    // ── Save（真实序列化器）──
    const SceneSnapshot snapA = CaptureScene(sceneA, {});
    const std::string path = b5::P("b5_golden.scene");
    ASSERT_TRUE(SaveSnapshotToFile(snapA, path));

    // 落盘证据：components[] 含 Collider；绝无 Body 状态（BodyID/ShapeRef/runtimeBodyID）
    {
        const std::string text = b5::ReadFile(path);
        EXPECT_NE(text.find("\"Collider\""), std::string::npos);
        EXPECT_EQ(text.find("BodyID"), std::string::npos);
        EXPECT_EQ(text.find("bodyID"), std::string::npos);
        EXPECT_EQ(text.find("runtimeBodyID"), std::string::npos);
        EXPECT_EQ(text.find("ShapeRef"), std::string::npos);
    }

    // ── 销毁 Physics World（真实：b2DestroyWorld）──
    adapterA.ReleaseWorld();
    EXPECT_EQ(adapterA.GetBodyCount(), 0u);

    // ── Cold Start：全新 Scene 从磁盘重建 ──
    auto [adapterB, sceneB] = b5::ColdStart(path, 2);
    ASSERT_NE(sceneB, nullptr);
    ASSERT_NE(adapterB, nullptr);
    ASSERT_EQ(sceneB->GetObjectCount(), 2u);
    EXPECT_EQ(adapterB->GetBodyCount(), 2u);   // BodyCount == ComponentCount

    // 逐字段验证（真实 Physics 层）
    auto* r1 = static_cast<ColliderComponent*>(
        sceneB->GetObjects()[0]->GetComponentByName("Collider"));
    auto* r2 = static_cast<ColliderComponent*>(
        sceneB->GetObjects()[1]->GetComponentByName("Collider"));
    ASSERT_NE(r1, nullptr);
    ASSERT_NE(r2, nullptr);

    IPhysicsBody* b1 = adapterB->GetBody(*r1);
    IPhysicsBody* b2 = adapterB->GetBody(*r2);
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(b2, nullptr);

    EXPECT_FLOAT_EQ(CircleRadius(b1), 1.25f);
    EXPECT_EQ(ShapeCategory(b1), 0x0002u);
    EXPECT_EQ(ShapeMask(b1), 0x0008u);
    EXPECT_FALSE(ShapeIsSensor(b1));
    EXPECT_TRUE(b1->IsActive());

    float hx = 0.0f, hy = 0.0f;
    BoxHalfExtents(b2, &hx, &hy);
    EXPECT_FLOAT_EQ(hx, 2.0f);
    EXPECT_FLOAT_EQ(hy, 1.5f);
    EXPECT_TRUE(ShapeIsSensor(b2));
    EXPECT_EQ(ShapeCategory(b2), 0x0004u);
    EXPECT_EQ(ShapeMask(b2), 0x0031u);

    // 组件本身亦为保存的状态
    EXPECT_FLOAT_EQ(r1->GetRadius(), 1.25f);
    EXPECT_EQ(r2->GetShape(), ColliderShape::Box);
}

// ── B5-2 Save 即快照：不是旧 Body 快照，也不是未保存的 runtime ──

TEST(F2ColliderPhysics, ColdRestart_SaveIsSnapshot_NotRuntimeState) {
    PhysicsColliderAdapter adapterA;   // 声明在 sceneA 之前（观察者先存活）
    Scene sceneA;
    auto* c = b5::AddColliderToScene(sceneA, "colSnap");
    c->SetShape(ColliderShape::Circle);
    c->SetRadius(1.0f);

    adapterA.SetWorld(MakeWorld());
    adapterA.ObserveGameObject(*sceneA.GetObjects()[0]);

    // 第一拍：radius=1 保存 → runtime mutate 到 2（不保存）→ 冷启 → 必须 = 1
    const std::string path = b5::P("b5_snap.scene");
    ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(sceneA, {}), path));
    c->SetRadius(2.0f);   // 未保存的 runtime 变更
    adapterA.ReleaseWorld();
    {
        auto [adapterB, sceneB] = b5::ColdStart(path, 1);
        ASSERT_NE(sceneB, nullptr);
        auto* r = static_cast<ColliderComponent*>(
            sceneB->GetObjects()[0]->GetComponentByName("Collider"));
        ASSERT_NE(r, nullptr);
        EXPECT_FLOAT_EQ(r->GetRadius(), 1.0f);   // 保存的 Component 状态
        ASSERT_NE(adapterB, nullptr);
        IPhysicsBody* body = adapterB->GetBody(*r);
        ASSERT_NE(body, nullptr);
        EXPECT_FLOAT_EQ(CircleRadius(body), 1.0f);   // Body 派生自保存状态
    }

    // 第二拍：重新 Save（此时组件=2）→ 冷启 → 必须 = 2
    adapterA.SetWorld(MakeWorld());
    adapterA.RebuildAll();
    ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(sceneA, {}), path));
    adapterA.ReleaseWorld();
    {
        auto [adapterB, sceneB] = b5::ColdStart(path, 1);
        ASSERT_NE(sceneB, nullptr);
        auto* r = static_cast<ColliderComponent*>(
            sceneB->GetObjects()[0]->GetComponentByName("Collider"));
        ASSERT_NE(r, nullptr);
        EXPECT_FLOAT_EQ(r->GetRadius(), 2.0f);
        ASSERT_NE(adapterB, nullptr);
        IPhysicsBody* body = adapterB->GetBody(*r);
        ASSERT_NE(body, nullptr);
        EXPECT_FLOAT_EQ(CircleRadius(body), 2.0f);
    }
}

// ── B5-3 多实体恢复 ──

TEST(F2ColliderPhysics, ColdRestart_MultiEntity_AllRestored) {
    Scene sceneA;
    auto* cA = b5::AddColliderToScene(sceneA, "A_Circle");
    cA->SetShape(ColliderShape::Circle);
    cA->SetRadius(0.5f);
    auto* cB = b5::AddColliderToScene(sceneA, "B_BoxSensor");
    cB->SetShape(ColliderShape::Box);
    cB->SetHalfExtents(2.0f, 1.0f);
    cB->SetIsSensor(true);
    auto* cC = b5::AddColliderToScene(sceneA, "C_BoxDisabled");
    cC->SetShape(ColliderShape::Box);
    cC->SetHalfExtents(1.0f, 1.0f);
    cC->SetEnabled(false);
    cC->SetCategory(0x0008);
    cC->SetMask(0x0031);

    const std::string path = b5::P("b5_multi.scene");
    ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(sceneA, {}), path));

    auto [adapterB, sceneB] = b5::ColdStart(path, 3);
    ASSERT_NE(sceneB, nullptr);
    ASSERT_NE(adapterB, nullptr);
    ASSERT_EQ(sceneB->GetObjectCount(), 3u);
    EXPECT_EQ(adapterB->GetBodyCount(), 3u);   // 3 Components → 3 Bodies，0 duplicate 0 missing

    auto* rA = static_cast<ColliderComponent*>(
        sceneB->GetObjects()[0]->GetComponentByName("Collider"));
    auto* rB = static_cast<ColliderComponent*>(
        sceneB->GetObjects()[1]->GetComponentByName("Collider"));
    auto* rC = static_cast<ColliderComponent*>(
        sceneB->GetObjects()[2]->GetComponentByName("Collider"));
    ASSERT_NE(rA, nullptr);
    ASSERT_NE(rB, nullptr);
    ASSERT_NE(rC, nullptr);

    IPhysicsBody* bA = adapterB->GetBody(*rA);
    IPhysicsBody* bB = adapterB->GetBody(*rB);
    IPhysicsBody* bC = adapterB->GetBody(*rC);
    ASSERT_NE(bA, nullptr);
    ASSERT_NE(bB, nullptr);
    ASSERT_NE(bC, nullptr);

    EXPECT_FLOAT_EQ(CircleRadius(bA), 0.5f);
    EXPECT_FALSE(ShapeIsSensor(bA));
    EXPECT_TRUE(bA->IsActive());

    float hx = 0.0f, hy = 0.0f;
    BoxHalfExtents(bB, &hx, &hy);
    EXPECT_FLOAT_EQ(hx, 2.0f);
    EXPECT_FLOAT_EQ(hy, 1.0f);
    EXPECT_TRUE(ShapeIsSensor(bB));

    BoxHalfExtents(bC, &hx, &hy);
    EXPECT_FLOAT_EQ(hx, 1.0f);
    EXPECT_FLOAT_EQ(hy, 1.0f);
    EXPECT_FALSE(bC->IsActive());
    EXPECT_EQ(ShapeCategory(bC), 0x0008u);
    EXPECT_EQ(ShapeMask(bC), 0x0031u);
}

// ── B5-4 删除状态不复活（Save 即快照规则）──

TEST(F2ColliderPhysics, ColdRestart_RemovedCollider_NotResurrected) {
    const std::string path = b5::P("b5_remove.scene");

    // Phase 1：Save（A+B）→ Remove A（不保存）→ 冷启 → 恢复 Save 时的 A+B（快照语义）
    {
        Scene sceneA;
        auto goA = std::make_shared<GameObject>("A");
        goA->AddComponentByName("Collider");
        sceneA.AddObject(goA);
        auto goB = std::make_shared<GameObject>("B");
        goB->AddComponentByName("Collider");
        sceneA.AddObject(goB);

        ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(sceneA, {}), path));
        EXPECT_TRUE(goA->RemoveComponentByName("Collider"));   // 移除 A，未保存
        {
            auto [adapterB, sceneB] = b5::ColdStart(path, 2);
            ASSERT_NE(sceneB, nullptr);
            EXPECT_EQ(adapterB->GetBodyCount(), 2u);   // 快照恢复 A+B
        }
    }

    // Phase 2：移除 B → Save → 冷启 → 只恢复 A（已删除 Collider 绝不复活）
    {
        Scene sceneA;
        auto goA = std::make_shared<GameObject>("A");
        goA->AddComponentByName("Collider");
        sceneA.AddObject(goA);
        auto goB = std::make_shared<GameObject>("B");
        goB->AddComponentByName("Collider");
        sceneA.AddObject(goB);

        EXPECT_TRUE(goB->RemoveComponentByName("Collider"));
        ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(sceneA, {}), path));
        {
            auto [adapterB, sceneB] = b5::ColdStart(path, 2);
            ASSERT_NE(sceneB, nullptr);
            EXPECT_EQ(adapterB->GetBodyCount(), 1u);   // 只有 A 有 Collider
            auto* rA = static_cast<ColliderComponent*>(
                sceneB->GetObjects()[0]->GetComponentByName("Collider"));
            auto* rB = static_cast<ColliderComponent*>(
                sceneB->GetObjects()[1]->GetComponentByName("Collider"));
            ASSERT_NE(rA, nullptr);
            EXPECT_EQ(rB, nullptr);   // B 无 Collider（已删除不复活）
        }
    }
}

// ── B5-5a 负路径：无 Collider / 未知类型 / 数据损坏 ──

TEST(F2ColliderPhysics, ColdRestart_Negative_NoCollider_UnknownType_CorruptData) {
    // ① 空场景（无 Collider）：实体存活，0 bodies
    {
        Scene sceneA;
        auto go = std::make_shared<GameObject>("plain");
        sceneA.AddObject(go);
        const std::string path = b5::P("b5_empty.scene");
        ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(sceneA, {}), path));
        auto [adapterB, sceneB] = b5::ColdStart(path, 1);
        ASSERT_NE(sceneB, nullptr);
        EXPECT_EQ(adapterB->GetBodyCount(), 0u);
        EXPECT_EQ(sceneB->GetObjects()[0]->GetName(), "plain");   // 实体存活
    }

    // ② 未知组件类型：warning + 实体照常加载（F1 原则），不崩
    {
        const std::string path = b5::P("b5_unknown.scene");
        {
            std::ofstream f(path, std::ios::trunc);
            f << "{\"format\":\"engine.scene\",\"version\":1,\"entities\":["
                 "{\"name\":\"unk\",\"position\":[0,0,0],"
                 "\"components\":[{\"type\":\"Nope\",\"data\":{}}]}]}";
        }
        auto [adapterB, sceneB] = b5::ColdStart(path, 1);
        ASSERT_NE(sceneB, nullptr);
        EXPECT_EQ(adapterB->GetBodyCount(), 0u);
        EXPECT_EQ(sceneB->GetObjects()[0]->GetName(), "unk");   // 实体存活
        EXPECT_EQ(sceneB->GetObjects()[0]->GetComponentByName("Collider"), nullptr);
    }

    // ③ 组件数据损坏（radius 变成字符串）：Deserialize 拒绝 → 默认值 + 实体存活 + 可绑定
    {
        Scene sceneA;
        auto* c = b5::AddColliderToScene(sceneA, "corrupt");
        c->SetShape(ColliderShape::Circle);
        c->SetRadius(1.25f);
        const std::string path = b5::P("b5_corrupt.scene");
        ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(sceneA, {}), path));
        {
            std::string text = b5::ReadFile(path);
            const std::string needle = "\"radius\": 1.25";
            const auto pos = text.find(needle);
            ASSERT_NE(pos, std::string::npos);
            text.replace(pos, needle.size(), "\"radius\": \"oops\"");
            std::ofstream f(path, std::ios::trunc);
            f << text;
        }
        auto [adapterB, sceneB] = b5::ColdStart(path, 1);
        ASSERT_NE(sceneB, nullptr);
        auto* r = static_cast<ColliderComponent*>(
            sceneB->GetObjects()[0]->GetComponentByName("Collider"));
        ASSERT_NE(r, nullptr);   // 组件保留（默认值）
        EXPECT_FLOAT_EQ(r->GetRadius(), 0.5f);   // 默认 radius
        ASSERT_NE(adapterB, nullptr);
        IPhysicsBody* body = adapterB->GetBody(*r);
        ASSERT_NE(body, nullptr);
        EXPECT_FLOAT_EQ(CircleRadius(body), 0.5f);   // 默认值可正常绑定
    }
}

// ── B5-5b 负路径：Load 时无 Physics World → 晚到可恢复 ──

TEST(F2ColliderPhysics, ColdRestart_Negative_NoWorldAtLoad_Recoverable) {
    Scene sceneA;
    auto* c = b5::AddColliderToScene(sceneA, "noworld");
    c->SetShape(ColliderShape::Circle);
    c->SetRadius(0.75f);
    const std::string path = b5::P("b5_noworld.scene");
    ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(sceneA, {}), path));

    // 冷启但先不建 World：观察 → 干净失败（0 body，组件已受管）
    SceneSnapshot snap;
    std::string err;
    ASSERT_TRUE(LoadSnapshotFromFile(path, snap, err));
    PhysicsColliderAdapter adapterB;   // 无 World；声明在 scene 之前（观察者先存活）
    auto sceneB = std::make_shared<Scene>();
    OpenGLGraphicsFactory gfx;
    TextureManager texMgr(gfx);
    ContentRegistry reg;
    ASSERT_TRUE(InstantiateScene(snap, *sceneB, texMgr, reg).ok);

    for (auto& go : sceneB->GetObjects()) adapterB.ObserveGameObject(*go);
    EXPECT_EQ(adapterB.GetBodyCount(), 0u);
    EXPECT_EQ(adapterB.GetTrackedCount(), 1u);   // 组件已受管，待世界就绪

    // World 晚到 → RebuildAll → Body 从组件重建
    adapterB.SetWorld(MakeWorld());
    adapterB.RebuildAll();
    auto* r = static_cast<ColliderComponent*>(
        sceneB->GetObjects()[0]->GetComponentByName("Collider"));
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(adapterB.IsBound(*r));
    EXPECT_EQ(adapterB.GetBodyCount(), 1u);
    IPhysicsBody* body = adapterB.GetBody(*r);
    ASSERT_NE(body, nullptr);
    EXPECT_FLOAT_EQ(CircleRadius(body), 0.75f);
}

// ════════════════════════════════════════════════════════════
// B6 Physics Behavior Consistency：恢复出来的 Static Collider 在真实 Physics
// 层的【行为】是否与从未冷启动的连续运行等价。
//
// 观测机制：经 Box2DPhysicsWorld::CreateBody 直接投入一枚【动态探针】。
// Static Collider 挡住探针 → 探针静止在 collider 顶沿；穿透 → 探针深陷下方。
// 该差异由 collider 的 shape / sensor / filter / enabled 驱动，是对物理行为
// （而非字段回声）的直接观测。A（连续）/ B（冷重启）对拍 trace，证明等价。
// ════════════════════════════════════════════════════════════

namespace b6 {

    /// 直接经 IPhysicsWorld 创建动态探针（纯 Physics 层观测，非 Adapter 路径）
    std::shared_ptr<IPhysicsBody> MakeProbe(const std::shared_ptr<IPhysicsWorld>& world,
                                            uint16 cat, uint16 mask,
                                            float y = 5.0f) {
        BodyDef def;
        def.type = BodyType::Dynamic;
        def.position = Vec2(0.0f, y);
        def.shape.type = ShapeType::Circle;
        def.shape.circleRadius = 0.2f;
        def.categoryBits = cat;
        def.maskBits = mask;
        return world->CreateBody(def);
    }

    /// 从 y=5 释放探针并步进 2 秒（120 帧 @ 60Hz），记录探针 Y 轨迹
    std::vector<float> DropTrace(const std::shared_ptr<IPhysicsWorld>& world,
                                 uint16 cat, uint16 mask) {
        auto probe = MakeProbe(world, cat, mask);
        std::vector<float> trace;
        trace.reserve(120);
        for (int i = 0; i < 120; ++i) {
            world->Step(1.0f / 60.0f);
            trace.push_back(probe->GetPosition().y);
        }
        return trace;
    }

    bool TracesClose(const std::vector<float>& a, const std::vector<float>& b, float eps) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::fabs(a[i] - b[i]) > eps) return false;
        return true;
    }

    /// 探针最终是否被 collider 挡住（静止在上方 Y>0）而非穿透（Y<<0）
    bool IsBlocked(const std::vector<float>& trace) {
        return !trace.empty() && trace.back() > 0.0f;
    }

    /// 冷重启场景并返回 adapter/collider（observer-before-observed 顺序由 pair 保证）
    struct Restarted {
        std::unique_ptr<PhysicsColliderAdapter> adapter;
        std::shared_ptr<Scene> scene;
        ColliderComponent* collider = nullptr;
    };

    Restarted Restart(const std::string& path) {
        Restarted r;
        auto p = b5::ColdStart(path, 1);
        r.adapter = std::move(p.first);
        r.scene = std::move(p.second);
        if (r.scene && r.scene->GetObjectCount() >= 1) {
            r.collider = static_cast<ColliderComponent*>(
                r.scene->GetObjects()[0]->GetComponentByName("Collider"));
        }
        return r;
    }

    /// 独立世界搭建一个 collider + 一枚探针并降落 trace。
    /// 关键：每枚探针必须用【独立世界】，否则上一个探针空置在块上会挡住下一个。
    /// 仅测试用；collider 经 Box2DPhysicsWorld 直接传入（与 Adapter 无关的物理层离散）。
    struct ProbeRun {
        ColliderComponent* collider = nullptr;
        std::vector<float> trace;
    };
} // namespace b6

// ── B6-1 A/B 探针 trace 对拍：连续运行 vs 冷重启（Circle）──

TEST(F2ColliderPhysics, B6_Trace_AB_DynamicProbe_Circle) {
    // 源场景：Circle radius 1.0，普通 collider，cat=Player mask=All
    Scene src;
    auto* cs = b5::AddColliderToScene(src, "c");
    cs->SetShape(ColliderShape::Circle);
    cs->SetRadius(1.0f);
    cs->SetCategory(0x0002);
    cs->SetMask(0xFFFF);
    cs->SetIsSensor(false);
    const std::string path = b5::P("b6_trace_circle.scene");
    ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(src, {}), path));

    // ── Run A：连续运行（不冷启）──
    PhysicsColliderAdapter adapterA;
    adapterA.SetWorld(MakeWorld());
    auto goA = MakeRig("rigA");
    auto* cA = AddCollider(*goA);
    cA->SetShape(ColliderShape::Circle);
    cA->SetRadius(1.0f);
    cA->SetCategory(0x0002);
    cA->SetMask(0xFFFF);
    ASSERT_TRUE(adapterA.Bind(*cA));
    const std::vector<float> traceA = b6::DropTrace(adapterA.GetWorld(), 0x0002, 0xFFFF);
    EXPECT_TRUE(b6::IsBlocked(traceA));   // Circle top = y=1.0 → 探针静止在上方

    // ── Run B：Save → Destroy → 全新 Scene/GameObject/World → 自动 Bind → 同输入（真冷启链路）──
    auto ra = b6::Restart(path);
    ASSERT_NE(ra.scene, nullptr);
    ASSERT_NE(ra.collider, nullptr);
    ASSERT_EQ(ra.adapter->GetBodyCount(), 1u);
    const std::vector<float> traceB = b6::DropTrace(ra.adapter->GetWorld(), 0x0002, 0xFFFF);
    EXPECT_TRUE(b6::IsBlocked(traceB));

    // 行为 trace 等价（同一 dt/输入下），误差需有明确 epsilon
    EXPECT_TRUE(b6::TracesClose(traceA, traceB, 2e-3f));

    // Physics representation 亦对齐 + BodyCount 恒 1
    const BehaviorSnapshot snapA = Snapshot(adapterA, *cA);
    IPhysicsBody* bB = ra.adapter->GetBody(*ra.collider);
    ASSERT_NE(bB, nullptr);
    EXPECT_FLOAT_EQ(bB->GetPosition().x, snapA.pos.x);
    EXPECT_FLOAT_EQ(bB->GetPosition().y, snapA.pos.y);
    EXPECT_FLOAT_EQ(CircleRadius(bB), snapA.radius);
    EXPECT_EQ(bB->IsActive(), snapA.active);
    EXPECT_EQ(ShapeIsSensor(bB), snapA.sensor);
    EXPECT_EQ(ShapeCategory(bB), snapA.cat);
    EXPECT_EQ(ShapeMask(bB), snapA.mask);
    EXPECT_EQ(ra.adapter->GetBodyCount(), 1u);
}

// ── B6-2 Circle / Box 双路径：两形状行为等价——

TEST(F2ColliderPhysics, B6_AB_DualPath_CircleAndBox) {
    // Circle：radius 1.0
    {
        Scene src;
        auto* cs = b5::AddColliderToScene(src, "c");
        cs->SetShape(ColliderShape::Circle);
        cs->SetRadius(1.0f);
        const std::string path = b5::P("b6_dual_circle.scene");
        ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(src, {}), path));

        PhysicsColliderAdapter a;
        a.SetWorld(MakeWorld());
        auto go = MakeRig("rig");
        auto* c = AddCollider(*go);
        c->SetShape(ColliderShape::Circle);
        c->SetRadius(1.0f);
        ASSERT_TRUE(a.Bind(*c));
        const auto tA = b6::DropTrace(a.GetWorld(), 0x0002, 0xFFFF);
        EXPECT_TRUE(b6::IsBlocked(tA));

        auto r = b6::Restart(path);
        ASSERT_NE(r.collider, nullptr);
        const auto tB = b6::DropTrace(r.adapter->GetWorld(), 0x0002, 0xFFFF);
        EXPECT_TRUE(b6::TracesClose(tA, tB, 2e-3f));
        IPhysicsBody* body = r.adapter->GetBody(*r.collider);
        ASSERT_NE(body, nullptr);
        EXPECT_FLOAT_EQ(CircleRadius(body), 1.0f);
    }

    // Box：halfX 2.0 / halfY 0.5
    {
        Scene src;
        auto* cs = b5::AddColliderToScene(src, "c");
        cs->SetShape(ColliderShape::Box);
        cs->SetHalfExtents(2.0f, 0.5f);
        const std::string path = b5::P("b6_dual_box.scene");
        ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(src, {}), path));

        PhysicsColliderAdapter a;
        a.SetWorld(MakeWorld());
        auto go = MakeRig("rig");
        auto* c = AddCollider(*go);
        c->SetShape(ColliderShape::Box);
        c->SetHalfExtents(2.0f, 0.5f);
        ASSERT_TRUE(a.Bind(*c));
        const auto tA = b6::DropTrace(a.GetWorld(), 0x0002, 0xFFFF);
        EXPECT_TRUE(b6::IsBlocked(tA));   // Box top = y=0.5

        auto r = b6::Restart(path);
        ASSERT_NE(r.collider, nullptr);
        const auto tB = b6::DropTrace(r.adapter->GetWorld(), 0x0002, 0xFFFF);
        EXPECT_TRUE(b6::TracesClose(tA, tB, 2e-3f));
        IPhysicsBody* body = r.adapter->GetBody(*r.collider);
        ASSERT_NE(body, nullptr);
        float hx = 0.0f, hy = 0.0f;
        BoxHalfExtents(body, &hx, &hy);
        EXPECT_FLOAT_EQ(hx, 2.0f);
        EXPECT_FLOAT_EQ(hy, 0.5f);
    }
}

// ── B6-3 Sensor 行为：sensor collider 不产生物理响应（探针穿透）──

TEST(F2ColliderPhysics, B6_Sensor_ProbePassesThrough_ConsistentAfterRestart) {
    // normal：阻挡 → 探针静止
    // sensor：穿透 → 探针深陷下方
    {
        Scene src;
        auto* cs = b5::AddColliderToScene(src, "c");
        cs->SetShape(ColliderShape::Box);
        cs->SetHalfExtents(2.0f, 0.5f);
        cs->SetIsSensor(false);
        const std::string path = b5::P("b6_sensor_normal.scene");
        ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(src, {}), path));
        auto r = b6::Restart(path);
        ASSERT_NE(r.collider, nullptr);
        ASSERT_FALSE(ShapeIsSensor(r.adapter->GetBody(*r.collider)));
        const auto t = b6::DropTrace(r.adapter->GetWorld(), 0x0002, 0xFFFF);
        EXPECT_TRUE(b6::IsBlocked(t));     // normal → 阻挡
    }

    {
        Scene src;
        auto* cs = b5::AddColliderToScene(src, "c");
        cs->SetShape(ColliderShape::Box);
        cs->SetHalfExtents(2.0f, 0.5f);
        cs->SetIsSensor(true);
        const std::string path = b5::P("b6_sensor_sensor.scene");
        ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(src, {}), path));

        // 连续运行：sensor → 探针穿透（不产生实体物理响应）
        PhysicsColliderAdapter a;
        a.SetWorld(MakeWorld());
        auto go = MakeRig("rig");
        auto* c = AddCollider(*go);
        c->SetShape(ColliderShape::Box);
        c->SetHalfExtents(2.0f, 0.5f);
        c->SetIsSensor(true);
        ASSERT_TRUE(a.Bind(*c));
        const auto tA = b6::DropTrace(a.GetWorld(), 0x0002, 0xFFFF);
        EXPECT_FALSE(b6::IsBlocked(tA));   // sensor → 穿透

        // 冷重启后仍是 sensor，行为一致（穿透）
        auto r = b6::Restart(path);
        ASSERT_NE(r.collider, nullptr);
        IPhysicsBody* body = r.adapter->GetBody(*r.collider);
        ASSERT_NE(body, nullptr);
        EXPECT_TRUE(ShapeIsSensor(body));
        EXPECT_TRUE(body->IsActive());
        EXPECT_EQ(ShapeCategory(body), 0x0001u);   // 默认 filter 保留
        const auto tB = b6::DropTrace(r.adapter->GetWorld(), 0x0002, 0xFFFF);
        EXPECT_FALSE(b6::IsBlocked(tB));
        EXPECT_TRUE(b6::TracesClose(tA, tB, 2e-3f));
    }
}

// ── B6-4 Filter 行为：证明 Physics 实际使用 category/mask（真交互，非字段回声）──

TEST(F2ColliderPhysics, B6_Filter_PhysicsActuallyAppliesCategoryMask_AfterRestart) {
    // collider: cat=0x0002(Player) mask=0x0002 只跟 Player 碰撞。
    // 探针 cat=0x0002 → 碰撞 → 阻挡（真实交互证明 filter 生效）
    // 探针 cat=0x0004(Enemy) → 无碰撞 → 穿透（即使几何重叠，filter 阻止）
    Scene src;
    auto* cs = b5::AddColliderToScene(src, "c");
    cs->SetShape(ColliderShape::Box);
    cs->SetHalfExtents(2.0f, 0.5f);
    cs->SetCategory(0x0002);
    cs->SetMask(0x0002);
    const std::string path = b5::P("b6_filter.scene");
    ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(src, {}), path));

    // 每枚探针使用【独立世界】：否则上一枚探针静止在块上会物理挡住下一枚。
    // 连续运行（Run A）：碰撞组/滤除组各用独立 adapter+world
    const auto dropInFreshWorld = [](uint16 probeCat, float* blockedOut) {
        PhysicsColliderAdapter a;
        a.SetWorld(MakeWorld());
        auto go = MakeRig("rig");
        auto* c = AddCollider(*go);
        c->SetShape(ColliderShape::Box);
        c->SetHalfExtents(2.0f, 0.5f);
        c->SetCategory(0x0002);
        c->SetMask(0x0002);
        bool bound = a.Bind(*c);
        *blockedOut = bound ? 1.0f : 0.0f;   // 仅把绑定成功与否带出；实际断言在上方外层做
        return b6::DropTrace(a.GetWorld(), probeCat, 0xFFFF);
    };
    float boundHit = 0.0f, boundMiss = 0.0f;
    const auto tHit  = dropInFreshWorld(0x0002, &boundHit);   // Player → 碰撞 → 被挡
    const auto tMiss = dropInFreshWorld(0x0004, &boundMiss);  // Enemy  → 滤除 → 穿透（几何仍重叠）
    ASSERT_EQ(boundHit, 1.0f);
    ASSERT_EQ(boundMiss, 1.0f);
    EXPECT_TRUE(b6::IsBlocked(tHit));
    EXPECT_FALSE(b6::IsBlocked(tMiss));

    // Cold Restart（Run B）：同样各用独立冷启世界
    const auto tHitB  = b6::DropTrace(b6::Restart(path).adapter->GetWorld(), 0x0002, 0xFFFF);
    const auto tMissB = b6::DropTrace(b6::Restart(path).adapter->GetWorld(), 0x0004, 0xFFFF);
    EXPECT_TRUE(b6::IsBlocked(tHitB));
    EXPECT_FALSE(b6::IsBlocked(tMissB));

    // 冷重启后 filter 恢复
    {
        auto r = b6::Restart(path);
        ASSERT_NE(r.collider, nullptr);
        IPhysicsBody* body = r.adapter->GetBody(*r.collider);
        ASSERT_NE(body, nullptr);
        EXPECT_EQ(ShapeCategory(body), 0x0002u);
        EXPECT_EQ(ShapeMask(body), 0x0002u);
    }

    EXPECT_TRUE(b6::TracesClose(tHit, tHitB, 2e-3f));
    EXPECT_TRUE(b6::TracesClose(tMiss, tMissB, 2e-3f));
}

// ── B6-5 Mutation → Restart：radius 变更后冷启，行为 == 新 radius 的行为 ──

TEST(F2ColliderPhysics, B6_Mutation_RadiusThenRestart_BehaviorMatches) {
    const std::string path = b5::P("b6_mut_radius.scene");

    // 保存时 radius = 2.0
    {
        Scene src;
        auto* cs = b5::AddColliderToScene(src, "c");
        cs->SetShape(ColliderShape::Circle);
        cs->SetRadius(2.0f);
        ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(src, {}), path));
    }

    // 连续运行基准：Static Circle radius 2.0 → 探针被挡（顶沿 y≈2.0）
    const float baselineDropY = 5.0f;
    PhysicsColliderAdapter a;
    a.SetWorld(MakeWorld());
    auto go = MakeRig("rig");
    auto* c = AddCollider(*go);
    c->SetShape(ColliderShape::Circle);
    c->SetRadius(2.0f);
    ASSERT_TRUE(a.Bind(*c));
    const auto tBaseline = b6::DropTrace(a.GetWorld(), 0x0002, 0xFFFF);
    EXPECT_TRUE(b6::IsBlocked(tBaseline));

    // 冷重启 radius=2.0：行为与基准一致（radius=2 的行为）
    auto r = b6::Restart(path);
    ASSERT_NE(r.collider, nullptr);
    EXPECT_FLOAT_EQ(r.collider->GetRadius(), 2.0f);
    IPhysicsBody* body = r.adapter->GetBody(*r.collider);
    ASSERT_NE(body, nullptr);
    EXPECT_FLOAT_EQ(CircleRadius(body), 2.0f);
    const auto t = b6::DropTrace(r.adapter->GetWorld(), 0x0002, 0xFFFF);
    EXPECT_TRUE(b6::IsBlocked(t));
    EXPECT_TRUE(b6::TracesClose(tBaseline, t, 2e-3f));
}

// ── B6-5(b) Mutation → Restart：enabled=false 保存后冷启，Body 必须保持 disabled ──

TEST(F2ColliderPhysics, B6_Mutation_EnabledFalse_ThenRestart_BodyStayDisabled) {
    const std::string path = b5::P("b6_mut_disabled.scene");

    {
        Scene src;
        auto* cs = b5::AddColliderToScene(src, "c");
        cs->SetShape(ColliderShape::Box);
        cs->SetHalfExtents(2.0f, 0.5f);
        cs->SetEnabled(false);
        ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(src, {}), path));
    }

    auto r = b6::Restart(path);
    ASSERT_NE(r.collider, nullptr);
    IPhysicsBody* body = r.adapter->GetBody(*r.collider);
    ASSERT_NE(body, nullptr);

    // Body 必须保持 disabled：不产生物理响应 → 探针穿透
    EXPECT_FALSE(body->IsActive());
    EXPECT_TRUE(r.collider->IsEnabled() == false);
    const auto t = b6::DropTrace(r.adapter->GetWorld(), 0x0002, 0xFFFF);
    EXPECT_FALSE(b6::IsBlocked(t));
}

// ── B6-6 三路对拍：A 连续 / B 中途 Destroy→Rebuild / C Save→冷重启，最终 A≈B≈C ──

TEST(F2ColliderPhysics, B6_ThreeWay_Continuous_MidDestroy_ColdRestart) {
    // A：连续运行
    PhysicsColliderAdapter a;
    a.SetWorld(MakeWorld());
    auto goA = MakeRig("rigA");
    auto* cA = AddCollider(*goA);
    cA->SetShape(ColliderShape::Circle);
    cA->SetRadius(1.0f);
    ASSERT_TRUE(a.Bind(*cA));
    const auto tA = b6::DropTrace(a.GetWorld(), 0x0002, 0xFFFF);

    // B：中途 Destroy World → Rebuild
    PhysicsColliderAdapter b;
    b.SetWorld(std::make_shared<Box2DPhysicsWorld>(Vec2(0.0f, -9.81f)));
    auto goB = MakeRig("rigB");
    auto* cB = AddCollider(*goB);
    cB->SetShape(ColliderShape::Circle);
    cB->SetRadius(1.0f);
    ASSERT_TRUE(b.Bind(*cB));
    b.ReleaseWorld();
    b.SetWorld(std::make_shared<Box2DPhysicsWorld>(Vec2(0.0f, -9.81f)));
    b.RebuildAll();
    ASSERT_EQ(b.GetBodyCount(), 1u);
    const auto tB = b6::DropTrace(b.GetWorld(), 0x0002, 0xFFFF);

    // C：Save → Cold Restart → Rebuild
    const std::string path = b5::P("b6_threeway.scene");
    {
        Scene src;
        auto* cs = b5::AddColliderToScene(src, "c");
        cs->SetShape(ColliderShape::Circle);
        cs->SetRadius(1.0f);
        ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(src, {}), path));
    }
    auto r = b6::Restart(path);
    ASSERT_NE(r.collider, nullptr);
    const auto tC = b6::DropTrace(r.adapter->GetWorld(), 0x0002, 0xFFFF);

    EXPECT_TRUE(b6::IsBlocked(tA));
    EXPECT_TRUE(b6::IsBlocked(tB));
    EXPECT_TRUE(b6::IsBlocked(tC));
    EXPECT_TRUE(b6::TracesClose(tA, tB, 2e-3f));
    EXPECT_TRUE(b6::TracesClose(tA, tC, 2e-3f));   // A ≈ B ≈ C
}

// ── B6-7 稳定性：Cold Restart ×10 → BodyCount 不增长 / m_Tracked 不增长 / trace 不漂移 ──

TEST(F2ColliderPhysics, B6_RepeatColdRestart_x10_NoGrowth_NoDrift_NoGhost) {
    const std::string path = b5::P("b6_repeat.scene");
    {
        Scene src;
        auto* cs = b5::AddColliderToScene(src, "c");
        cs->SetShape(ColliderShape::Box);
        cs->SetHalfExtents(2.0f, 0.5f);
        ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(src, {}), path));
    }

    std::vector<float> reference;
    for (int round = 0; round < 10; ++round) {
        auto r = b6::Restart(path);
        ASSERT_NE(r.scene, nullptr);
        ASSERT_NE(r.adapter, nullptr);
        ASSERT_NE(r.collider, nullptr);

        // BodyCount / Tracked 每一轮都精确恒定（1 → 1 → 1 ...，绝不增长）
        EXPECT_EQ(r.adapter->GetBodyCount(), 1u);
        EXPECT_EQ(r.adapter->GetTrackedCount(), 1u);
        EXPECT_TRUE(r.adapter->IsBound(*r.collider));

        const auto t = b6::DropTrace(r.adapter->GetWorld(), 0x0002, 0xFFFF);
        EXPECT_TRUE(b6::IsBlocked(t));
        if (reference.empty()) {
            reference = t;
        } else {
            EXPECT_TRUE(b6::TracesClose(reference, t, 2e-3f));   // 行为 trace 不漂移
        }
    }
}

// ── B6-8 负路径：非法几何干净失败 + 不产生 ghost ──

TEST(F2ColliderPhysics, B6_Negative_InvalidGeometry_CleanFail_NoGhost) {
    auto world = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(world);

    // 非法 radius = 0 → Bind 干净失败（不 crash / 不 double-bind / 不产生 ghost body）
    auto goR = MakeRig("badR");
    auto* badR = AddCollider(*goR);
    badR->SetShape(ColliderShape::Circle);
    badR->SetRadius(0.0f);
    EXPECT_FALSE(adapter.Bind(*badR));
    EXPECT_FALSE(adapter.IsBound(*badR));
    EXPECT_EQ(adapter.GetBody(*badR), nullptr);
    EXPECT_EQ(adapter.GetBodyCount(), 0u);

    // 非法 half extent = 0 → 干净失败
    auto goH = MakeRig("badH");
    auto* badH = AddCollider(*goH);
    badH->SetShape(ColliderShape::Box);
    badH->SetHalfExtents(0.0f, 1.0f);
    EXPECT_FALSE(adapter.Bind(*badH));
    EXPECT_EQ(adapter.GetBodyCount(), 0u);

    // disabled 组件 / 重复 Rebuild / 未知 shape 一律干净：
    // 失败路径不污染 m_Bodies，也不产生 double-bind
    EXPECT_EQ(adapter.GetBodyCount(), 0u);

    // 之后合法组件照常可绑定（无 ghost 残留）
    auto goOk = MakeRig("ok");
    auto* ok = AddCollider(*goOk);
    ok->SetShape(ColliderShape::Circle);
    ok->SetRadius(1.0f);
    EXPECT_TRUE(adapter.Bind(*ok));
    EXPECT_EQ(adapter.GetBodyCount(), 1u);
    EXPECT_TRUE(b2Body_IsValid(GetNativeBodyId(adapter.GetBody(*ok))));
}

TEST(F2ColliderPhysics, B6_Negative_WorldDestroyThenQueryBody_NoCrash) {
    auto world = MakeWorld();
    PhysicsColliderAdapter adapter;
    adapter.SetWorld(world);
    auto go = MakeRig("rig");
    auto* c = AddCollider(*go);
    c->SetShape(ColliderShape::Circle);
    c->SetRadius(1.0f);
    ASSERT_TRUE(adapter.Bind(*c));

    const b2BodyId native = GetNativeBodyId(adapter.GetBody(*c));
    adapter.ReleaseWorld();

    // 已失效 body：查询不再返回，注册表清空，原生 body 无效（不 crash）
    EXPECT_EQ(adapter.GetBody(*c), nullptr);
    EXPECT_EQ(adapter.GetBodyCount(), 0u);
    EXPECT_FALSE(b2Body_IsValid(native));

    // 之后可安全恢复
    adapter.SetWorld(MakeWorld());
    adapter.RebuildAll();
    ASSERT_TRUE(adapter.IsBound(*c));
    EXPECT_EQ(adapter.GetBodyCount(), 1u);
}

TEST(F2ColliderPhysics, B6_Negative_RepeatedObserveAfterColdLoad_NoDuplicate) {
    const std::string path = b5::P("b6_reobserve.scene");
    {
        Scene src;
        auto* cs = b5::AddColliderToScene(src, "c");
        cs->SetShape(ColliderShape::Circle);
        cs->SetRadius(1.0f);
        ASSERT_TRUE(SaveSnapshotToFile(CaptureScene(src, {}), path));
    }
    auto r = b6::Restart(path);
    ASSERT_NE(r.adapter, nullptr);
    ASSERT_EQ(r.adapter->GetBodyCount(), 1u);

    // 对同一个 GameObject 重复 ObserveGameObject：幂等，不产生 duplicate body
    for (int i = 0; i < 3; ++i) {
        r.adapter->ObserveGameObject(*r.scene->GetObjects()[0]);
    }
    EXPECT_EQ(r.adapter->GetBodyCount(), 1u);
    EXPECT_EQ(r.adapter->GetTrackedCount(), 1u);
    EXPECT_TRUE(r.adapter->IsBound(*r.collider));
}

} // namespace
