#pragma once

/**
 * @file AudioTestApp.h
 * @brief OpenAL Soft 音频集成测试
 *
 * 测试内容：
 *   - 初始化音频引擎
 *   - 从文件加载 WAV/OGG 音频（通过 AudioLoader）
 *   - 3D 空间音效（WASD 移动音源位置）
 *   - 音高/音量/循环控制
 */

#include <Engine/Core/IWindow.h>
#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/Renderer/SpriteBatch.h>
#include <Engine/Core/RenderResources/Shader.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/Core/Renderer/OrthographicCamera.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/Audio/IAudioEngine.h>
#include <Engine/Core/Audio/AudioLoader.h>
#include <memory>

namespace Engine {

    class AudioTestApp {
    public:
        AudioTestApp(IGraphicsFactory& factory);
        ~AudioTestApp();
        void Run();

    private:
        void Update(float32 dt);
        void Render();
        void PrintHelp();

        // ── 音频引擎 ──
        std::shared_ptr<IAudioEngine> m_AudioEngine;
        std::shared_ptr<IAudioSource> m_Source;
        std::shared_ptr<IAudioBuffer> m_Buffer;

        // ── 音源位置（3D 空间音频演示） ──
        Vec3 m_SourcePos = {0.0f, 0.0f, 0.0f};
        Vec3 m_ListenerPos = {0.0f, 0.0f, 0.0f};

        // ── 渲染资源（仅用于窗口/上下文） ──
        IGraphicsFactory& m_Factory;
        TextureManager m_TextureManager;
        std::unique_ptr<IWindow> m_Window;

        // ── FPS ──
        float32 m_LastFrameTime = 0.0f;
        int32   m_WindowWidth  = 800;
        int32   m_WindowHeight = 600;
    };

} // namespace Engine
