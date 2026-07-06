#include "ECSTest.h"
#include <Engine/Platform/PlatformUtils.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/IWindow.h>
#include <Engine/Core/GameObject/SpriteComponent.h>
#include <Engine/Core/ECS/EntityCommandBuffer.h>
#include <Engine/Core/ECS/ComponentRegistry.h>
#include <cstring>
#include <cmath>
#include <iostream>

namespace Engine {

// ═══════════════════════════════════════════════════════
// 构造函数 — 初始化所有子系统
// ═══════════════════════════════════════════════════════
ECSTest::ECSTest(IGraphicsFactory& factory)
    : m_Factory(factory)
    , m_TextureManager(factory)
    , m_SceneRenderer(factory, m_TextureManager)
{
    m_Window = m_Factory.CreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "ECS Test");
    m_InputManager.Init(m_Window.get());

    float32 aspect = static_cast<float32>(WINDOW_WIDTH) / WINDOW_HEIGHT;
    float32 viewHeight = 10.0f;
    float32 viewWidth = viewHeight * aspect;
    m_Camera = OrthographicCamera(-viewWidth / 2, viewWidth / 2,
                                  -viewHeight / 2, viewHeight / 2);

    auto* ctx = m_Window->GetContext();
    m_SceneRenderer.SetRenderContext(*ctx);
    m_SceneRenderer.SetCamera(&m_Camera);

    auto batchShader = m_Factory.CreateShader(
        "assets/shaders/sprite_batch.vert",
        "assets/shaders/sprite_batch.frag"
    );
    m_SceneRenderer.SetShader(batchShader);

    m_Texture = m_TextureManager.Load("assets/textures/test.png");

    // 注册退出动作
    auto& actionExit = m_InputManager.CreateAction("Exit");
    actionExit.AddBinding(KeyBinding::FromKey(KeyCode::Escape));
    actionExit.OnPressed([this]() { m_ShouldClose = true; });

    // ── 创建渲染场景 ──
    {
        // ECS 实体对应的 GameObject —— 演示桥接模式
        auto renderSprite = std::make_shared<GameObject>("ECSSprite");
        renderSprite->GetSprite().SetTexture(m_Texture);
        renderSprite->GetSprite().SetColor(0.2f, 0.8f, 1.0f, 1.0f);
        renderSprite->GetTransform().SetPosition(0.0f, 0.0f, 0.0f);
        m_Scene.AddObject(std::move(renderSprite));
    }

    m_Scene.OnCreate();

    // ── 注册自定义组件类型到 ECS ──
    RegisterComponentType<HealthComponent>();
    RegisterComponentType<DamageComponent>();
    RegisterComponentType<RigidBody3DComponent>();
    RegisterComponentType<BoxCollider3DComponent>();
    RegisterComponentType<SphereCollider3DComponent>();
    RegisterComponentType<CapsuleCollider3DComponent>();
    RegisterComponentType<PhysicsRuntimeComponent>();

    // ── 先运行单元测试 ──
    RunCoreTests();
    PrintHelp();
}

ECSTest::~ECSTest() {
    m_InputManager.Shutdown();
}

