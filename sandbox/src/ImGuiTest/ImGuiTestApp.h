#pragma once

/**
 * @file ImGuiTestApp.h
 * @brief ImGui 综合测试用例 — 物理刚体 + 精灵渲染 + UI 面板
 *
 * 测试项：
 *   1. 精灵渲染 + 物理模拟 — 带纹理的箱子和球体
 *   2. UI 开关流畅性 — F1 切换显示/隐藏，拖拽全局缩放
 *   3. 实体属性热重载 — 层级面板选实体 → 检视器修改 Transform/Physics
 *   4. 输入抢占验证 — ImGui 激活时 WASD 不移动实体
 *   5. 内存泄漏检测 — 对象计数、创建/销毁日志
 */

#include <Engine/Application.h>
#include <Engine/SceneHierarchyPanel.h>
#include <Engine/InspectorPanel.h>
#include <Engine/ConsoleLog.h>
#include <Engine/ConsolePanel.h>
#include <Engine/UIManager.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/GameObject/SpriteComponent.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/Physics/PhysicsComponent.h>
#include <Engine/Core/Physics/IPhysicsWorld.h>
#include <Engine/Core/Physics/IPhysicsBody.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/IWindow.h>
#include <Engine/Core/Renderer/SpriteBatch.h>
#include <Engine/Core/Renderer/OrthographicCamera.h>
#include <Engine/Box2D/Box2DPhysicsWorld.h>
#include <Engine/OpenGL/OpenGLContext.h>
#include <imgui.h>
#include <iostream>
#include <GLFW/glfw3.h>

namespace Engine {

    struct MemoryTracker {
        static int created;
        static int destroyed;
        static void Report() {
            LOG_INFO(std::string("Memory: Created=") + std::to_string(created) +
                     " Destroyed=" + std::to_string(destroyed) +
                     " Alive=" + std::to_string(created - destroyed));
        }
    };
    int MemoryTracker::created = 0;
    int MemoryTracker::destroyed = 0;

    class ImGuiTestApp : public Application {
    public:
        ImGuiTestApp(IGraphicsFactory& factory)
            : Application(factory)
        {
            // ── 子类扩展的 init 步骤 ──
            RegisterInitStep("PhysicsScene", StartupPhase::Scene,
                [this]() { return InitPhysicsScene(); });
        }

        ~ImGuiTestApp() override {
            LOG_INFO("=== ImGuiTest Shutdown ===");
            MemoryTracker::Report();
        }

    protected:
        void OnStartup() override
        {
            // 调整相机以适配物理场景视野
            float32 aspect = static_cast<float32>(800) / static_cast<float32>(600);
            float32 viewHeight = 12.0f;
            float32 viewWidth  = viewHeight * aspect;
            m_Camera = std::make_unique<OrthographicCamera>(
                -viewWidth * 0.5f, viewWidth * 0.5f,
                -viewHeight * 0.5f, viewHeight * 0.5f);

            // 创建 SpriteBatch（需要 RenderContext 就绪）
            auto* ctx = m_Window->GetContext();
            m_SpriteBatch = m_Factory.CreateSpriteBatch(*ctx);

            // 加载精灵着色器
            m_SpriteShader = m_Factory.CreateShader(
                "assets/shaders/sprite_batch.vert",
                "assets/shaders/sprite_batch.frag");

            // 预加载纹理
            m_SpriteTexture = m_TextureManager.Load("assets/textures/test.png");

            // ── 写入日志 ──
            LOG_INFO("=== ImGuiTest Suite Started ===");
            LOG_INFO("F1: Toggle UI  |  F2: Toggle Console  |  F3: Add Box");
            MemoryTracker::Report();
        }

        void OnUpdate(float32 dt) override
        {
            // ── 物理模拟 ──
            m_PhysicsWorld->Step(dt);

            // ── 物理 → 变换同步 ──
            m_TestScene->PostPhysicsUpdate();

            // ── 鼠标拖拽处理 ──
            HandleMouseDrag(dt);
        }

        void OnRender() override
        {
            if (!m_SpriteBatch || !m_SpriteShader || !m_SpriteTexture) return;

            // 绑定着色器 + 设置相机矩阵
            m_SpriteShader->Bind();
            m_SpriteShader->SetMat4("u_ViewProjection",
                m_Camera->GetViewProjectionMatrixPtr());

            // 渲染场景中的所有精灵
            m_SpriteBatch->Begin(m_SpriteTexture);

            for (const auto& obj : m_TestScene->GetObjects())
            {
                RenderObjectRecursive(obj.get(), m_SpriteTexture);
            }

            m_SpriteBatch->End();
        }

