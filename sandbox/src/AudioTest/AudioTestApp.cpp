#include "AudioTestApp.h"
#include <Engine/Platform/PlatformUtils.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/IWindow.h>
#include <Engine/OpenAL/OpenALAudioEngine.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include <cmath>
#include <cstring>

namespace Engine {

    // ============================================================
    // 构造
    // ============================================================

    AudioTestApp::AudioTestApp(IGraphicsFactory& factory)
        : m_Factory(factory)
        , m_TextureManager(factory)
    {
        // ── 1. 创建窗口（用于 GLFW 事件循环） ──
        m_Window = m_Factory.CreateWindow(m_WindowWidth, m_WindowHeight,
                                          "Audio Test (OpenAL Soft)");

        // ── 2. 初始化音频引擎 ──
        m_AudioEngine = std::make_shared<OpenALAudioEngine>();
        if (!m_AudioEngine->Init()) {
            std::cerr << "[AudioTest] Failed to initialize audio engine!" << std::endl;
            return;
        }

        // ── 3. 尝试加载音频文件 ──
        AudioData audioData;

        // 尝试加载 WAV 文件
        audioData = AudioLoader::Load("assets/audio/test.wav");
        if (!audioData.IsValid()) {
            // 如果 WAV 文件不存在，尝试加载 OGG 文件
            audioData = AudioLoader::Load("assets/audio/test.ogg");
        }
        if (!audioData.IsValid()) {
            // 如果文件都不存在，使用程序化生成的正弦波作为后备
            std::cout << "[AudioTest] No audio files found, generating sine wave" << std::endl;

            int32 sampleRate = 44100;
            float32 duration = 2.0f;
            float32 frequency = 440.0f;
            int32 totalSamples = static_cast<int32>(sampleRate * duration);
            int32 dataSize = totalSamples * 2;

            audioData.pcmData.resize(dataSize);
            int16* samples = reinterpret_cast<int16*>(audioData.pcmData.data());

            for (int32 i = 0; i < totalSamples; ++i) {
                float32 t = static_cast<float32>(i) / sampleRate;
                float32 envelope = 1.0f;
                float32 fadeTime = 0.05f;
                if (t < fadeTime) envelope = t / fadeTime;
                if (t > duration - fadeTime) envelope = (duration - t) / fadeTime;
                float32 sample = std::sin(2.0f * 3.14159f * frequency * t) * envelope;
                samples[i] = static_cast<int16>(sample * 32767.0f);
            }

            audioData.channels   = 1;
            audioData.sampleRate = sampleRate;
            audioData.info.sampleRate = sampleRate;
            audioData.info.dataSize   = dataSize;
            audioData.info.duration   = duration;
            audioData.info.format     = AudioFormat::Mono16;
        } else {
            std::cout << "[AudioTest] Loaded audio: "
                      << audioData.channels << "ch, "
                      << audioData.sampleRate << "Hz, "
                      << audioData.info.duration << "s" << std::endl;
        }

        // ── 4. 创建缓冲区并加载 ──
        m_Buffer = m_AudioEngine->CreateBuffer(
            audioData.pcmData.data(),
            audioData.info.dataSize,
            audioData.info
        );

        // ── 5. 创建音源并播放 ──
        m_Source = m_AudioEngine->CreateSource();
        m_Source->SetPosition(m_SourcePos);
        m_Source->SetLooping(true);  // 持续播放
        m_Source->SetVolume(0.8f);

        // 设置 3D 衰减
        m_Source->SetAttenuation(AttenuationMode::InverseClamped, 2.0f, 20.0f);

        // 播放
        m_Source->Play(m_Buffer);

        // ── 6. 设置听者默认位置 ──
        m_AudioEngine->SetListenerPosition(m_ListenerPos);
        m_AudioEngine->SetListenerOrientation(
            Vec3(0.0f, 0.0f, -1.0f),  // forward
            Vec3(0.0f, 1.0f, 0.0f)   // up
        );

        PrintHelp();
    }

    // ============================================================
    // 析构
    // ============================================================