// ═══════════════════════════════════════════════════════
// 测试 1: 实体创建与销毁
// ═══════════════════════════════════════════════════════
bool ECSTest::TestEntityCreation() {
    // 大规模实体创建测试
    constexpr int kLargeCount = 10000;
    std::vector<EntityHandle> entities;
    entities.reserve(kLargeCount);

    for (int i = 0; i < kLargeCount; ++i) {
        EntityHandle e = m_EntityMgr.CreateEntity();
        if (e.IsNull()) return false;
        entities.push_back(e);
    }

    // 验证总数
    if (m_EntityMgr.GetEntityCount() != kLargeCount) {
        m_Log.Error("Entity count mismatch: got {}, expected {}", 
                    m_EntityMgr.GetEntityCount(), kLargeCount);
        return false;
    }

    // 验证全部存活
    for (int i = 0; i < kLargeCount; ++i) {
        if (!m_EntityMgr.IsAlive(entities[i])) return false;
    }

    // 销毁一半
    for (int i = 0; i < kLargeCount; i += 2) {
        m_EntityMgr.DestroyEntity(entities[i]);
    }

    if (m_EntityMgr.GetEntityCount() != kLargeCount / 2) return false;

    // Generation 验证：重新使用 ID
    EntityHandle e4 = m_EntityMgr.CreateEntity();
    // 至少有一个 ID 被复用
    bool reused = false;
    for (int i = 0; i < kLargeCount; i += 2) {
        if (e4.Index() == entities[i].Index()) {
            if (e4.Generation() == entities[i].Generation()) return false;
            reused = true;
            break;
        }
    }
    if (!reused) {
        m_Log.Warn("Entity ID reuse not verified (may be expected with large pool)");
    }

    // 再验证计数
    if (m_EntityMgr.GetEntityCount() != kLargeCount / 2 + 1) return false;

    m_Log.Info("EntityCreation: {} entities created/destroyed/recycled OK", kLargeCount);
    return true;
}

// ═══════════════════════════════════════════════════════
// 测试 2: 组件 CRUD（添加/获取/移除）
// ═══════════════════════════════════════════════════════
bool ECSTest::TestComponentCRUD() {
    EntityHandle e = m_EntityMgr.CreateEntity();
    if (e.IsNull()) return false;

    // 添加组件
    HealthComponent& health = m_EntityMgr.AddComponent<HealthComponent>(e, 100, 100);
    if (health.hp != 100) return false;

    // 获取组件
    HealthComponent* hp = m_EntityMgr.GetComponent<HealthComponent>(e);
    if (hp == nullptr) return false;

    // 修改组件
    hp->TakeDamage(30);
    if (hp->hp != 70) return false;

    // HasComponent
    if (!m_EntityMgr.HasComponent<HealthComponent>(e)) return false;

    // 自动迁移：添加第二个组件
    m_EntityMgr.AddComponent<DamageComponent>(e, 10, 0.0f);
    DamageComponent* dmg = m_EntityMgr.GetComponent<DamageComponent>(e);
    if (dmg == nullptr) return false;
    if (dmg->damagePerSecond != 10) return false;

    // 移除组件（迁移回单组件 Archetype）
    m_EntityMgr.RemoveComponent<DamageComponent>(e);
    if (m_EntityMgr.HasComponent<DamageComponent>(e)) return false;
    if (!m_EntityMgr.HasComponent<HealthComponent>(e)) return false;

    // 移除最后一个组件后实体应仍存在（无组件状态）
    m_EntityMgr.RemoveComponent<HealthComponent>(e);
    if (m_EntityMgr.HasComponent<HealthComponent>(e)) return false;
    if (!m_EntityMgr.IsAlive(e)) return false;

    return true;
}

// ═══════════════════════════════════════════════════════
// 测试 3: Query 遍历
// ═══════════════════════════════════════════════════════
bool ECSTest::TestEntityQuery() {
    // 创建具有特定组件的实体
    EntityHandle entities[5];
    for (int i = 0; i < 5; ++i) {
        EntityHandle e = m_EntityMgr.CreateEntity();
        entities[i] = e;
        m_EntityMgr.AddComponent<HealthComponent>(e, 100 + i * 10, 100);
        if (i < 3) {
            m_EntityMgr.AddComponent<DamageComponent>(e, 5 + i, 0.0f);
        }
    }

    // 查询拥有 HealthComponent 的所有实体
    auto query = m_EntityMgr.Query()
        .With<HealthComponent>()
        .Build();

    if (!query.IsValid()) return false;

    // 统计结果
    int foundHealth = 0;
    for (auto& range : query) {
        auto span = range.chunk->GetComponentSpan<HealthComponent>();
        foundHealth += static_cast<int>(span.size());
    }
    if (foundHealth != 5) return false;

    // 查询拥有 Health + Damage 的实体
    auto queryHD = m_EntityMgr.Query()
        .With<HealthComponent>()
        .With<DamageComponent>()
        .Build();

    int foundHD = 0;
    for (auto& range : queryHD) {
        auto span = range.chunk->GetComponentSpan<HealthComponent>();
        foundHD += static_cast<int>(span.size());
    }
    if (foundHD != 3) return false;

    // 带排除的查询：有 Health 但排除 Damage
    auto queryExclude = m_EntityMgr.Query()
        .With<HealthComponent>()
        .Without<DamageComponent>()
        .Build();

    int foundExclude = 0;
    for (auto& range : queryExclude) {
        auto span = range.chunk->GetComponentSpan<HealthComponent>();
        foundExclude += static_cast<int>(span.size());
    }
    if (foundExclude != 2) return false;

    return true;
}

