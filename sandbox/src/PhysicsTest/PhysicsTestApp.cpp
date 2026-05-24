#include "PhysicsTestApp.h"
#include <Engine/Platform/PlatformUtils.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/IWindow.h>
#include <Engine/Box2D/Box2DPhysicsWorld.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include <cmath>
#include <cstdlib>

namespace Engine {

    // ============================================================
    // 构造：初始化所有系统
    // ============================================================

    PhysicsTestApp::PhysicsTestApp(IGraphicsFactory& factory)
        : m_Factory(factory)
        , m_TextureManager(factory)
    {
        // ── 1. 创建窗口 ──
        m_Window = m_Factory.CreateWindow(m_WindowWidth, m_WindowHeight,
                                          "Physics Test (Box2D)");

        // ── 2. 创建正交相机 ──
        float32 aspect = static_cast<float32>(m_WindowWidth) /
                         static_cast<float32>(m_WindowHeight);
        float32 viewHeight = 12.0f;
        float32 viewWidth  = viewHeight * aspect;
        m_Camera = std::make_unique<OrthographicCamera>(
            -viewWidth * 0.5f, viewWidth * 0.5f,
            -viewHeight * 0.5f, viewHeight * 0.5f);

        // ── 3. 创建渲染资源 ──
        auto* context = m_Window->GetContext();
        m_SpriteBatch = m_Factory.CreateSpriteBatch(*context);
        m_BatchShader = m_Factory.CreateShader(
            "assets/shaders/sprite_batch.vert",
            "assets/shaders/sprite_batch.frag"
        );
        m_Texture = m_TextureManager.Load("assets/textures/test.png");

        // ── 4. 创建物理世界（重力向下） ──
        m_PhysicsWorld = std::make_shared<Box2DPhysicsWorld>(
            Vec2(0.0f, -9.81f)  // 地球重力
        );

        // ── 5. 碰撞回调示例 ──
        m_PhysicsWorld->SetContactBeginCallback(
            [](const ContactInfo& info) {
                std::cout << "[Physics] Collision detected!" << std::endl;
                (void)info;
            }
        );

        // ── 6. 创建地面（静态刚体） ──
        {
            BodyDef groundDef;
            groundDef.type = BodyType::Static;
            groundDef.position = Vec2(0.0f, -5.0f);
            groundDef.shape.type = ShapeType::Box;
            groundDef.shape.boxSize = Vec2(8.0f, 0.5f);
            groundDef.friction = 0.5f;
            m_PhysicsWorld->CreateBody(groundDef);
        }

        // ── 7. 创建多个带物理的箱子（动态刚体） ──
        {
            // 左边的堆叠
            for (int32 i = 0; i < 5; i++) {
                auto box = std::make_shared<GameObject>(
                    "Box_Left_" + std::to_string(i));
                box->GetTransform().SetPosition(
                    -2.0f, -3.0f + i * 1.2f, 0.0f);
                box->GetSprite().SetTexture(m_Texture);
                box->GetSprite().SetColor(0.8f, 0.3f, 0.3f, 1.0f);

                BodyDef bodyDef;
                bodyDef.type = BodyType::Dynamic;
                bodyDef.shape.type = ShapeType::Box;
                bodyDef.shape.boxSize = Vec2(0.45f, 0.45f);
                bodyDef.density  = 1.0f;
                bodyDef.friction = 0.5f;
                box->GetPhysics().CreateBody(m_PhysicsWorld, bodyDef);

                // 初始位置同步到物理体
                const auto& pos = box->GetTransform().GetPosition();
                box->GetPhysics().SyncTransformToPhysics(
                    Vec2(pos.x, pos.y), 0.0f);

                m_Scene.AddObject(std::move(box));
            }

            // 右边的堆叠
            for (int32 i = 0; i < 5; i++) {
                auto box = std::make_shared<GameObject>(
                    "Box_Right_" + std::to_string(i));
                box->GetTransform().SetPosition(
                    2.0f, -3.0f + i * 1.2f, 0.0f);
                box->GetSprite().SetTexture(m_Texture);
                box->GetSprite().SetColor(0.3f, 0.5f, 0.8f, 1.0f);

                BodyDef bodyDef;
                bodyDef.type = BodyType::Dynamic;
                bodyDef.shape.type = ShapeType::Box;
                bodyDef.shape.boxSize = Vec2(0.45f, 0.45f);
                bodyDef.density  = 1.0f;
                bodyDef.friction = 0.5f;
                box->GetPhysics().CreateBody(m_PhysicsWorld, bodyDef);

                const auto& pos = box->GetTransform().GetPosition();
                box->GetPhysics().SyncTransformToPhysics(
                    Vec2(pos.x, pos.y), 0.0f);

                m_Scene.AddObject(std::move(box));
            }
        }

        // ── 8. 创建圆形物体 ──
        {
            auto ball = std::make_shared<GameObject>("Ball");
            ball->GetTransform().SetPosition(0.0f, 4.0f, 0.0f);
            ball->GetSprite().SetTexture(m_Texture);
            ball->GetSprite().SetColor(0.2f, 0.8f, 0.3f, 1.0f);

            BodyDef bodyDef;
            bodyDef.type = BodyType::Dynamic;
            bodyDef.shape.type = ShapeType::Circle;
            bodyDef.shape.circleRadius = 0.5f;
            bodyDef.density  = 1.0f;
            bodyDef.friction = 0.3f;
            bodyDef.restitution = 0.6f;  // 有弹性的球
            ball->GetPhysics().CreateBody(m_PhysicsWorld, bodyDef);

            const auto& pos = ball->GetTransform().GetPosition();
            ball->GetPhysics().SyncTransformToPhysics(
                Vec2(pos.x, pos.y), 0.0f);

            m_Scene.AddObject(std::move(ball));
        }

        std::cout << "==========================================" << std::endl;
        std::cout << "  Physics Test (Box2D) Started!" << std::endl;
        std::cout << "  测试内容:" << std::endl;
        std::cout << "    - 左右堆叠的箱子（碰撞 + 堆叠）" << std::endl;
        std::cout << "    - 弹性球从空中掉落" << std::endl;
        std::cout << "    - 物理步进后位置自动同步到精灵" << std::endl;
        std::cout << "    - 碰撞事件回调" << std::endl;
        std::cout << "==========================================" << std::endl;
    }

