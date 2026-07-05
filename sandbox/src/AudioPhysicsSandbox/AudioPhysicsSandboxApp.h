#pragma once

/**
 * @file AudioPhysicsSandboxApp.h
 * @brief 音频物理沙盒 — 背景音乐循环 + 键盘触发音效 + 拖拽碰撞发声 + 内存资源追踪
 *
 * 功能：
 *   - 程序化生成的背景音乐循环播放
 *   - 数字键 1~5 触发不同音效（钢琴/鼓/贝斯/扫弦/镲）
 *   - 鼠标左键拖动物体，碰撞时播放对应材质的冲击音效
 *   - 控制台每秒输出 FPS、碰撞次数、活跃音效数、音源/物理体/音频剪辑资源计数
 *   - 退出时打印资源释放统计
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
#include <Engine/Core/Physics/IJoint.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/Audio/IAudioEngine.h>
#include <Engine/Core/Audio/IAudioSource.h>
#include <Engine/Core/Audio/IAudioBuffer.h>
#include <Engine/Core/Audio/AudioClip.h>
#include <Engine/Core/Audio/AudioClipManager.h>
#include <Engine/OpenGL/OpenGLPhysicsDebugDraw.h>
#include <Engine/Audio/AudioSystem.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>

namespace Engine {

    // ============================================================
    // 材质类型（用于碰撞音效）
    // ============================================================
    enum class MaterialType : uint8 {
        Stone = 0,
        Wood  = 1,
        Metal = 2,
        Rubber = 3,
        Count
    };

    static const char* GetMaterialName(MaterialType mat) {
        switch (mat) {
            case MaterialType::Stone:  return "Stone";
            case MaterialType::Wood:   return "Wood";
            case MaterialType::Metal:  return "Metal";
            case MaterialType::Rubber: return "Rubber";
            default: return "Unknown";
        }
    }

    // ============================================================
    // 资源追踪统计
    // ============================================================
    struct ResourceStats {
        int32 audioSourcesCreated = 0;
        int32 audioSourcesDestroyed = 0;
        int32 audioBuffersCreated = 0;
        int32 audioBuffersDestroyed = 0;
        int32 physicsBodiesCreated = 0;
        int32 physicsBodiesDestroyed = 0;
        int32 jointsCreated = 0;
        int32 jointsDestroyed = 0;
        int32 totalCollisions = 0;
        int32 clipsLoaded = 0;
        int32 clipsReleased = 0;

        int32 ActiveAudioSources() const { return audioSourcesCreated - audioSourcesDestroyed; }
        int32 ActiveAudioBuffers() const { return audioBuffersCreated - audioBuffersDestroyed; }
        int32 ActivePhysicsBodies() const { return physicsBodiesCreated - physicsBodiesDestroyed; }
        int32 ActiveJoints() const { return jointsCreated - jointsDestroyed; }
    };

    class AudioPhysicsSandboxApp {
    public:
        AudioPhysicsSandboxApp(IGraphicsFactory& factory);
        ~AudioPhysicsSandboxApp();
        void Run();

    private:
        void Update(float32 dt);
        void Render();
        void SetupPhysicsWorld();
        void SetupAudio();
        void SetupBackgroundMusic();
        void SetupKeyboardSounds();
        void SpawnTestBodies();
        void HandleMouseDrag();
        void HandleKeyboardInput();
        void OnCollision(const ContactInfo& info);
        void PrintHelp();
        void PrintResourceStats();
        void PrintFinalStats();

        // ── 音频生成工具 ──
        std::vector<uint8> GenerateImpactWav(MaterialType material);
        std::vector<uint8> GenerateBackgroundMusicWav();
        std::vector<uint8> GenerateKeyboardSoundWav(int32 soundIndex);

        // ── 窗口 / 渲染 ──
        IGraphicsFactory& m_Factory;
        TextureManager m_TextureManager;
        std::unique_ptr<IWindow> m_Window;
        std::shared_ptr<ISpriteBatch> m_SpriteBatch;
        std::shared_ptr<Shader> m_BatchShader;
        std::shared_ptr<Texture> m_Texture;
        OrthographicCamera m_Camera;
        int32 m_WindowWidth  = 1280;
        int32 m_WindowHeight = 720;

        // ── 音频系统 ──
        std::shared_ptr<IAudioEngine> m_AudioEngine;

        // 背景音乐
        std::shared_ptr<AudioClip> m_BgmClip;
        std::shared_ptr<IAudioBuffer> m_BgmBuffer;
        std::shared_ptr<IAudioSource> m_BgmSource;

        // 键盘触发音效
        std::vector<std::shared_ptr<AudioClip>> m_KeyboardClips;

        // 碰撞音效（按材质）
        std::unordered_map<MaterialType, std::shared_ptr<AudioClip>> m_ImpactClips;

        // ── 物理世界 ──
        std::shared_ptr<IPhysicsWorld> m_PhysicsWorld;
        std::shared_ptr<IPhysicsBody>  m_MouseAnchorBody;
        std::shared_ptr<IJoint>        m_MouseJoint;
        IPhysicsBody* m_DraggedBody = nullptr;

        // ── 鼠标拖拽状态 ──
        Vec2  m_MouseWorldPos = {0.0f, 0.0f};
        bool  m_MouseDown = false;

        // ── 场景 ──
        std::unique_ptr<OpenGLPhysicsDebugDraw> m_DebugDraw;

        // 存储所有物理体，用于资源追踪
        std::vector<std::shared_ptr<IPhysicsBody>> m_AllBodies;
        std::vector<std::shared_ptr<IJoint>> m_AllJoints;

        // ── FPS & 统计 ──
        float32 m_FpsTimer = 0.0f;
        int32   m_FpsCount = 0;
        float32 m_CurrentFps = 0.0f;
        float32 m_LastFrameTime = 0.0f;
        static constexpr float32 FIXED_DT = 1.0f / 60.0f;

        // ── 资源追踪 ──
        ResourceStats m_Stats;

        // ── 键盘按键边沿触发 ──
        bool m_KeyLastState[256] = {false};

        bool WasKeyPressed(int key);

        // ── 屏幕坐标转世界坐标 ──
        Vec2 ScreenToWorld(float32 screenX, float32 screenY) const;
    };

} // namespace Engine