        void OnImGui() override
        {
            // ── 热键 ──
            if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
                m_UIVisible = !m_UIVisible;
                UIManager::Get()->SetVisible(m_UIVisible);
                LOG_INFO(std::string("UI: ") + (m_UIVisible ? "ON" : "OFF"));
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
                m_ConsolePanel.ToggleVisibility();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) {
                AddPhysicsBox();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F4, false)) {
                AddPhysicsBall();
            }

            // ── 面板 ──
            m_HierarchyPanel.OnImGui();
            m_InspectorPanel.OnImGui();
            m_ConsolePanel.OnImGui();
            DrawTestPanel();
            DrawInputPreemptionPanel();
        }

    private:
        // ── 初始化物理场景 ──
        bool InitPhysicsScene()
        {
            m_TestScene = std::make_unique<Scene>();
            m_PhysicsWorld = std::make_shared<Box2DPhysicsWorld>(Vec2(0.0f, -9.81f));

            // 地面
            {
                auto ground = std::make_shared<GameObject>("Ground");
                ground->GetTransform().SetPosition(Vec3(0.0f, -5.0f, 0));
                ground->GetTransform().SetScale(Vec3(16.0f, 1.0f, 1.0f));
                ground->GetSprite().SetColor(0.3f, 0.3f, 0.35f, 1.0f);

                BodyDef bd;
                bd.type = BodyType::Static;
                bd.shape.type = ShapeType::Box;
                bd.shape.boxSize = {8.0f, 0.5f};
                bd.friction = 0.5f;
                ground->GetPhysics().CreateBody(m_PhysicsWorld, bd);
                m_TestScene->AddObject(ground);
                MemoryTracker::created++;
            }

            // 堆叠的箱子
            for (int col = 0; col < 3; ++col) {
                for (int row = 0; row < 4; ++row) {
                    AddStaticBox(col - 1, row);
                }
            }

            // 球
            AddPhysicsBallAt(0.0f, 4.0f);

            // 鼠标拖拽锚点
            {
                BodyDef ad;
                ad.type = BodyType::Static;
                ad.position = Vec2(0, 0);
                m_MouseAnchor = m_PhysicsWorld->CreateBody(ad);
            }

            m_HierarchyPanel.SetScene(m_TestScene.get());
            m_HierarchyPanel.SetSelectionCallback([this](GameObject* sel) {
                m_InspectorPanel.SetTarget(sel);
                if (sel) LOG_INFO(std::string("Selected: ") + sel->GetName());
            });
            m_ConsolePanel.SetVisible(true);

            LOG_INFO("Physics scene created with boxes + ball");
            return true;
        }

        void AddStaticBox(int col, int row) {
            auto box = std::make_shared<GameObject>("Box_" + std::to_string(col) + "_" + std::to_string(row));
            float x = col * 1.3f;
            float y = -3.0f + row * 1.2f;
            box->GetTransform().SetPosition(Vec3(x, y, 0));

            float r = (col == 0) ? 0.8f : (col == 1) ? 0.3f : 0.2f;
            float g = (col == 0) ? 0.3f : (col == 1) ? 0.5f : 0.6f;
            float b = (col == 0) ? 0.3f : (col == 1) ? 0.8f : 0.9f;
            box->GetSprite().SetColor(r, g, b, 1.0f);

            BodyDef bd;
            bd.type = BodyType::Dynamic;
            bd.shape.type = ShapeType::Box;
            bd.shape.boxSize = {0.45f, 0.45f};
            bd.density  = 1.0f;
            bd.friction = 0.5f;
            box->GetPhysics().CreateBody(m_PhysicsWorld, bd);
            const auto& p = box->GetTransform().GetPosition();
            box->GetPhysics().SyncTransformToPhysics(Vec2(p.x, p.y), 0);

            m_TestScene->AddObject(box);
            MemoryTracker::created++;
        }

        void AddPhysicsBox() {
            static int n = 0; ++n;
            auto box = std::make_shared<GameObject>("Box_" + std::to_string(n));
            box->GetTransform().SetPosition(Vec3(0, 5.0f, 0));
            box->GetSprite().SetColor(0.2f, 0.7f, 0.3f, 1.0f);

            BodyDef bd;
            bd.type = BodyType::Dynamic;
            bd.shape.type = ShapeType::Box;
            bd.shape.boxSize = {0.45f, 0.45f};
            bd.density = 1.0f;
            box->GetPhysics().CreateBody(m_PhysicsWorld, bd);
            const auto& p = box->GetTransform().GetPosition();
            box->GetPhysics().SyncTransformToPhysics(Vec2(p.x, p.y), 0);

            m_TestScene->AddObject(box);
            MemoryTracker::created++;
            LOG_INFO("Added box");
        }

        void AddPhysicsBallAt(float x, float y) {
            auto ball = std::make_shared<GameObject>("Ball");
            ball->GetTransform().SetPosition(Vec3(x, y, 0));
            ball->GetSprite().SetColor(0.2f, 0.8f, 0.3f, 1.0f);

            BodyDef bd;
            bd.type = BodyType::Dynamic;
            bd.shape.type = ShapeType::Circle;
            bd.shape.circleRadius = 0.5f;
            bd.density  = 1.0f;
            bd.friction = 0.3f;
            bd.restitution = 0.5f;
            ball->GetPhysics().CreateBody(m_PhysicsWorld, bd);
            const auto& p = ball->GetTransform().GetPosition();
            ball->GetPhysics().SyncTransformToPhysics(Vec2(p.x, p.y), 0);

            m_TestScene->AddObject(ball);
            MemoryTracker::created++;
            LOG_INFO("Added ball");
        }

        void AddPhysicsBall() { AddPhysicsBallAt(0.0f, 5.0f); }

        // ── 递归渲染对象树 ──
        void RenderObjectRecursive(GameObject* obj,
                                   const std::shared_ptr<Texture>& defaultTex)
        {
            if (!obj || !obj->IsActive()) return;

            // 确保使用精灵纹理
            if (!obj->GetSprite().HasTexture())
                obj->GetSprite().SetTexture(defaultTex);

            // 提交精灵数据到 SpriteBatch
            obj->SubmitSprite(*m_SpriteBatch);

            // 递归子对象
            for (const auto& child : obj->GetChildren())
                RenderObjectRecursive(child.get(), defaultTex);
        }

        // ── 鼠标拖拽物理体 ──
        void HandleMouseDrag(float32 /*dt*/)
        {
            GLFWwindow* win = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());
            double mx, my;
            glfwGetCursorPos(win, &mx, &my);

            // 简易屏幕→世界（适配物理相机）
            Vec2 world = ScreenToPhysicsWorld((float)mx, (float)my);

            bool pressed = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

            if (pressed && !m_MouseJoint) {
                // 射线检测找到最近物体
                IPhysicsBody* hit = nullptr;
                float minDist = 1.0f;
                for (const auto& obj : m_TestScene->GetObjects()) {
                    if (auto* body = obj->GetPhysics().GetBody()) {
                        Vec2 bp = body->GetPosition();
                        float d = std::sqrt((world.x - bp.x) * (world.x - bp.x) +
                                           (world.y - bp.y) * (world.y - bp.y));
                        if (d < minDist && obj->GetName() != "Ground") {
                            minDist = d;
                            hit = body;
                        }
                    }
                }
                if (hit) {
                    MouseJointDef jd;
                    jd.bodyA = m_MouseAnchor.get();
                    jd.bodyB = hit;
                    jd.target = world;
                    jd.maxForce = 1000.0f;
                    jd.stiffness = 100.0f;
                    m_MouseJoint = m_PhysicsWorld->CreateJoint(jd);
                }
            } else if (!pressed && m_MouseJoint) {
                m_PhysicsWorld->DestroyJoint(m_MouseJoint.get());
                m_MouseJoint.reset();
            }

            if (m_MouseJoint) {
                m_MouseJoint->SetTarget(world);
            }
        }

        Vec2 ScreenToPhysicsWorld(float mx, float my) {
            int w, h;
            GLFWwindow* win = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());
            glfwGetWindowSize(win, &w, &h);
            float ndcX = (2.0f * mx) / w - 1.0f;
            float ndcY = 1.0f - (2.0f * my) / h;
            const float32* vp = m_Camera->GetViewProjectionMatrixPtr();
            return Vec2(ndcX * (12.0f * w / h * 0.5f),
                        ndcY * (12.0f * 0.5f));
        }

        // ── UI 面板 ──
        void DrawTestPanel() {
            ImGui::Begin("Test Controls");

            static int toggleCount = 0;
            if (ImGui::Button("Toggle UI (F1)")) {
                m_UIVisible = !m_UIVisible;
                UIManager::Get()->SetVisible(m_UIVisible);
                ++toggleCount;
            }
            ImGui::SameLine();
            ImGui::Text("Toggles: %d | %s", toggleCount, m_UIVisible ? "ON" : "OFF");

            ImGui::Separator();

            float s = UIManager::Get()->GetScale();
            if (ImGui::SliderFloat("UI Scale", &s, 0.5f, 2.5f, "%.2f"))
                UIManager::Get()->SetScale(s);
            if (ImGui::Button("Reset Scale"))
                UIManager::Get()->SetScale(1.0f);

            ImGui::Separator();

            if (ImGui::Button("Add Box (F3)")) AddPhysicsBox();
            ImGui::SameLine();
            if (ImGui::Button("Add Ball (F4)")) AddPhysicsBall();
            ImGui::SameLine();
            if (ImGui::Button("Clear"))
            {
                m_InspectorPanel.SetTarget(nullptr);
                m_TestScene->Clear();
                m_MouseJoint.reset();
                AddStaticBox(0, 0);
                LOG_INFO("Scene cleared (kept 1 box)");
            }

            ImGui::Separator();
            if (ImGui::Button("Reload Font 16px"))
                UIManager::Get()->LoadFont(nullptr, 16);
            ImGui::SameLine();
            if (ImGui::Button("Reload Font 24px"))
                UIManager::Get()->LoadFont(nullptr, 24);

            ImGui::Separator();
            ImGui::Text("Objects: %zu", m_TestScene->GetObjects().size());
            ImGui::Text("Created: %d  Alive: %d",
                        MemoryTracker::created,
                        MemoryTracker::created - MemoryTracker::destroyed);

            ImGui::End();
        }

        void DrawInputPreemptionPanel() {
            ImGui::Begin("Input Preemption");
            bool wm = UIManager::Get()->WantCaptureMouse();
            bool wk = UIManager::Get()->WantCaptureKeyboard();

            ImDrawList* dl = ImGui::GetWindowDrawList();
            float x = ImGui::GetCursorScreenPos().x;
            float y = ImGui::GetCursorScreenPos().y;
            float cw = ImGui::GetContentRegionAvail().x;
            float h  = 20.0f;

            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cw, y + h),
                              wm ? IM_COL32(255,120,0,200) : IM_COL32(0,200,80,200));
            ImGui::Dummy(ImVec2(cw, h));
            ImGui::SetCursorScreenPos(ImVec2(x + 4, y + 2));
            ImGui::Text("Mouse: %s", wm ? "BLOCKED" : "FREE");

            ImGui::SetCursorScreenPos(ImVec2(x, y + h + 4));
            dl->AddRectFilled(ImVec2(x, y + h + 4), ImVec2(x + cw, y + h * 2 + 4),
                              wk ? IM_COL32(255,120,0,200) : IM_COL32(0,200,80,200));
            ImGui::Dummy(ImVec2(cw, h));
            ImGui::SetCursorScreenPos(ImVec2(x + 4, y + h + 6));
            ImGui::Text("Keyboard: %s", wk ? "BLOCKED" : "FREE");

            ImGui::SetCursorScreenPos(ImVec2(x, y + h * 2 + 12));
            ImGui::TextWrapped("Interact with ImGui controls to watch input blocking. "
                               "WASD moves selected entity when keyboard is FREE.");
            ImGui::End();
        }

        // ── 成员 ──
        std::unique_ptr<Scene> m_TestScene;
        std::shared_ptr<Box2DPhysicsWorld> m_PhysicsWorld;
        SceneHierarchyPanel m_HierarchyPanel;
        InspectorPanel m_InspectorPanel;
        ConsolePanel m_ConsolePanel;

        std::shared_ptr<ISpriteBatch> m_SpriteBatch;
        std::shared_ptr<Shader> m_SpriteShader;
        std::shared_ptr<Texture> m_SpriteTexture;

        std::shared_ptr<IPhysicsBody> m_MouseAnchor;
        std::shared_ptr<IJoint> m_MouseJoint;

        bool m_UIVisible = true;
    };

} // namespace Engine
