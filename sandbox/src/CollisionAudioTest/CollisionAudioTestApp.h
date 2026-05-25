#pragma once

/**
 * @file CollisionAudioTestApp.h
 * @brief 碰撞音效测试 — 不同材质碰撞产生不同声音
 *
 * 测试内容：
 *   - 物理世界中不同材质的物体碰撞时播放对应的音效
 *   - 材质类型：Stone（石头）、Wood（木头）、Metal（金属）
 *   - 使用 Audio::PlayOneShot 一次性播放
 *   - 使用 AudioClipManager 缓存音频资源
 */

#include <Engine/Core/IWindow.h>
#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/Renderer/SpriteBatch.h>
#include <Engine/Core/RenderResources/Shader.h>
#include <Engine/Core/RenderResources/Texture.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/Core/Renderer/OrthographicCamera.h>
#include <Engine/Core/Input.h>
#include <Engine/Platform/PlatformUtils.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/Physics/IPhysicsWorld.h>
#include <Engine/Core/Physics/IPhysicsBody.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/Audio/IAudioEngine.h>
#include <Engine/Core/Audio/AudioClipManager.h>
#include <Engine/OpenGL/OpenGLPhysicsDebugDraw.h>
#include <Engine/Audio/AudioSystem.h>
#include <memory>
#include <unordered_map>

namespace Engine {

    // ============================================================
    // 材质类型
    // ============================================================
    enum class MaterialType : uint8 {
        Stone = 0,
        Wood  = 1,
        Metal = 2,
        Count
    };

    // ============================================================
    // 材质碰撞音效查找表
    // ============================================================
    static const char* GetMaterialName(MaterialType mat) {
        switch (mat) {
            case MaterialType::Stone: return "Stone";
            case MaterialType::Wood:  return "Wood";
            case MaterialType::Metal: return "Metal";
            default: return "Unknown";
        }
    }

    class CollisionAudioTestApp {
    public:
        CollisionAudioTestApp(IGraphicsFactory& factory);
        ~CollisionAudioTestApp();
        void Run();

    private:
        void Update(float32 dt);
        void Render();
        void SetupPhysicsWorld();
        void SetupAudio();
        void SpawnTestBodies();
        void OnCollision(const ContactInfo& info);
        void PrintHelp();

        /// 生成 WAV 格式的冲击音效数据（纯内存，无需音频文件）
        std::vector<uint8> GenerateImpactWav(MaterialType material);

        // ── 窗口 / 渲染 ──
        IGraphicsFactory& m_Factory;
        TextureManager m_TextureManager;
        std::unique_ptr<IWindow> m_Window;
        std::shared_ptr<ISpriteBatch> m_SpriteBatch;
        std::shared_ptr<Shader> m_BatchShader;
        std::shared_ptr<Texture> m_Texture;
        std::unique_ptr<OrthographicCamera> m_Camera;
        int32 m_WindowWidth  = 1280;
        int32 m_WindowHeight = 720;

        // ── 音频系统 ──
        std::shared_ptr<IAudioEngine> m_AudioEngine;
        AudioClipManager m_AudioClipMgr;
        std::unordered_map<MaterialType, std::shared_ptr<AudioClip>> m_ImpactClips;

        // ── 物理世界 ──
        std::shared_ptr<IPhysicsWorld> m_PhysicsWorld;

        // ── 场景 ──
        Scene m_Scene;
        std::unique_ptr<OpenGLPhysicsDebugDraw> m_DebugDraw;

        // ── FPS ──
        float32 m_FpsTimer = 0.0f;
        int32   m_FpsCount = 0;
        float32 m_CurrentFps = 0.0f;

        // ── 帧时间 ──
        float32 m_LastFrameTime = 0.0f;

        // ── 碰撞统计 ──
        int32 m_TotalCollisions = 0;
    };

} // namespace Engine
