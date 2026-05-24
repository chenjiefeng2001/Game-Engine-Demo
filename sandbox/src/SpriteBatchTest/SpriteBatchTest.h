#pragma once
#include <Engine/Core/IWindow.h>
#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/Renderer/SpriteBatch.h>
#include <Engine/Core/RenderResources/Shader.h>
#include <Engine/Core/RenderResources/Texture.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/Core/Renderer/OrthographicCamera.h>
#include <Engine/Types.h>
#include <memory>
#include <vector>

namespace Engine {

    struct TestSprite {
        float32 baseX, baseY;
        float32 phase;
        float32 hue;
    };

    class SpriteBatchTest {
    public:
        SpriteBatchTest(IGraphicsFactory& factory);
        ~SpriteBatchTest();
        void Run();

    private:
        void Update(float32 dt);
        void Render();

        IGraphicsFactory& m_Factory;
        TextureManager m_TextureManager;
        std::unique_ptr<IWindow> m_Window;
        std::unique_ptr<OrthographicCamera> m_Camera;

        std::shared_ptr<ISpriteBatch> m_SpriteBatch;
        std::shared_ptr<Shader> m_BatchShader;
        std::shared_ptr<Texture> m_Texture;

        std::vector<TestSprite> m_Sprites;
        static constexpr int32 GRID_SIZE = 20;
        float32 m_GlobalTime = 0.0f;
        float32 m_LastFrameTime = 0.0f;

        float32 m_FpsAccumulator = 0.0f;
        int32   m_FrameCount = 0;
    };

}