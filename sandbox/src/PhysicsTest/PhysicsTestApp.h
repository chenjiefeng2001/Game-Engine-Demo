#pragma once

/**
 * @file PhysicsTestApp.h
 * @brief Box2D 物理集成测试 — 演示解耦后的物理系统使用方式
 *
 * 测试内容：
 *   - 创建物理世界 (Box2DPhysicsWorld)
 *   - 使用抽象接口 IPhysicsWorld / IPhysicsBody
 *   - 通过 PhysicsComponent 将物理体挂载到 GameObject
 *   - 物理步进后自动同步回 TransformComponent
 *   - 碰撞事件回调
 */

#include <Engine/Core/IWindow.h>
#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/Renderer/SpriteBatch.h>
#include <Engine/Core/RenderResources/Shader.h>
#include <Engine/Core/RenderResources/Texture.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/Core/Renderer/OrthographicCamera.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/InputManager.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/Physics/IPhysicsWorld.h>
#include <Engine/Core/Physics/IPhysicsBody.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <memory>

namespace Engine {

    class PhysicsTestApp {
    public:
        PhysicsTestApp(IGraphicsFactory& factory);
        ~PhysicsTestApp();
        void Run();

    private:
        void Update(float32 dt);
        void Render();

        // ── 物理场景 ──
        std::shared_ptr<IPhysicsWorld> m_PhysicsWorld;

        // ── 游戏场景 ──
        Scene m_Scene;

        // ── 渲染资源 ──
        IGraphicsFactory& m_Factory;
        TextureManager m_TextureManager;
        std::unique_ptr<IWindow> m_Window;
        std::shared_ptr<ISpriteBatch> m_SpriteBatch;
        std::shared_ptr<Shader> m_BatchShader;
        std::shared_ptr<Texture> m_Texture;
        std::unique_ptr<OrthographicCamera> m_Camera;

        // ── FPS ──
        float32 m_LastFrameTime = 0.0f;
        int32   m_WindowWidth  = 800;
        int32   m_WindowHeight = 600;

        // ── 物理参数 ──
        static constexpr float32 FIXED_DT = 1.0f / 60.0f;
    };

} // namespace Engine
