#include "GameObjectTest.h"
#include <Engine/Platform/PlatformUtils.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/IWindow.h>
#include <iostream>
#include <cmath>


struct GLFWwindow;

namespace Engine {

    Orbiter::Orbiter(std::string name)
        : GameObject(std::move(name))
    {
        GetSprite().SetColor(0.2f, 0.6f, 1.0f, 1.0f);
    }

    void Orbiter::Update(float dt) {
        m_Time += dt;

        // 公转：绕父节点旋转
        float angle = m_Time * m_OrbitSpeed;
        float x = std::cos(angle) * m_OrbitRadius;
        float y = std::sin(angle) * m_OrbitRadius;
        GetTransform().SetPosition(x, y, 0.0f);

        // 自转
        GetTransform().SetRotation(0.0f, 0.0f, m_Time * m_SelfRotateSpeed);

        // 缩放脉冲
        float pulse = 0.6f + 0.3f * std::sin(m_Time * m_PulseSpeed);
        GetTransform().SetScale(pulse);

        // 颜色随时间渐变
        float hue = std::fmod(m_Time * 0.15f, 1.0f);
        // 简单的 HSV->RGB 转换
        float r, g, b;
        int hi = static_cast<int>(hue * 6.0f);
        float f = hue * 6.0f - hi;
        float p_val = 1.0f * (1.0f - 0.8f);
        float q = 1.0f * (1.0f - f * 0.8f);
        float t = 1.0f * (1.0f - (1.0f - f) * 0.8f);
        switch (hi % 6) {
            case 0: r = 1.0f; g = t; b = p_val; break;
            case 1: r = q; g = 1.0f; b = p_val; break;
            case 2: r = p_val; g = 1.0f; b = t; break;
            case 3: r = p_val; g = q; b = 1.0f; break;
            case 4: r = t; g = p_val; b = 1.0f; break;
            case 5: r = 1.0f; g = p_val; b = q; break;
            default: r = 1.0f; g = 1.0f; b = 1.0f; break;
        }
        GetSprite().SetColor(r, g, b, 1.0f);

        // 子对象由基类 GameObject::Update 递归更新
        GameObject::Update(dt);
    }


    PlayerObject::PlayerObject(std::string name)
        : GameObject(std::move(name))
    {
        GetSprite().SetColor(1.0f, 0.8f, 0.2f, 1.0f);
        GetTransform().SetScale(0.8f);
    }

    void PlayerObject::Update(float dt) {
        // WASD 移动
        glm::vec3 move(0.0f);
        if (Input::IsKeyDown(KeyCode::W))      move.y += 1.0f;
        if (Input::IsKeyDown(KeyCode::S))      move.y -= 1.0f;
        if (Input::IsKeyDown(KeyCode::A))      move.x -= 1.0f;
        if (Input::IsKeyDown(KeyCode::D))      move.x += 1.0f;

        if (glm::length(move) > 0.0f) {
            move = glm::normalize(move) * m_MoveSpeed * dt;
            GetTransform().Translate(move);
        }

        // Q / E 旋转
        if (Input::IsKeyDown(KeyCode::Q))
            GetTransform().Rotate({ 0.0f, 0.0f, -2.0f * dt });
        if (Input::IsKeyDown(KeyCode::E))
            GetTransform().Rotate({ 0.0f, 0.0f,  2.0f * dt });

        // R / F 缩放
        if (Input::IsKeyDown(KeyCode::R)) {
            auto s = GetTransform().GetScale();
            GetTransform().SetScale(s * (1.0f + 1.0f * dt));
        }
        if (Input::IsKeyDown(KeyCode::F)) {
            auto s = GetTransform().GetScale();
            GetTransform().SetScale(s * (1.0f - 1.0f * dt));
        }

        // 子对象由基类 GameObject::Update 递归更新
        GameObject::Update(dt);
    }


    PulseSprite::PulseSprite(std::string name)
        : GameObject(std::move(name))
    {
        GetSprite().SetColor(0.3f, 1.0f, 0.4f, 1.0f);
    }

    void PulseSprite::Update(float dt) {
        m_Time += dt;

        // 呼吸式缩放
        float t = 0.5f + 0.5f * std::sin(m_Time * m_PulseSpeed);
        float s = m_MinScale + (m_MaxScale - m_MinScale) * t;
        GetTransform().SetScale(s);

        // 颜色交替
        float colorPhase = 0.5f + 0.5f * std::sin(m_Time * m_PulseSpeed * 0.7f);
        GetSprite().SetColor(
            0.2f + 0.8f * colorPhase,
            0.8f - 0.5f * colorPhase,
            0.3f + 0.4f * std::sin(m_Time * 1.3f),
            1.0f
        );

        // 子对象由基类 GameObject::Update 递归更新
        GameObject::Update(dt);
    }
    GameObjectTest::GameObjectTest(IGraphicsFactory& factory)
        : m_Factory(factory)
    {
        // ── 1. 创建窗口 ──
        m_Window = m_Factory.CreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT,
            "GameObject Test");

