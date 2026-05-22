#include "InputTestApp.h"
#include <Engine/Platform/PlatformUtils.h>
#include <Engine/Platform/GlfwInput.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/IWindow.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <iomanip>

namespace Engine {

    InputTestApp::InputTestApp(IGraphicsFactory& factory)
        : m_Factory(factory)
    {
        // ── 1. 创建窗口 ──
        m_Window = m_Factory.CreateWindow(m_WindowWidth, m_WindowHeight, "Input Manager Demo");

        // ── 2. 通过 InputManager 初始化输入系统 ──
        auto* nativeWin = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());
        m_InputManager.Init(nativeWin);

        // ── 3. 创建正交相机 ──
        m_Camera = std::make_unique<OrthographicCamera>(-3.0f, 3.0f, -3.0f, 3.0f);

        // ── 4. 创建渲染资源 ──
        auto* context = m_Window->GetContext();
        m_SpriteBatch = m_Factory.CreateSpriteBatch(*context);
        m_BatchShader = m_Factory.CreateShader(
            "assets/shaders/sprite_batch.vert",
            "assets/shaders/sprite_batch.frag"
        );
        m_Texture = m_Factory.CreateTexture("assets/textures/test.png");

        // ── 5. 注册输入动作映射 ──
        auto& actionRed = m_InputManager.CreateAction("SetRed");
        actionRed.AddBinding(KeyBinding::FromKey(KeyCode::R));

        auto& actionGreen = m_InputManager.CreateAction("SetGreen");
        actionGreen.AddBinding(KeyBinding::FromKey(KeyCode::G));

        auto& actionBlue = m_InputManager.CreateAction("SetBlue");
        actionBlue.AddBinding(KeyBinding::FromKey(KeyCode::B));

        auto& actionYellow = m_InputManager.CreateAction("SetYellow");
        actionYellow.AddBinding(KeyBinding::FromKey(KeyCode::Space));

        // 鼠标侧键绑定
        auto& actionSideBack = m_InputManager.CreateAction("SideBack");
        actionSideBack.AddBinding(KeyBinding::FromMouse(MouseCode::Button4));

        auto& actionSideForward = m_InputManager.CreateAction("SideForward");
        actionSideForward.AddBinding(KeyBinding::FromMouse(MouseCode::Button5));

        // 退出
        auto& actionExit = m_InputManager.CreateAction("Exit");
        actionExit.AddBinding(KeyBinding::FromKey(KeyCode::Escape));

        // WASD 方向动作
        auto& actionMoveUp = m_InputManager.CreateAction("MoveUp");
        actionMoveUp.AddBinding(KeyBinding::FromKey(KeyCode::W));

        auto& actionMoveDown = m_InputManager.CreateAction("MoveDown");
        actionMoveDown.AddBinding(KeyBinding::FromKey(KeyCode::S));

        auto& actionMoveLeft = m_InputManager.CreateAction("MoveLeft");
        actionMoveLeft.AddBinding(KeyBinding::FromKey(KeyCode::A));

        auto& actionMoveRight = m_InputManager.CreateAction("MoveRight");
        actionMoveRight.AddBinding(KeyBinding::FromKey(KeyCode::D));

        // ── 6. 注册事件回调 ──
        actionRed.OnPressed([this]() {
            m_ClearColorR = 0.8f; m_ClearColorG = 0.2f; m_ClearColorB = 0.2f;
            std::cout << "[Action:SetRed] 背景变红" << std::endl;
            });

        actionGreen.OnPressed([this]() {
            m_ClearColorR = 0.2f; m_ClearColorG = 0.8f; m_ClearColorB = 0.2f;
            std::cout << "[Action:SetGreen] 背景变绿" << std::endl;
            });

        actionBlue.OnPressed([this]() {
            m_ClearColorR = 0.2f; m_ClearColorG = 0.2f; m_ClearColorB = 0.8f;
            std::cout << "[Action:SetBlue] 背景变蓝" << std::endl;
            });