// ═══════════════════════════════════════════════════════
// 测试 4: ECSBridge 映射
// ═══════════════════════════════════════════════════════
bool ECSTest::TestECSBridge() {
    // 创建实体
    EntityHandle e = m_EntityMgr.CreateEntity();
    m_EntityMgr.AddComponent<HealthComponent>(e, 200, 200);

    // 创建 GameObject
    auto go = std::make_shared<GameObject>("ECS_Mapped");

    // 桥接：建立双向映射
    ECSBridge::Link(go.get(), e);

    // 验证双向查询
    EntityHandle retrieved = ECSBridge::GetEntityHandle(go.get());
    if (retrieved != e) return false;

    GameObject* retrievedGO = ECSBridge::GetGameObject(e);
    if (retrievedGO != go.get()) return false;

    if (!ECSBridge::HasGameObject(e)) return false;

    // 解除映射
    ECSBridge::Unlink(go.get());
    if (ECSBridge::HasGameObject(e)) return false;

    return true;
}

// ═══════════════════════════════════════════════════════
// 测试 5: 物理组件存储
// ═══════════════════════════════════════════════════════
bool ECSTest::TestPhysicsComponents() {
    EntityHandle e = m_EntityMgr.CreateEntity();

    // 添加物理配置组件
    auto& body = m_EntityMgr.AddComponent<RigidBody3DComponent>(e);
    body.motionType = RigidBody3DComponent::MotionType::Dynamic;
    body.mass = 10.0f;
    body.friction = 0.3f;

    // 添加碰撞体
    auto& box = m_EntityMgr.AddComponent<BoxCollider3DComponent>(e);
    box.halfExtents = Vec3(0.5f, 0.5f, 0.5f);
    box.density = 2.0f;

    // 添加运行时组件（模拟物理系统写入）
    auto& runtime = m_EntityMgr.AddComponent<PhysicsRuntimeComponent>(e);
    runtime.runtimeBodyID = 12345;
    runtime.isActive = true;

    // 验证数据完整性
    RigidBody3DComponent* bodyCheck = m_EntityMgr.GetComponent<RigidBody3DComponent>(e);
    if (bodyCheck == nullptr) return false;
    if (bodyCheck->mass != 10.0f || bodyCheck->friction != 0.3f) return false;
    if (bodyCheck->motionType != RigidBody3DComponent::MotionType::Dynamic) return false;

    BoxCollider3DComponent* boxCheck = m_EntityMgr.GetComponent<BoxCollider3DComponent>(e);
    if (boxCheck == nullptr) return false;
    if (boxCheck->halfExtents.x != 0.5f || boxCheck->halfExtents.y != 0.5f) return false;

    PhysicsRuntimeComponent* rtCheck = m_EntityMgr.GetComponent<PhysicsRuntimeComponent>(e);
    if (rtCheck == nullptr) return false;
    if (rtCheck->runtimeBodyID != 12345 || !rtCheck->isActive) return false;

    // 验证查询组件集合
    auto query = m_EntityMgr.Query()
        .With<RigidBody3DComponent>()
        .With<BoxCollider3DComponent>()
        .With<PhysicsRuntimeComponent>()
        .Build();

    if (!query.IsValid()) return false;

    return true;
}

// ═══════════════════════════════════════════════════════
// 测试报告
// ═══════════════════════════════════════════════════════
void ECSTest::ReportTestResult(const char* name, bool passed) {
    m_TotalTests++;
    if (passed) m_PassedTests++;
    std::cout << "  [" << (passed ? "PASS" : "FAIL") << "] " << name << std::endl;
}

