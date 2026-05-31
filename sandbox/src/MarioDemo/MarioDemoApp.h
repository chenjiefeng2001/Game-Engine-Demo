#pragma once

/**
 * @file MarioDemoApp.h
 * @brief 超级马里奥兄弟 Demo — 复刻 1-1 和 1-2 关卡
 *
 * 使用 Game-Engine-Demo 引擎，展示：
 *   - Scene / GameObject / Component 实体系统
 *   - Box2D 物理引擎（重力、碰撞、刚体）
 *   - SpriteComponent 精灵渲染
 *   - Input 输入系统
 *   - Audio 音效播放
 *
 * 操作说明：
 *   A/D 或 ←/→  移动
 *   Space/W/↑    跳跃
 *   R            重置关卡
 *   1            切换 1-1 关卡
 *   2            切换 1-2 关卡
 *   Escape       退出
 */

#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/IWindow.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/InputManager.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/Core/Renderer/OrthographicCamera.h>
#include <Engine/Core/RHI/SceneRenderer.h>
#include <Engine/Core/RHI/IRenderQueue.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/GameObject/SpriteComponent.h>
#include <Engine/Core/Physics/PhysicsComponent.h>
#include <Engine/Core/Physics/PhysicsDefs.h>
#include <Engine/Core/Audio/AudioClip.h>
#include <Engine/Core/Audio/IAudioEngine.h>
#include <Engine/Audio/AudioSystem.h>
#include <Engine/ConsolePanel.h>
#include <Engine/ConsoleCommandRegistry.h>
#include <Engine/MemoryPanel.h>
#include <Engine/MemoryTracker.h>
#include <Engine/Types.h>

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace Engine {

    // ── 玩家状态 ──
    enum class PlayerState {
        Idle,
        Walking,
        Jumping,
        Dead,
        LevelComplete
    };

    class MarioDemoApp {
    public:
        MarioDemoApp(IGraphicsFactory& factory);
        ~MarioDemoApp() = default;

        MarioDemoApp(const MarioDemoApp&) = delete;
        MarioDemoApp& operator=(const MarioDemoApp&) = delete;

        void Run();

    private:
        void BuildLevel_1_1();
        void BuildLevel_1_2();
        void ResetLevel();
        void Die();
        void InitBGM();
        void InitLevelCompleteFanfare();
        void GoToNextLevel();
        void InitConsoleCommands();
        bool InitUI();
        // ── 成员 ──
        IGraphicsFactory&       m_Factory;
        std::unique_ptr<IWindow> m_Window;
        InputManager            m_InputManager;
        TextureManager          m_TextureManager;
        SceneRenderer           m_SceneRenderer;
        OrthographicCamera m_Camera;

        std::shared_ptr<IPhysicsWorld> m_Physics;
        Scene                   m_Scene;

        std::shared_ptr<GameObject> m_Player;
        PlayerState             m_State = PlayerState::Idle;
        float32                 m_PlayerSpeed = 6.0f;
        float32                 m_JumpForce = 12.0f;
        bool                    m_OnGround = false;
        int32                   m_Lives = 3;
        int32                   m_Score = 0;
        int32                   m_CoinsCollected = 0;
        float32                 m_DeathTimer = 0.0f;
        float32                 m_InvTimer = 0.0f;
        float32                 m_LevelCompleteTimer = 0.0f;
        float32                 m_AnimT = 0.0f;
        int32                   m_AnimF = 0;
        bool                    m_ShouldClose = false;

        int32                   m_Level = 0;
        float32                 m_LevelW = 80.0f;
        float32                 m_LevelH = 13.0f;
        float32                 m_ViewWidth = 0.0f;
        float32                 m_ViewHeight = 13.0f;

        struct EnemyData {
            std::shared_ptr<GameObject> obj;
            bool alive = true;
            float32 dir = -1.0f;
            float32 speed = 1.2f;
        };
        std::vector<EnemyData>  m_Enemies;
        std::vector<std::shared_ptr<GameObject>> m_Coins;

        std::shared_ptr<IAudioEngine> m_Audio;
        std::shared_ptr<AudioClip> m_SndJump;
        std::shared_ptr<AudioClip> m_SndCoin;
        std::shared_ptr<AudioClip> m_SndStomp;
        std::shared_ptr<AudioClip> m_SndDie;

        std::shared_ptr<IAudioBuffer> m_BgmBuffer;

        std::shared_ptr<IAudioSource> m_BgmSource;

        std::unordered_map<std::string, std::shared_ptr<Texture>> m_Tex;

        // ── 调试面板 ──
        ConsolePanel m_ConsolePanel;
        MemoryPanel  m_MemoryPanel;
        bool m_UIInitialized = false;

        static constexpr int32 WINDOW_WIDTH  = 800;
        static constexpr int32 WINDOW_HEIGHT = 480;
    };

} // namespace Engine


