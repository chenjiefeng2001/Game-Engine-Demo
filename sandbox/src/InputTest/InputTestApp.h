#pragma once
#include <Engine/Core/IWindow.h>
#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/Renderer/SpriteBatch.h>
#include <Engine/Core/RenderResources/Shader.h>
#include <Engine/Core/RenderResources/Texture.h>
#include <Engine/Core/Renderer/OrthographicCamera.h>
#include <Engine/Core/Input.h>
#include <memory>

namespace Engine {

    class InputTestApp {
    public:
        InputTestApp(IGraphicsFactory& factory);
        ~InputTestApp();
        void Run();

    private:
        void Update(float dt);
        void Render();

        IGraphicsFactory& m_Factory;
        std::unique_ptr<IWindow> m_Window;

        // ── 精灵批处理 ──
        std::shared_ptr<ISpriteBatch> m_SpriteBatch;
        std::shared_ptr<Shader> m_BatchShader;
        std::shared_ptr<Texture> m_Texture;
        std::unique_ptr<OrthographicCamera> m_Camera;

        // ── 清屏色 ──
        float m_ClearColorR = 0.2f;
        float m_ClearColorG = 0.2f;
        float m_ClearColorB = 0.2f;

        // ── 帧率 ──
        float m_LastFrameTime = 0.0f;
        float m_FpsAccumulator = 0.0f;
        int   m_FrameCount = 0;
    };

}