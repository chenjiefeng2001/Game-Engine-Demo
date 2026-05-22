#pragma once
#include <Engine/Core/IWindow.h>
#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/Renderer/SpriteBatch.h>
#include <Engine/Core/RenderResources/Shader.h>
#include <Engine/Core/RenderResources/Texture.h>
#include <Engine/Core/Renderer/OrthographicCamera.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/InputManager.h>
#include <memory>
#include <glm/glm.hpp>

namespace Engine {

    class InputTestApp {
    public:
        InputTestApp(IGraphicsFactory& factory);
        ~InputTestApp();
        void Run();

    private:
        void Update(float dt);
        void Render();
        void PrintInputState();

        IGraphicsFactory& m_Factory;
        std::unique_ptr<IWindow> m_Window;

        // ── 输入管理器 ──
        InputManager m_InputManager;

        // ── 渲染资源 ──
        std::shared_ptr<ISpriteBatch> m_SpriteBatch;
        std::shared_ptr<Shader> m_BatchShader;
        std::shared_ptr<Texture> m_Texture;
        std::unique_ptr<OrthographicCamera> m_Camera;

        // ── 背景色 ──
        float m_ClearColorR = 0.2f;
        float m_ClearColorG = 0.2f;
        float m_ClearColorB = 0.2f;

        // ── 鼠标世界坐标跟踪 ──
        glm::vec2 m_WorldMousePos{ 0.0f, 0.0f };

        // ── FPS ──
        float m_LastFrameTime = 0.0f;
        float m_FpsAccumulator = 0.0f;
        int   m_FrameCount = 0;
        int   m_WindowWidth = 800;
        int   m_WindowHeight = 600;
    };

}