    AudioTestApp::~AudioTestApp() {
        if (m_Source) {
            m_Source->Stop();
        }
        m_AudioEngine->Shutdown();
    }

    // ============================================================
    // 帮助信息
    // ============================================================

    void AudioTestApp::PrintHelp() {
        std::cout << "==========================================" << std::endl;
        std::cout << "  Audio Test (OpenAL Soft) Started!" << std::endl;
        std::cout << "  操作:" << std::endl;
        std::cout << "    W/S : 音源前后移动 (Z轴)" << std::endl;
        std::cout << "    A/D : 音源左右移动 (X轴)" << std::endl;
        std::cout << "    Q/E : 音源上下移动 (Y轴)" << std::endl;
        std::cout << "    ↑/↓ : 音量增大/减小" << std::endl;
        std::cout << "    ←/→ : 音高降低/升高" << std::endl;
        std::cout << "    Space: 暂停/继续" << std::endl;
        std::cout << "    R   : 重置音源到原点" << std::endl;
        std::cout << "    Esc : 退出" << std::endl;
        std::cout << "==========================================" << std::endl;
    }

    // ============================================================
    // 每帧更新
    // ============================================================

    void AudioTestApp::Update(float32 dt) {
        // ── 键盘控制 ──
        GLFWwindow* window = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());
        if (!window) return;

        float32 speed = 3.0f * dt;

        // 音源位置控制 (WASD + QE)
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) m_SourcePos.z -= speed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) m_SourcePos.z += speed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) m_SourcePos.x -= speed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) m_SourcePos.x += speed;
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) m_SourcePos.y -= speed;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) m_SourcePos.y += speed;
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) m_SourcePos = Vec3(0.0f, 0.0f, 0.0f);

        // 音量控制
        float32 vol = m_Source->GetVolume();
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)   vol += speed;
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) vol -= speed;
        vol = std::max(0.0f, std::min(1.0f, vol));
        m_Source->SetVolume(vol);

        // 音高控制
        float32 pitch = m_Source->GetPitch();
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) pitch += speed * 0.5f;
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)  pitch -= speed * 0.5f;
        pitch = std::max(0.25f, std::min(4.0f, pitch));
        m_Source->SetPitch(pitch);

        // 暂停/继续
        static bool spaceWasPressed = false;
        bool spaceNow = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (spaceNow && !spaceWasPressed) {
            if (m_Source->IsPlaying()) m_Source->Pause();
            else m_Source->Resume();
        }
        spaceWasPressed = spaceNow;

        // 退出
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        // ── 更新音源 3D 位置 ──
        m_Source->SetPosition(m_SourcePos);

        // ── 更新音频引擎（后台异步处理） ──
        m_AudioEngine->Update();
    }

    // ============================================================
    // 渲染
    // ============================================================

    void AudioTestApp::Render() {
        auto context = m_Window->GetContext();
        context->ClearColor(0.05f, 0.05f, 0.1f, 1.0f);

        // 控制台输出位置信息（每秒几次，避免刷屏）
        static float32 outputTimer = 0.0f;
        outputTimer += 1.0f / 60.0f;
        if (outputTimer >= 0.5f) {
            outputTimer = 0.0f;
            std::cout << "\r[Audio] Pos: ("
                      << m_SourcePos.x << ", "
                      << m_SourcePos.y << ", "
                      << m_SourcePos.z << ")  Vol: "
                      << m_Source->GetVolume() << "  Pitch: "
                      << m_Source->GetPitch() << "  State: "
                      << (m_Source->IsPlaying() ? "PLAYING" : "PAUSED")
                      << "    " << std::flush;
        }
    }

    // ============================================================
    // 主循环
    // ============================================================

    void AudioTestApp::Run() {
        m_LastFrameTime = Time::GetTime();

        while (!m_Window->ShouldClose()) {
            float32 time = Time::GetTime();
            float32 dt = time - m_LastFrameTime;
            m_LastFrameTime = time;
            if (dt > 0.25f) dt = 0.25f;

            glfwPollEvents();
            Update(dt);
            Render();

            auto ctx = m_Window->GetContext();
            ctx->SwapBuffers();
        }
    }

} // namespace Engine
