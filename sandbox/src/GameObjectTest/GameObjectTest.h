#pragma once

#include <Engine/Core/IWindow.h>
#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/Renderer/OrthographicCamera.h>
#include <Engine/Core/RenderResources/Texture.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/Core/RHI/SceneRenderer.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/InputManager.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/Scene/Scene.h>
#include <memory>
#include <vector>

namespace Engine {


    class Orbiter : public GameObject {
    public:
        explicit Orbiter(std::string name);
        void Update(float dt) override;

        void SetOrbitSpeed(float speed) { m_OrbitSpeed = speed; }
        void SetOrbitRadius(float radius) { m_OrbitRadius = radius; }
        void SetSelfRotateSpeed(float speed) { m_SelfRotateSpeed = speed; }
        void SetPulseSpeed(float speed) { m_PulseSpeed = speed; }

    private:
        float m_OrbitSpeed     = 1.5f;
        float m_OrbitRadius    = 1.5f;
        float m_SelfRotateSpeed = 2.0f;
        float m_PulseSpeed     = 3.0f;
        float m_Time           = 0.0f;
    };

    class PlayerObject : public GameObject {
    public:
        explicit PlayerObject(std::string name);
        void Update(float dt) override;

        void SetMoveSpeed(float speed) { m_MoveSpeed = speed; }
        float GetMoveSpeed() const { return m_MoveSpeed; }

    private:
        float m_MoveSpeed = 3.0f;
    };

    class PulseSprite : public GameObject {
    public:
        explicit PulseSprite(std::string name);
        void Update(float dt) override;

        void SetPulseSpeed(float speed) { m_PulseSpeed = speed; }
        void SetMinScale(float s) { m_MinScale = s; }
        void SetMaxScale(float s) { m_MaxScale = s; }

    private:
        float m_PulseSpeed = 2.0f;
        float m_MinScale   = 0.3f;
        float m_MaxScale   = 1.2f;
        float m_Time       = 0.0f;
    };

    class GameObjectTest {
    public:
        GameObjectTest(IGraphicsFactory& factory);
        ~GameObjectTest();
        void Run();

    private:
        void Update(float dt);
        void Render();
        void PrintHelp();

        IGraphicsFactory& m_Factory;
        TextureManager m_TextureManager;
        std::unique_ptr<IWindow> m_Window;
        InputManager m_InputManager;
        std::unique_ptr<OrthographicCamera> m_Camera;

        // 场景渲染器（RHI 封装）
        SceneRenderer m_SceneRenderer;

        // 场景（管理所有根级游戏对象）
        Scene m_Scene;

        // 默认纹理（所有精灵共享）
        std::shared_ptr<Texture> m_Texture;

        // 通过名称查找对象的快捷引用
        GameObject* m_PlayerObj  = nullptr;
        GameObject* m_ChildObj   = nullptr;

        // 状态
        float m_LastFrameTime = 0.0f;
        float m_FpsAccumulator = 0.0f;
        int   m_FrameCount = 0;
        bool  m_ShouldClose = false;

        float m_ClearColorR = 0.1f;
        float m_ClearColorG = 0.1f;
        float m_ClearColorB = 0.15f;

        static constexpr int WINDOW_WIDTH  = 800;
        static constexpr int WINDOW_HEIGHT = 600;
    };

} // namespace Engine
