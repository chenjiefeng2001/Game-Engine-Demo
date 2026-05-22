#include "InputTestApp.h"
#include <Engine/Platform/PlatformUtils.h>
#include <Engine/Platform/GlfwInput.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/IWindow.h>
#include <GLFW/glfw3.h>
#include <iostream>

namespace Engine {

    InputTestApp::InputTestApp(IGraphicsFactory& factory)
        : m_Factory(factory)
    {
        // ── 1. 创建窗口 ──
        m_Window = m_Factory.CreateWindow(800, 600, "Sprite Batch Demo");

        // ── 2. 初始化输入系统 ──
        auto* nativeWin = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());
        Input::Init(std::make_unique<GlfwInput>(nativeWin));

        // ── 3. 创建摄像机 ──
        m_Camera = std::make_unique<OrthographicCamera>(-3.0f, 3.0f, -3.0f, 3.0f);

        // ── 4. 创建批处理系统 ──
        auto* context = m_Window->GetContext();
        m_SpriteBatch = m_Factory.CreateSpriteBatch(*context);
        m_BatchShader = m_Factory.CreateShader(
            "assets/shaders/sprite_batch.vert",
            "assets/shaders/sprite_batch.frag"
        );
        m_Texture = m_Factory.CreateTexture("assets/textures/test.png");

        std::cout << "==========================================" << std::endl;
        std::cout << "  Sprite Batch Demo Started!" << std::endl;
        std::cout << "  Rendering 10000 sprites in 1 Draw Call" << std::endl;
        std::cout << "==========================================" << std::endl;
    }

    InputTestApp::~InputTestApp() {
        Input::Shutdown();
    }

    void InputTestApp::Update(float dt) {
        // ─── IsKeyPressed：本帧刚按下（事件风格）───

        if (Input::IsKeyPressed(KeyCode::R)) {
            m_ClearColorR = 0.8f; m_ClearColorG = 0.2f; m_ClearColorB = 0.2f;
            std::cout << "[Pressed] R → Red" << std::endl;
        }
        if (Input::IsKeyPressed(KeyCode::G)) {
            m_ClearColorR = 0.2f; m_ClearColorG = 0.8f; m_ClearColorB = 0.2f;
            std::cout << "[Pressed] G → Green" << std::endl;
        }
        if (Input::IsKeyPressed(KeyCode::B)) {
            m_ClearColorR = 0.2f; m_ClearColorG = 0.2f; m_ClearColorB = 0.8f;
            std::cout << "[Pressed] B → Blue" << std::endl;
        }
        if (Input::IsKeyPressed(KeyCode::Space)) {
            m_ClearColorR = 0.8f; m_ClearColorG = 0.8f; m_ClearColorB = 0.2f;
            std::cout << "[Pressed] Space → Yellow" << std::endl;
        }
        if (Input::IsKeyPressed(KeyCode::Escape)) {
            std::cout << "[Pressed] Escape — setting close flag" << std::endl;
            auto* win = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        }

        // ─── IsKeyDown：当前按住（轮询风格），每帧刷屏 ───

        if (Input::IsKeyDown(KeyCode::W))
            std::cout << "[Down] W is held" << std::endl;
        if (Input::IsKeyDown(KeyCode::S))
            std::cout << "[Down] S is held" << std::endl;
        if (Input::IsKeyDown(KeyCode::A))
            std::cout << "[Down] A is held" << std::endl;
        if (Input::IsKeyDown(KeyCode::D))
            std::cout << "[Down] D is held" << std::endl;

        // ─── 鼠标测试 ───

        float dx = Input::GetMouseDeltaX();
        float dy = Input::GetMouseDeltaY();
        if (dx != 0.0f || dy != 0.0f)
            std::cout << "[Mouse] delta: (" << dx << ", " << dy << ")" << std::endl;

        float scroll = Input::GetScrollDelta();
        if (scroll != 0.0f)
            std::cout << "[Scroll] " << scroll << std::endl;

        // ─── IsKeyReleased 测试 ───
        if (Input::IsKeyReleased(KeyCode::LeftShift))
            std::cout << "[Released] LeftShift" << std::endl;
    }

    void InputTestApp::Render() {
        auto context = m_Window->GetContext();
        context->ClearColor(m_ClearColorR, m_ClearColorG, m_ClearColorB, 1.0f);

        // ── 批处理渲染 10000 个精灵 ──
        m_BatchShader->Bind();
        m_BatchShader->SetMat4("u_ViewProjection",
            m_Camera->GetViewProjectionMatrixPtr());
        m_SpriteBatch->Begin(m_Texture);

        for (int i = 0; i < 10000; i++) {
            SpriteData sprite;
            sprite.transform.x = (i % 100) * 0.06f - 3.0f;
            sprite.transform.y = (i / 100) * 0.06f - 3.0f;
            sprite.transform.scaleX = 0.05f;
            sprite.transform.scaleY = 0.05f;
            sprite.colorR = 1.0f;
            sprite.colorG = (float)(i % 256) / 255.0f;
            sprite.colorB = (float)((i * 3) % 256) / 255.0f;
            // UV 默认 (0,0,1,1) = 完整纹理
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

            // ── 主循环顺序（关键！）──
            glfwPollEvents();              // ① 收集事件 → 触发回调 → 填充 Input 状态
            Input::Get()->OnUpdate();      // ② 锁定本帧状态（清除 changed 标志）
            Update(dt);                    // ③ 逻辑更新（查询 Input）
            Render();                      // ④ 渲染

            auto ctx = m_Window->GetContext();
            ctx->SwapBuffers();            // ⑤ 交换缓冲区（而不是 m_Window->OnUpdate）

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