        actionYellow.OnPressed([this]() {
            m_ClearColorR = 0.8f; m_ClearColorG = 0.8f; m_ClearColorB = 0.2f;
            std::cout << "[Action:SetYellow] 背景变黄" << std::endl;
            });

        actionSideBack.OnPressed([]() {
            std::cout << "[Action:SideBack] 鼠标侧键4（后退）按下" << std::endl;
            });

        actionSideForward.OnPressed([]() {
            std::cout << "[Action:SideForward] 鼠标侧键5（前进）按下" << std::endl;
            });

        actionExit.OnPressed([this]() {
            std::cout << "[Action:Exit] 退出程序" << std::endl;
            auto* win = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());
            glfwSetWindowShouldClose(win, GLFW_TRUE);
            });

        actionMoveUp.OnPressed([]() {
            std::cout << "[Action:MoveUp] 开始移动" << std::endl;
            });
        actionMoveUp.OnReleased([]() {
            std::cout << "[Action:MoveUp] 停止移动" << std::endl;
            });

        actionMoveDown.OnPressed([]() {
            std::cout << "[Action:MoveDown] 开始移动" << std::endl;
            });
        actionMoveDown.OnReleased([]() {
            std::cout << "[Action:MoveDown] 停止移动" << std::endl;
            });

        std::cout << "==========================================" << std::endl;
        std::cout << "  Input Manager Demo Started!" << std::endl;
        std::cout << "  按键映射测试:" << std::endl;
        std::cout << "    R          -> 红色背景" << std::endl;
        std::cout << "    G          -> 绿色背景" << std::endl;
        std::cout << "    B          -> 蓝色背景" << std::endl;
        std::cout << "    Space      -> 黄色背景" << std::endl;
        std::cout << "    W/S/A/D    -> 方向 (Press/Release 回调)" << std::endl;
        std::cout << "    Escape     -> 退出" << std::endl;
        std::cout << "    Mouse4/5   -> 侧键测试" << std::endl;
        std::cout << "    Shift+鼠标 -> 屏幕->世界坐标测试" << std::endl;
        std::cout << "==========================================" << std::endl;
    }

    InputTestApp::~InputTestApp() {
        m_InputManager.Shutdown();
    }

    void InputTestApp::Update(float dt) {
        // ── 方式1: 通过 InputManager 动作映射查询 ──
        if (m_InputManager.IsActionPressed("SetRed")) {
            std::cout << "  (也可以通过 InputManager::IsActionPressed 查询)" << std::endl;
        }

        if (m_InputManager.IsActionDown("MoveUp"))
            std::cout << "[Action:MoveDown] W 按住中" << std::endl;
        if (m_InputManager.IsActionDown("MoveDown"))
            std::cout << "[Action:MoveDown] S 按住中" << std::endl;
        if (m_InputManager.IsActionDown("MoveLeft"))
            std::cout << "[Action:MoveDown] A 按住中" << std::endl;
        if (m_InputManager.IsActionDown("MoveRight"))
            std::cout << "[Action:MoveDown] D 按住中" << std::endl;

        // ── 方式2: 直接通过 InputManager 查询原始输入 ──
        float dx = m_InputManager.GetMouseDeltaX();
        float dy = m_InputManager.GetMouseDeltaY();
        if (dx != 0.0f || dy != 0.0f) {
            std::cout << "[Mouse] delta: (" << dx << ", " << dy << ")"
                << "  pos: (" << m_InputManager.GetMouseX()
                << ", " << m_InputManager.GetMouseY() << ")" << std::endl;
        }

        float scroll = m_InputManager.GetScrollDelta();
        if (scroll != 0.0f)
            std::cout << "[Scroll] " << (scroll > 0 ? "向上滚" : "向下滚")
            << " 增量: " << scroll << std::endl;

        // 直接查询鼠标侧键
        if (m_InputManager.IsMousePressed(MouseCode::Button4))
            std::cout << "[Direct:Mouse4] 侧键4 按下" << std::endl;
        if (m_InputManager.IsMousePressed(MouseCode::Button5))
            std::cout << "[Direct:Mouse5] 侧键5 按下" << std::endl;

        // 鼠标中键
        if (m_InputManager.IsMousePressed(MouseCode::ButtonMiddle))
            std::cout << "[Direct:MouseMiddle] 中键按下" << std::endl;

        // ── 屏幕->世界坐标转换测试 ──
        static bool wasShiftDown = false;
        bool isShiftDown = m_InputManager.IsKeyDown(KeyCode::LeftShift);
        if (isShiftDown) {
            m_WorldMousePos = m_InputManager.ScreenToWorld(
                *m_Camera,
                static_cast<float>(m_WindowWidth),
                static_cast<float>(m_WindowHeight)
            );
            std::cout << "[Screen->World] 屏幕("
                << m_InputManager.GetMouseX() << ", "
                << m_InputManager.GetMouseY() << ") -> 世界("
                << m_WorldMousePos.x << ", " << m_WorldMousePos.y << ")"
                << std::endl;
        }
        if (!isShiftDown && wasShiftDown) {
            std::cout << "[Screen->World] 停止显示（松开 Shift）" << std::endl;
        }
        wasShiftDown = isShiftDown;

        // ── 测试释放事件 ──
        if (m_InputManager.IsKeyReleased(KeyCode::LeftShift))
            std::cout << "[Released] LeftShift 释放" << std::endl;

        // ── 左键轮询 ──
        if (m_InputManager.IsMousePressed(MouseCode::ButtonLeft))
            std::cout << "[Direct:MouseLeft] 左键点击（轮询）" << std::endl;
    }

    void InputTestApp::Render() {
        auto context = m_Window->GetContext();
        context->ClearColor(m_ClearColorR, m_ClearColorG, m_ClearColorB, 1.0f);

        // ── 渲染 10000 个精灵 ──
        m_BatchShader->Bind();
        m_BatchShader->SetMat4("u_ViewProjection",
            m_Camera->GetViewProjectionMatrixPtr());
        m_SpriteBatch->Begin(m_Texture);

        bool showCursor = m_InputManager.IsKeyDown(KeyCode::LeftShift);

        for (int i = 0; i < 10000; i++) {
            SpriteData sprite;
            sprite.transform.x = (i % 100) * 0.06f - 3.0f;
            sprite.transform.y = (i / 100) * 0.06f - 3.0f;
            sprite.transform.scaleX = 0.05f;
            sprite.transform.scaleY = 0.05f;

            if (showCursor) {
                float dx = sprite.transform.x - m_WorldMousePos.x;
                float dy = sprite.transform.y - m_WorldMousePos.y;
                float dist = dx * dx + dy * dy;
                if (dist < 0.1f) {
                    sprite.colorR = 1.0f;
                    sprite.colorG = 1.0f;
                    sprite.colorB = 1.0f;
                }
                else {
                    sprite.colorR = 1.0f;
                    sprite.colorG = (float)(i % 256) / 255.0f;
                    sprite.colorB = (float)((i * 3) % 256) / 255.0f;
                }
            }
            else {
                sprite.colorR = 1.0f;
                sprite.colorG = (float)(i % 256) / 255.0f;
                sprite.colorB = (float)((i * 3) % 256) / 255.0f;
            }
            m_SpriteBatch->Draw(sprite);
        }

        m_SpriteBatch->End();
    }

    void InputTestApp::Run() {
        m_LastFrameTime = Time::GetTime();

        while (!m_Window->ShouldClose()) {
            float time = Time::GetTime();
            float dt = time - m_LastFrameTime;
            m_LastFrameTime = time;
            if (dt > 0.25f) dt = 0.25f;

            // ── 主循环顺序（关键！） ──
            glfwPollEvents();             
            m_InputManager.OnUpdate();     
            Update(dt);                    
            Render();                     

            auto ctx = m_Window->GetContext();
            ctx->SwapBuffers();

            // ── FPS ──
            m_FpsAccumulator += dt;
            m_FrameCount++;
            if (m_FpsAccumulator >= 1.0f) {
                std::cout << "[FPS] " << m_FrameCount << std::endl;
                m_FpsAccumulator = 0.0f;
                m_FrameCount = 0;
            }
        }
    }

}
