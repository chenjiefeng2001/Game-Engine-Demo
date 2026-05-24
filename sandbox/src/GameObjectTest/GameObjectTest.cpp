#include "GameObjectTest.h"
#include <Engine/Platform/PlatformUtils.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/IWindow.h>
#include <glm/glm.hpp>
#include <iostream>
#include <cmath>


namespace Engine {

    Orbiter::Orbiter(std::string name)
        : GameObject(std::move(name))
    {
        GetSprite().SetColor(0.2f, 0.6f, 1.0f, 1.0f);
    }

    void Orbiter::Update(float32 dt) {
        m_Time += dt;

        // 公转：绕父节点旋转
        float32 angle = m_Time * m_OrbitSpeed;
        float32 x = std::cos(angle) * m_OrbitRadius;
        float32 y = std::sin(angle) * m_OrbitRadius;
        GetTransform().SetPosition(x, y, 0.0f);

        // 自转
        GetTransform().SetRotation(0.0f, 0.0f, m_Time * m_SelfRotateSpeed);

        // 缩放脉冲
        float32 pulse = 0.6f + 0.3f * std::sin(m_Time * m_PulseSpeed);
        GetTransform().SetScale(pulse);

        // 颜色随时间渐变
        float32 hue = std::fmod(m_Time * 0.15f, 1.0f);
        // 简单的 HSV->RGB 转换
        float32 r, g, b;
        int32 hi = static_cast<int32>(hue * 6.0f);
        float32 f = hue * 6.0f - hi;
        float32 p_val = 1.0f * (1.0f - 0.8f);
        float32 q = 1.0f * (1.0f - f * 0.8f);
        float32 t = 1.0f * (1.0f - (1.0f - f) * 0.8f);
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

    void PlayerObject::Update(float32 dt) {
        // WASD 移动（使用 glm 做向量运算，再转成 Engine 类型）
        glm::vec3 move(0.0f);
        if (Input::IsKeyDown(KeyCode::W))      move.y += 1.0f;
        if (Input::IsKeyDown(KeyCode::S))      move.y -= 1.0f;
        if (Input::IsKeyDown(KeyCode::A))      move.x -= 1.0f;
        if (Input::IsKeyDown(KeyCode::D))      move.x += 1.0f;

        if (glm::length(move) > 0.0f) {
            move = glm::normalize(move) * m_MoveSpeed * dt;
            GetTransform().Translate(Vec3(move.x, move.y, move.z));
        }

        // Q / E 旋转
        if (Input::IsKeyDown(KeyCode::Q))
            GetTransform().Rotate(Vec3(0.0f, 0.0f, -2.0f * dt));
        if (Input::IsKeyDown(KeyCode::E))
            GetTransform().Rotate(Vec3(0.0f, 0.0f,  2.0f * dt));

        // R / F 缩放
        if (Input::IsKeyDown(KeyCode::R)) {
            const Vec3& s = GetTransform().GetScale();
            GetTransform().SetScale(Vec3(s.x * (1.0f + 1.0f * dt),
                                         s.y * (1.0f + 1.0f * dt),
                                         s.z * (1.0f + 1.0f * dt)));
        }
        if (Input::IsKeyDown(KeyCode::F)) {
            const Vec3& s = GetTransform().GetScale();
            GetTransform().SetScale(Vec3(s.x * (1.0f - 1.0f * dt),
                                         s.y * (1.0f - 1.0f * dt),
                                         s.z * (1.0f - 1.0f * dt)));
        }

        // 子对象由基类 GameObject::Update 递归更新
        GameObject::Update(dt);
    }


    PulseSprite::PulseSprite(std::string name)
        : GameObject(std::move(name))
    {
        GetSprite().SetColor(0.3f, 1.0f, 0.4f, 1.0f);
    }

    void PulseSprite::Update(float32 dt) {
        m_Time += dt;

        // 呼吸式缩放
        float32 t = 0.5f + 0.5f * std::sin(m_Time * m_PulseSpeed);
        float32 s = m_MinScale + (m_MaxScale - m_MinScale) * t;
        GetTransform().SetScale(s);

        // 颜色交替
        float32 colorPhase = 0.5f + 0.5f * std::sin(m_Time * m_PulseSpeed * 0.7f);
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
        , m_TextureManager(factory)
        , m_SceneRenderer(factory, m_TextureManager)
    {

        m_Window = m_Factory.CreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT,
            "GameObject Test");

        m_InputManager.Init(m_Window.get());

        float32 aspect = static_cast<float32>(WINDOW_WIDTH) / WINDOW_HEIGHT;
        float32 viewHeight = 10.0f;
        float32 viewWidth = viewHeight * aspect;
        m_Camera = std::make_unique<OrthographicCamera>(
            -viewWidth / 2, viewWidth / 2,
            -viewHeight / 2, viewHeight / 2
        );

        // ── 4. 创建渲染资源 ──
        auto* ctx = m_Window->GetContext();

        // 初始化 SceneRenderer（传入渲染上下文用于创建 SpriteBatch）
        m_SceneRenderer.SetRenderContext(*ctx);
        m_SceneRenderer.SetCamera(m_Camera.get());

        auto batchShader = m_Factory.CreateShader(
            "assets/shaders/sprite_batch.vert",
            "assets/shaders/sprite_batch.frag"
        );
        m_SceneRenderer.SetShader(batchShader);

        m_Texture = m_TextureManager.Load("assets/textures/test.png");

        // ── 注册输入动作 ──
        // Escape 按下时标记退出，不直接调用 GLFW API
        auto& actionExit = m_InputManager.CreateAction("Exit");
        actionExit.AddBinding(KeyBinding::FromKey(KeyCode::Escape));

        actionExit.OnPressed([this]() {
            m_ShouldClose = true;
            });

        // ── 6. 构建游戏对象层次 ──
        {
            // ── 根对象：居中的静态精灵 ──
            auto root = std::make_shared<GameObject>("Root");
            root->GetSprite().SetTexture(m_Texture);
            root->GetSprite().SetColor(0.6f, 0.6f, 0.8f, 1.0f);
            root->GetSprite().SetUV(0.0f, 0.0f, 1.0f, 1.0f);
            root->GetTransform().SetScale(1.2f);

            // ── 子对象：绕根公转的卫星 ──
            auto orbiter = std::make_shared<Orbiter>("Orbiter");
            orbiter->GetSprite().SetTexture(m_Texture);
            orbiter->SetOrbitSpeed(1.2f);
            orbiter->SetOrbitRadius(3.0f);
            orbiter->SetSelfRotateSpeed(2.5f);
            orbiter->SetPulseSpeed(2.0f);
            root->AddChild(orbiter);

            // ── 孙对象：卫星的卫星（二级层级） ──
            auto moon = std::make_shared<Orbiter>("Moon");
            moon->GetSprite().SetTexture(m_Texture);
            moon->SetOrbitSpeed(3.0f);
            moon->SetOrbitRadius(1.0f);
            moon->SetSelfRotateSpeed(4.0f);
            moon->SetPulseSpeed(3.5f);
            // 修改 moon 的基准颜色，使其与父卫星区分
            moon->GetSprite().SetColor(1.0f, 0.4f, 0.4f, 1.0f);
            orbiter->AddChild(moon);

            m_Scene.AddObject(std::move(root));
        }

        {
            // ── 玩家控制对象 ──
            auto player = std::make_shared<PlayerObject>("Player");
            player->GetSprite().SetTexture(m_Texture);
            player->GetTransform().SetPosition(-3.0f, -2.0f, 0.0f);
            m_PlayerObj = player.get();
            m_Scene.AddObject(std::move(player));
        }

        {
            // ── 脉冲精灵（呼吸效果） ──
            auto pulser = std::make_shared<PulseSprite>("Pulser");
            pulser->GetSprite().SetTexture(m_Texture);
            pulser->GetTransform().SetPosition(4.0f, 2.0f, 0.0f);
            pulser->SetPulseSpeed(1.8f);
            pulser->SetMinScale(0.3f);
            pulser->SetMaxScale(1.2f);
            m_Scene.AddObject(std::move(pulser));
        }

        {
            // ── 位于层次树深处的一个对象 —— 演示 FindChild ──
            auto container = std::make_shared<GameObject>("Container");
            container->GetTransform().SetPosition(0.0f, 3.5f, 0.0f);
            container->GetSprite().SetColor(0.5f, 0.5f, 0.5f, 0.6f);

            auto inner = std::make_shared<PulseSprite>("InnerPulser");
            inner->GetSprite().SetTexture(m_Texture);
            inner->SetPulseSpeed(2.5f);
            inner->SetMinScale(0.4f);
            inner->SetMaxScale(0.9f);
            container->AddChild(inner);

            m_Scene.AddObject(std::move(container));

            // 演示 Scene::FindObject 查找深层对象
            auto* found = m_Scene.FindObject("InnerPulser");
            if (found) {
                std::cout << "[FindObject] 找到 \""
                    << found->GetName() << "\"" << std::endl;
            }
        }

        // ── 调用所有对象的 OnCreate（Scene 自动递归） ──
        m_Scene.OnCreate();

        // ── 输出帮助信息 ──
        PrintHelp();
    }

    GameObjectTest::~GameObjectTest() {
        // Scene 析构时会自动调用 OnDestroy
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


    void GameObjectTest::Update(float32 dt) {
        m_Scene.Update(dt);
    }

    void GameObjectTest::Render() {
        auto ctx = m_Window->GetContext();
        ctx->ClearColor(m_ClearColorR, m_ClearColorG, m_ClearColorB, 1.0f);


        auto defaultTex = m_TextureManager.Load("assets/textures/default.png");
        m_SceneRenderer.Render(m_Scene, defaultTex);
    }


    void GameObjectTest::Run() {
        m_LastFrameTime = Time::GetTime();

        while (!m_Window->ShouldClose() && !m_ShouldClose) {
            float32 time = Time::GetTime();
            float32 dt = time - m_LastFrameTime;
            m_LastFrameTime = time;
            if (dt > 0.25f) dt = 0.25f;


            m_InputManager.OnUpdate();

            Update(dt);
            Render();

            m_Window->OnUpdate();

            m_FpsAccumulator += dt;
            m_FrameCount++;
            if (m_FpsAccumulator >= 1.0f) {
                std::cout << "[FPS] " << m_FrameCount
                    << " | Objects: " << (m_Scene.GetObjects().size() + 4)
                    << std::endl;
                m_FpsAccumulator = 0.0f;
                m_FrameCount = 0;
            }
        }
    }

} // namespace Engine