void ECSTest::RunCoreTests() {
    std::cout << "\n=== ECS Core Unit Tests ===" << std::endl;

    ReportTestResult("EntityCreation",       TestEntityCreation());
    ReportTestResult("ComponentCRUD",        TestComponentCRUD());
    ReportTestResult("EntityQuery",          TestEntityQuery());
    ReportTestResult("ECSBridge",            TestECSBridge());
    ReportTestResult("PhysicsComponents",    TestPhysicsComponents());

    std::cout << "  -> " << m_PassedTests << "/" << m_TotalTests << " passed\n" << std::endl;
}

// ═══════════════════════════════════════════════════════
// 渲染循环测试
// ═══════════════════════════════════════════════════════
void ECSTest::RunRenderingTest(float32 dt) {
    static float32 s_Time = 0.0f;
    s_Time += dt;

    // 通过 ECS 更新渲染对象的位置（模拟 ECS-驱动渲染）
    GameObject* ecsSprite = m_Scene.FindObject("ECSSprite");
    if (ecsSprite) {
        // 正弦波运动 —— 验证渲染链路完整
        float32 x = 3.0f * std::sin(s_Time * 0.8f);
        float32 y = 2.0f * std::cos(s_Time * 0.6f);
        ecsSprite->GetTransform().SetPosition(x, y, 0.0f);

        // 颜色随时间变化
        float32 r = 0.5f + 0.5f * std::sin(s_Time * 0.5f);
        float32 g = 0.5f + 0.5f * std::sin(s_Time * 0.7f + 2.0f);
        float32 b = 0.5f + 0.5f * std::sin(s_Time * 0.9f + 4.0f);
        ecsSprite->GetSprite().SetColor(r, g, b, 1.0f);

        // 缩放脉冲
        float32 scale = 0.8f + 0.4f * std::sin(s_Time * 1.2f);
        ecsSprite->GetTransform().SetScale(scale);
    }

    m_Scene.Update(dt);
}

void ECSTest::PrintHelp() {
    std::cout << "==============================================" << std::endl;
    std::cout << "  ECS + RHI Integration Test" << std::endl;
    std::cout << "==============================================" << std::endl;
    std::cout << "  验证内容:" << std::endl;
    std::cout << "    [1] Entity Creation/Destruction" << std::endl;
    std::cout << "    [2] Component Add/Get/Remove" << std::endl;
    std::cout << "    [3] Archetype Query (With/Without)" << std::endl;
    std::cout << "    [4] ECSBridge (双向映射)" << std::endl;
    std::cout << "    [5] Physics Component Storage" << std::endl;
    std::cout << "    [*] RHI Render Loop (ECS-driven)" << std::endl;
    std::cout << std::endl;
    std::cout << "  键盘操作:" << std::endl;
    std::cout << "    Escape     退出" << std::endl;
    std::cout << "==============================================" << std::endl;
}

// ═══════════════════════════════════════════════════════
// 主循环
// ═══════════════════════════════════════════════════════
void ECSTest::Run() {
    m_LastFrameTime = Time::GetTime();

    std::cout << "\n=== RHI Render Loop ===" << std::endl;

    while (!m_Window->ShouldClose() && !m_ShouldClose) {
        float32 time = Time::GetTime();
        float32 dt = time - m_LastFrameTime;
        m_LastFrameTime = time;
        if (dt > 0.25f) dt = 0.25f;

        m_InputManager.OnUpdate();
        RunRenderingTest(dt);

        // 渲染
        auto ctx = m_Window->GetContext();
        ctx->ClearColor(0.05f, 0.05f, 0.1f, 1.0f);

        auto defaultTex = m_TextureManager.Load("assets/textures/default.png");
        m_SceneRenderer.Render(m_Scene, defaultTex);

        m_Window->OnUpdate();
    }

    std::cout << "\n=== ECS Test Complete ===" << std::endl;
}

} // namespace Engine