    // ============================================================
    // 析构
    // ============================================================

    PhysicsTestApp::~PhysicsTestApp() {
        // PhysicsComponent 通过 shared_ptr 持有物理世界引用，
        // 自动管理生命周期，无需手动 reset。
    }

    // ============================================================
    // 每帧更新
    // ============================================================

    void PhysicsTestApp::Update(float32 dt) {
        (void)dt;

        // ── 物理步进（固定步长 60Hz） ──
        // 使用固定步长累加器保证物理稳定性
        static float32 accumulator = 0.0f;
        accumulator += dt;
        while (accumulator >= FIXED_DT) {
            m_PhysicsWorld->Step(FIXED_DT, 8, 3);
            accumulator -= FIXED_DT;
        }

        // ── 物理同步：将 Box2D 位置写回 TransformComponent ──
        m_Scene.PostPhysicsUpdate();
    }

    // ============================================================
    // 每帧渲染
    // ============================================================

    void PhysicsTestApp::Render() {
        auto context = m_Window->GetContext();
        context->ClearColor(0.1f, 0.1f, 0.15f, 1.0f);

        // ── 使用 SceneRenderer 风格渲染每个对象 ──
        m_BatchShader->Bind();
        m_BatchShader->SetMat4("u_ViewProjection",
            m_Camera->GetViewProjectionMatrixPtr());

        // 遍历场景中所有对象并提交精灵
        m_SpriteBatch->Begin(m_Texture);

        std::function<void(GameObject&)> renderRecursive =
            [&](GameObject& obj) {
                if (obj.HasSprite() && obj.IsActive()) {
                    obj.SubmitSprite(*m_SpriteBatch);
                }
                for (auto& child : obj.GetChildren()) {
                    renderRecursive(*child);
                }
            };

        for (auto& obj : m_Scene.GetObjects()) {
            renderRecursive(*obj);
        }

        m_SpriteBatch->End();
    }

    // ============================================================
    // 主循环
    // ============================================================

    void PhysicsTestApp::Run() {
        m_LastFrameTime = Time::GetTime();

        while (!m_Window->ShouldClose()) {
            float32 time = Time::GetTime();
            float32 dt = time - m_LastFrameTime;
            m_LastFrameTime = time;
            if (dt > 0.25f) dt = 0.25f;

            // ── 主循环顺序 ──
            glfwPollEvents();
            Update(dt);
            Render();

            auto ctx = m_Window->GetContext();
            ctx->SwapBuffers();
        }
    }

} // namespace Engine
