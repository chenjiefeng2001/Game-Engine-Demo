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
#include <Engine/Core/Physics/IJoint.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/OpenGL/OpenGLPhysicsDebugDraw.h>
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
        std::shared_ptr<IPhysicsBody>  m_MouseAnchorBody;  ///< 鼠标拖拽的静态锚点

        // ── 游戏场景 ──
        Scene m_Scene;

        // ── 渲染资源 ──
        IGraphicsFactory& m_Factory;
        TextureManager m_TextureManager;
        std::unique_ptr<IWindow> m_Window;
        std::shared_ptr<ISpriteBatch> m_SpriteBatch;
        std::shared_ptr<Shader> m_BatchShader;
        std::shared_ptr<Texture> m_Texture;
        OrthographicCamera m_Camera;

        // ── 鼠标拖拽状态 ──
        std::shared_ptr<IJoint> m_MouseJoint;
        IPhysicsBody* m_DraggedBody = nullptr;
        Vec2  m_MouseWorldPos = {0.0f, 0.0f};
        bool  m_MouseDown = false;

        // ── 调试绘制 ──
        std::unique_ptr<OpenGLPhysicsDebugDraw> m_DebugDraw;
        bool  m_DebugDrawEnabled = true;

        // ── FPS ──
        float32 m_LastFrameTime = 0.0f;
        int32   m_WindowWidth  = 800;
        int32   m_WindowHeight = 600;

        // ── 物理参数 ──
        static constexpr float32 FIXED_DT = 1.0f / 60.0f;

        // ── 屏幕坐标转世界坐标 ──
        Vec2 ScreenToWorld(float32 screenX, float32 screenY) const;

        // ── 热键帮助 ──
        void PrintDebugHelp();
    };

} // namespace Engine
