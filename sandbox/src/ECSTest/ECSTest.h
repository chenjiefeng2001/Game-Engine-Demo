#pragma once

/**
 * @file ECSTest.h
 * @brief ECS + RHI 集成测试
 *
 * 验证内容：
 *   1. ECS 核心：EntityManager 实体创建/销毁、组件 CRUD、Query 遍历
 *   2. ECS → GameObject 桥接：通过 ECSBridge 测试双向映射
 *   3. RHI 渲染链路：创建窗口、SpriteBatch、ECS 驱动的渲染循环
 *   4. 物理组件集成：验证 RigidBody3DComponent/BoxCollider3DComponent 的 ECS 存储
 */

#include <Engine/Core/IWindow.h>
#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/Renderer/OrthographicCamera.h>
#include <Engine/Core/RenderResources/Texture.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/Core/RHI/SceneRenderer.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/InputManager.h>
#include <Engine/Core/ECS/ECS.h>
#include <Engine/Core/ECS/ECSBridge.h>
#include <Engine/Core/ECS/PhysicsComponents.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Types.h>
#include <memory>
#include <vector>
#include <cstdio>

namespace Engine {

// ═══════════════════════════════════════════════════════
// 自定义 ECS 组件（测试非 POD 类型）
// ═══════════════════════════════════════════════════════
struct HealthComponent {
    int32 hp = 100;
    int32 maxHP = 100;

    void TakeDamage(int32 dmg) {
        hp = (std::max)(0, hp - dmg);
    }
    bool IsDead() const { return hp <= 0; }
};

struct DamageComponent {
    int32 damagePerSecond = 10;
    float32 timer = 0.0f;
};

// ═══════════════════════════════════════════════════════
// 应用类
// ═══════════════════════════════════════════════════════
class ECSTest {
public:
    ECSTest(IGraphicsFactory& factory);
    ~ECSTest();

    void Run();

private:
    void RunCoreTests();
    void RunRenderingTest(float32 dt);
    void PrintHelp();

    // ── 测试函数 ──
    bool TestEntityCreation();
    bool TestComponentCRUD();
    bool TestEntityQuery();
    bool TestECSBridge();
    bool TestPhysicsComponents();

    // ── 报告 ──
    void ReportTestResult(const char* name, bool passed);
    int  m_PassedTests = 0;
    int  m_TotalTests  = 0;

    // ── 依赖 ──
    IGraphicsFactory&  m_Factory;
    TextureManager     m_TextureManager;
    SceneRenderer      m_SceneRenderer;
    InputManager       m_InputManager;

    std::unique_ptr<IWindow> m_Window;
    OrthographicCamera m_Camera;

    // ECS
    EntityManager m_EntityMgr;

    // 渲染场景（OOP GameObject）
    Scene m_Scene;

    std::shared_ptr<Texture> m_Texture;

    bool m_ShouldClose = false;
    float32 m_LastFrameTime = 0.0f;

    static constexpr int32 WINDOW_WIDTH  = 800;
    static constexpr int32 WINDOW_HEIGHT = 600;
};

} // namespace Engine