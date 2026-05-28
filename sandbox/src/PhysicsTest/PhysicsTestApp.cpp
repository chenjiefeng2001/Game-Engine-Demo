#include "PhysicsTestApp.h"
#include <Engine/Platform/PlatformUtils.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/IWindow.h>
#include <Engine/OpenGL/OpenGLContext.h>
#include <Engine/Box2D/Box2DPhysicsWorld.h>
#include <Engine/Core/GameObject/SpriteComponent.h>
#include <Engine/Core/Physics/PhysicsComponent.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
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

        // ── 窗口大小变化回调 ──
        m_Window->SetEventCallback([this](Event& e) {
            if (e.type == EventType::WindowResize) {
                m_WindowWidth  = e.resize.width;
                m_WindowHeight = e.resize.height;

                // 重建相机（更新宽高比）
                float32 aspect = static_cast<float32>(m_WindowWidth) /
                                 static_cast<float32>(m_WindowHeight);
                float32 viewHeight = 12.0f;
                float32 viewWidth  = viewHeight * aspect;
                m_Camera = OrthographicCamera(
                    -viewWidth * 0.5f, viewWidth * 0.5f,
                    -viewHeight * 0.5f, viewHeight * 0.5f);

                // 更新 OpenGL 视口
                auto* ctx = m_Window->GetContext();
                if (ctx) ctx->OnResize(m_WindowWidth, m_WindowHeight);
            }
        });

        // ── 2. 创建正交相机 ──
        float32 aspect = static_cast<float32>(m_WindowWidth) /
                         static_cast<float32>(m_WindowHeight);
        float32 viewHeight = 12.0f;
        float32 viewWidth  = viewHeight * aspect;
        m_Camera = OrthographicCamera(
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

        // ── 创建鼠标锚点（隐藏的静态 body，MouseJoint 的 bodyA） ──
        {
            BodyDef anchorDef;
            anchorDef.type = BodyType::Static;
            anchorDef.position = Vec2(0.0f, 0.0f);
            m_MouseAnchorBody = m_PhysicsWorld->CreateBody(anchorDef);
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

        // ── 8. 创建圆形物体（演示碰撞回调 + 持久回调） ──
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
            bodyDef.restitution = 0.6f;
            ball->GetPhysics().CreateBody(m_PhysicsWorld, bodyDef);

            // 演示：碰撞回调（游戏对象级别）
            ball->GetPhysics().SetOnCollisionEnter([](const ContactInfo& info) {
                std::cout << "[Collision Enter] Ball collided!" << std::endl;
                (void)info;
            });
            ball->GetPhysics().SetOnCollisionStay([](const ContactPersistData& data) {
                if (data.impulse > 1.0f) {
                    // 只在冲量较大时输出，避免刷屏
                    std::cout << "[Collision Stay] Impulse: " << data.impulse << std::endl;
                }
            });
            ball->GetPhysics().SetOnCollisionExit([](const ContactInfo& info) {
                std::cout << "[Collision Exit] Ball collision ended" << std::endl;
                (void)info;
            });

            const auto& pos = ball->GetTransform().GetPosition();
            ball->GetPhysics().SyncTransformToPhysics(
                Vec2(pos.x, pos.y), 0.0f);

            m_Scene.AddObject(std::move(ball));
        }

        // ── 9. 距离关节演示（弹簧连接两个箱子） ──
        {
            // 创建两个箱子
            auto boxA = std::make_shared<GameObject>("SpringBoxA");
            boxA->GetTransform().SetPosition(-1.0f, 0.0f, 0.0f);
            boxA->GetSprite().SetTexture(m_Texture);
            boxA->GetSprite().SetColor(0.9f, 0.6f, 0.2f, 1.0f);
            {
                BodyDef bd;
                bd.type = BodyType::Dynamic;
                bd.shape.type = ShapeType::Box;
                bd.shape.boxSize = {0.4f, 0.4f};
                boxA->GetPhysics().CreateBody(m_PhysicsWorld, bd);
                const auto& p = boxA->GetTransform().GetPosition();
                boxA->GetPhysics().SyncTransformToPhysics(Vec2(p.x, p.y), 0.0f);
            }

            auto boxB = std::make_shared<GameObject>("SpringBoxB");
            boxB->GetTransform().SetPosition(1.0f, 0.0f, 0.0f);
            boxB->GetSprite().SetTexture(m_Texture);
            boxB->GetSprite().SetColor(0.2f, 0.6f, 0.9f, 1.0f);
            {
                BodyDef bd;
                bd.type = BodyType::Dynamic;
                bd.shape.type = ShapeType::Box;
                bd.shape.boxSize = {0.4f, 0.4f};
                boxB->GetPhysics().CreateBody(m_PhysicsWorld, bd);
                const auto& p = boxB->GetTransform().GetPosition();
                boxB->GetPhysics().SyncTransformToPhysics(Vec2(p.x, p.y), 0.0f);
            }

            // 用距离关节（弹簧）连接它们
            DistanceJointDef jd;
            jd.bodyA = boxA->GetPhysics().GetBody();
            jd.bodyB = boxB->GetPhysics().GetBody();
            jd.localAnchorA = Vec2(0.5f, 0.0f);
            jd.localAnchorB = Vec2(-0.5f, 0.0f);
            jd.length = 3.0f;      // 静止长度
            jd.stiffness = 50.0f;  // 弹簧刚度
            jd.damping = 2.0f;     // 阻尼
            m_PhysicsWorld->CreateJoint(jd);

            m_Scene.AddObject(std::move(boxA));
            m_Scene.AddObject(std::move(boxB));
        }

        // ── 10. 初始化调试绘制（必须在 m_PhysicsWorld 创建之后） ──
        {
            auto* context = m_Window->GetContext();
            auto& glContext = static_cast<OpenGLContext*>(context)->GetGL();
            m_DebugDraw = std::make_unique<OpenGLPhysicsDebugDraw>(glContext);
            m_DebugDraw->SetFlags(DebugDraw_Shape | DebugDraw_Joint | DebugDraw_COM);
            m_PhysicsWorld->SetDebugDraw(m_DebugDraw.get());
        }

        std::cout << "==========================================" << std::endl;
        std::cout << "  Physics Test (Box2D) Started!" << std::endl;
        std::cout << "  测试内容:" << std::endl;
        std::cout << "    - 左右堆叠的箱子（碰撞 + 堆叠）" << std::endl;
        std::cout << "    - 弹性球从空中掉落（碰撞回调演示）" << std::endl;
        std::cout << "    - 距离关节（橙色↔蓝色弹簧连接）" << std::endl;
        std::cout << "    - 鼠标拖拽（点击拖动物体）" << std::endl;
        std::cout << "    - 物理步进后位置自动同步到精灵" << std::endl;
        std::cout << "    - 碰撞开始/持续/结束回调" << std::endl;
        std::cout << "    - 调试绘制（F1~F5 热切换）" << std::endl;
        std::cout << "==========================================" << std::endl;

        PrintDebugHelp();
    }

    // ============================================================
    // 析构
    // ============================================================

    PhysicsTestApp::~PhysicsTestApp() {
        // PhysicsComponent 通过 shared_ptr 持有物理世界引用，
        // 自动管理生命周期，无需手动 reset。
    }

    // ============================================================
    // 热键处理（调试绘制开关）
    // ============================================================

    namespace {
        // 记录按键按下状态，实现边沿触发
        bool WasKeyPressed(GLFWwindow* window, int key, bool& lastState) {
            bool current = glfwGetKey(window, key) == GLFW_PRESS;
            bool pressed = current && !lastState;
            lastState = current;
            return pressed;
        }
    }

    Vec2 PhysicsTestApp::ScreenToWorld(float32 screenX, float32 screenY) const {
        // 使用当前相机的视图投影矩阵计算（支持实时缩放/移动）
        float32 ndcX = 2.0f * screenX / static_cast<float32>(m_WindowWidth) - 1.0f;
        float32 ndcY = 1.0f - 2.0f * screenY / static_cast<float32>(m_WindowHeight);

        const float32* vp = m_Camera.GetViewProjectionMatrixPtr();
        glm::mat4 viewProj = glm::make_mat4(vp);
        glm::mat4 invVP = glm::inverse(viewProj);

        glm::vec4 world = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        return Vec2(world.x / world.w, world.y / world.w);
    }

    void PhysicsTestApp::PrintDebugHelp() {
        std::cout << "=== 操作说明 ===" << std::endl;
        std::cout << "  鼠标左键拖拽   : 拖动物体" << std::endl;
        std::cout << "=== 调试绘制热键 ===" << std::endl;
        std::cout << "  F1   : 切换所有调试绘制" << std::endl;
        std::cout << "  F2   : 切换碰撞体形状" << std::endl;
        std::cout << "  F3   : 切换关节" << std::endl;
        std::cout << "  F4   : 切换包围盒(AABB)" << std::endl;
        std::cout << "  F5   : 切换质心" << std::endl;
        std::cout << "=======================" << std::endl;
    }

    // ============================================================
    // 每帧更新
    // ============================================================

    void PhysicsTestApp::Update(float32 dt) {
        (void)dt;

        // ── 调试绘制热键（热重载开关） ──
        GLFWwindow* window = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());
        if (window) {
            static bool f1 = false, f2 = false, f3 = false, f4 = false, f5 = false;

            if (WasKeyPressed(window, GLFW_KEY_F1, f1)) {
                m_DebugDrawEnabled = !m_DebugDrawEnabled;
                m_PhysicsWorld->SetDebugDraw(m_DebugDrawEnabled ? m_DebugDraw.get() : nullptr);
                std::cout << (m_DebugDrawEnabled ? "[Debug] 开启" : "[Debug] 关闭") << std::endl;
            }
            if (WasKeyPressed(window, GLFW_KEY_F2, f2)) {
                uint32 flags = m_DebugDraw->GetFlags();
                flags ^= DebugDraw_Shape;
                m_DebugDraw->SetFlags(flags);
                std::cout << "[Debug] 形状: " << ((flags & DebugDraw_Shape) ? "ON" : "OFF") << std::endl;
            }
            if (WasKeyPressed(window, GLFW_KEY_F3, f3)) {
                uint32 flags = m_DebugDraw->GetFlags();
                flags ^= DebugDraw_Joint;
                m_DebugDraw->SetFlags(flags);
                std::cout << "[Debug] 关节: " << ((flags & DebugDraw_Joint) ? "ON" : "OFF") << std::endl;
            }
            if (WasKeyPressed(window, GLFW_KEY_F4, f4)) {
                uint32 flags = m_DebugDraw->GetFlags();
                flags ^= DebugDraw_AABB;
                m_DebugDraw->SetFlags(flags);
                std::cout << "[Debug] AABB: " << ((flags & DebugDraw_AABB) ? "ON" : "OFF") << std::endl;
            }
            if (WasKeyPressed(window, GLFW_KEY_F5, f5)) {
                uint32 flags = m_DebugDraw->GetFlags();
                flags ^= DebugDraw_COM;
                m_DebugDraw->SetFlags(flags);
                std::cout << "[Debug] 质心: " << ((flags & DebugDraw_COM) ? "ON" : "OFF") << std::endl;
            }
        }

        // ── 鼠标拖拽 ──
        if (window) {
            // 获取鼠标屏幕位置
            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);
            m_MouseWorldPos = ScreenToWorld(static_cast<float32>(mouseX),
                                             static_cast<float32>(mouseY));

            bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

            if (leftDown && !m_MouseDown) {
                // 鼠标按下：AABB 查询寻找鼠标位置下的物体
                // (不能用 RayCast 零长度射线，Box2D 会断言失败)
                m_MouseDown = true;
                const float32 queryHalf = 0.1f;
                auto hits = m_PhysicsWorld->QueryAABB(m_MouseWorldPos,
                                                       Vec2(queryHalf, queryHalf));
                for (auto* body : hits) {
                    if (body && body->GetType() == BodyType::Dynamic) {
                        // 创建鼠标关节：bodyA = 静态锚点, bodyB = 被拖拽的物体
                        MouseJointDef jd;
                        jd.bodyA = m_MouseAnchorBody.get();
                        jd.bodyB = body;
                        jd.target = m_MouseWorldPos;
                        jd.maxForce = 500.0f;
                        jd.stiffness = 100.0f;
                        jd.damping = 5.0f;
                        m_MouseJoint = m_PhysicsWorld->CreateJoint(jd);
                        m_DraggedBody = body;
                        std::cout << "[Mouse] 开始拖拽物体" << std::endl;
                        break;
                    }
                }
            } else if (!leftDown && m_MouseDown) {
                // 鼠标释放：销毁鼠标关节
                m_MouseDown = false;
                if (m_MouseJoint) {
                    m_PhysicsWorld->DestroyJoint(m_MouseJoint.get());
                    m_MouseJoint.reset();
                    m_DraggedBody = nullptr;
                    std::cout << "[Mouse] 释放物体" << std::endl;
                }
            } else if (leftDown && m_MouseJoint) {
                // 拖拽中：更新鼠标关节目标位置
                // 通过 IJoint 的 SetTarget 实现
                m_MouseJoint->SetTarget(m_MouseWorldPos);
            }
        }

        // ── 物理步进（固定步长 60Hz） ──
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
            m_Camera.GetViewProjectionMatrixPtr());

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

        // ── 调试绘制（叠加在精灵之上） ──
        if (m_DebugDrawEnabled && m_DebugDraw) {
            m_DebugDraw->SetViewProjection(m_Camera.GetViewProjectionMatrixPtr());
            m_PhysicsWorld->DebugDraw();
        }
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