        // ── 2. 初始化输入系统 ──
        auto* nativeWin = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());
        m_InputManager.Init(nativeWin);

        // ── 3. 创建相机 ──
        float aspect = static_cast<float>(WINDOW_WIDTH) / WINDOW_HEIGHT;
        float viewHeight = 10.0f;
        float viewWidth = viewHeight * aspect;
        m_Camera = std::make_unique<OrthographicCamera>(
            -viewWidth / 2, viewWidth / 2,
            -viewHeight / 2, viewHeight / 2
        );

        // ── 4. 创建渲染资源 ──
        auto* ctx = m_Window->GetContext();
        m_SpriteBatch = m_Factory.CreateSpriteBatch(*ctx);
        m_BatchShader = m_Factory.CreateShader(
            "assets/shaders/sprite_batch.vert",
            "assets/shaders/sprite_batch.frag"
        );
        m_Texture = m_Factory.CreateTexture("assets/textures/test.png");

        // ── 5. 注册输入动作 ──
        // Escape 按下时标记退出，不直接调用 GLFW API
        auto& actionExit = m_InputManager.CreateAction("Exit");
        actionExit.AddBinding(KeyBinding::FromKey(KeyCode::Escape));

        actionExit.OnPressed([this]() {
            m_ShouldClose = true;
            });

        // ── 6. 构建游戏对象层次 ──
        {
            // ── 6a. 根对象：居中的静态精灵 ──
            auto root = std::make_shared<GameObject>("Root");
            root->GetSprite().SetTexture(m_Texture);
            root->GetSprite().SetColor(0.6f, 0.6f, 0.8f, 1.0f);
            root->GetSprite().SetUV(0.0f, 0.0f, 1.0f, 1.0f);
            root->GetTransform().SetScale(1.2f);

            // ── 6b. 子对象：绕根公转的卫星 ──
            auto orbiter = std::make_shared<Orbiter>("Orbiter");
            orbiter->GetSprite().SetTexture(m_Texture);
            orbiter->SetOrbitSpeed(1.2f);
            orbiter->SetOrbitRadius(3.0f);
            orbiter->SetSelfRotateSpeed(2.5f);
            orbiter->SetPulseSpeed(2.0f);
            root->AddChild(orbiter);

            // ── 6c. 孙对象：卫星的卫星（二级层级） ──
            auto moon = std::make_shared<Orbiter>("Moon");
            moon->GetSprite().SetTexture(m_Texture);
            moon->SetOrbitSpeed(3.0f);
            moon->SetOrbitRadius(1.0f);
            moon->SetSelfRotateSpeed(4.0f);
            moon->SetPulseSpeed(3.5f);
            // 修改 moon 的基准颜色，使其与父卫星区分
            moon->GetSprite().SetColor(1.0f, 0.4f, 0.4f, 1.0f);
            orbiter->AddChild(moon);

            m_RootObjects.push_back(std::move(root));
        }

        {
            // ── 6d. 玩家控制对象 ──
            auto player = std::make_shared<PlayerObject>("Player");
            player->GetSprite().SetTexture(m_Texture);
            player->GetTransform().SetPosition(-3.0f, -2.0f, 0.0f);
            m_PlayerObj = player.get();
            m_RootObjects.push_back(std::move(player));
        }

        {
            // ── 6e. 脉冲精灵（呼吸效果） ──
            auto pulser = std::make_shared<PulseSprite>("Pulser");
            pulser->GetSprite().SetTexture(m_Texture);
            pulser->GetTransform().SetPosition(4.0f, 2.0f, 0.0f);
            pulser->SetPulseSpeed(1.8f);
            pulser->SetMinScale(0.3f);
            pulser->SetMaxScale(1.2f);
            m_RootObjects.push_back(std::move(pulser));
        }

        {
            // ── 6f. 位于层次树深处的一个对象 —— 演示 FindChild ──
            auto container = std::make_shared<GameObject>("Container");
            container->GetTransform().SetPosition(0.0f, 3.5f, 0.0f);
            container->GetSprite().SetColor(0.5f, 0.5f, 0.5f, 0.6f);

            auto inner = std::make_shared<PulseSprite>("InnerPulser");
            inner->GetSprite().SetTexture(m_Texture);
            inner->SetPulseSpeed(2.5f);
            inner->SetMinScale(0.4f);
            inner->SetMaxScale(0.9f);
            container->AddChild(inner);

            m_RootObjects.push_back(std::move(container));

            // 演示 FindChild 查找深层对象
            auto* found = m_RootObjects.back()->FindChild("InnerPulser");
            if (found) {
                std::cout << "[FindChild] 在 Container 下找到 \""
                    << found->GetName() << "\"" << std::endl;
            }
        }

        // ── 7. 调用所有对象的 OnCreate ──
        for (auto& obj : m_RootObjects) {
            obj->OnCreate();
            // 递归调用子对象的 OnCreate
            std::function<void(GameObject&)> recCreate =
                [&](GameObject& o) {
                    for (auto& c : o.GetChildren()) {
                        c->OnCreate();
                        recCreate(*c);
                    }
                };
            recCreate(*obj);
        }

        // ── 8. 输出帮助信息 ──
        PrintHelp();
    }

    GameObjectTest::~GameObjectTest() {
        // 调用所有对象的 OnDestroy
        for (auto& obj : m_RootObjects) {
            obj->OnDestroy();
            std::function<void(GameObject&)> recDestroy =
                [&](GameObject& o) {
                    for (auto& c : o.GetChildren()) {
                        c->OnDestroy();
                        recDestroy(*c);
                    }
                };
            recDestroy(*obj);
        }

        m_InputManager.Shutdown();
    }

    void GameObjectTest::PrintHelp() {
        std::cout << "==============================================" << std::endl;
        std::cout << "  GameObject Test" << std::endl;
        std::cout << "==============================================" << std::endl;
        std::cout << "  场景对象:" << std::endl;
        std::cout << "    [Root]       居中静态精灵 (父节点)" << std::endl;
        std::cout << "      [Orbiter]  绕父公转 + 自转 + 脉冲 (子节点)" << std::endl;
        std::cout << "        [Moon]   二级卫星 (孙节点)" << std::endl;
        std::cout << "    [Player]     WASDQE 控制 (黄色)" << std::endl;
        std::cout << "    [Pulser]     呼吸缩放 (绿色)" << std::endl;
        std::cout << "    [Container]" << std::endl;
        std::cout << "      [InnerPulser] 子脉冲精灵" << std::endl;
        std::cout << std::endl;
        std::cout << "  键盘操作:" << std::endl;
        std::cout << "    W/A/S/D    移动玩家" << std::endl;
        std::cout << "    Q/E        玩家旋转" << std::endl;
        std::cout << "    R/F        玩家放大/缩小" << std::endl;
        std::cout << "    Escape     退出" << std::endl;
        std::cout << "==============================================" << std::endl;
    }


    void GameObjectTest::Update(float dt) {
        // 更新所有根对象的 Update（递归）
        for (auto& obj : m_RootObjects) {
            obj->Update(dt);
        }
    }

    void GameObjectTest::Render() {
        auto ctx = m_Window->GetContext();
        ctx->ClearColor(m_ClearColorR, m_ClearColorG, m_ClearColorB, 1.0f);

        m_BatchShader->Bind();
        m_BatchShader->SetMat4("u_ViewProjection",
            m_Camera->GetViewProjectionMatrixPtr());

        // 开始批渲染
        m_SpriteBatch->Begin(m_Texture);

        // 递归提交所有对象的精灵
        std::function<void(GameObject&)> submitRecursive =
            [&](GameObject& obj) {
                // 提交当前对象的精灵
                obj.SubmitSprite(*m_SpriteBatch);

                // 递归提交子对象
                for (auto& child : obj.GetChildren()) {
                    submitRecursive(*child);
                }
            };

        for (auto& obj : m_RootObjects) {
            submitRecursive(*obj);
        }

        m_SpriteBatch->End();
    }


    void GameObjectTest::Run() {
        m_LastFrameTime = Time::GetTime();

        // 同时检查窗口原生关闭标志和内部退出标志
        while (!m_Window->ShouldClose() && !m_ShouldClose) {
            float time = Time::GetTime();
            float dt = time - m_LastFrameTime;
            m_LastFrameTime = time;
            if (dt > 0.25f) dt = 0.25f;

            // 先更新输入状态（由 InputManager 在构造函数中通过 GetNativeHandle 初始化）
            m_InputManager.OnUpdate();

            Update(dt);
            Render();

            // OnUpdate 内部调用 glfwPollEvents + SwapBuffers，
            // 通过 IWindow 接口屏蔽平台细节
            m_Window->OnUpdate();

            // FPS 统计
            m_FpsAccumulator += dt;
            m_FrameCount++;
            if (m_FpsAccumulator >= 1.0f) {
                std::cout << "[FPS] " << m_FrameCount
                    << " | Objects: " << (m_RootObjects.size() + 4)
                    << std::endl;
                m_FpsAccumulator = 0.0f;
                m_FrameCount = 0;
            }
        }
    }

} // namespace Engine
