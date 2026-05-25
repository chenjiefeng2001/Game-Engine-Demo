#include "AudioPhysicsSandboxApp.h"
#include <Engine/Platform/PlatformUtils.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/IWindow.h>
#include <Engine/Platform/PlatformUtils.h>
#include <Engine/Core/Physics/PhysicsDefs.h>
#include <Engine/Box2D/Box2DPhysicsWorld.h>
#include <Engine/OpenAL/OpenALAudioEngine.h>
#include <Engine/OpenGL/OpenGLContext.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <AL/al.h>
#include <AL/alc.h>

namespace Engine {

    // ============================================================
    // WAV 头结构（用于内存生成）
    // ============================================================
    struct WavHeader {
        char     riff[4]     = {'R', 'I', 'F', 'F'};
        uint32_t fileSize    = 0;
        char     wave[4]     = {'W', 'A', 'V', 'E'};
        char     fmtId[4]    = {'f', 'm', 't', ' '};
        uint32_t fmtSize     = 16;
        uint16_t audioFormat = 1;  // PCM
        uint16_t numChannels = 2;  // 立体声（背景音乐用立体声）
        uint32_t sampleRate  = 44100;
        uint32_t byteRate    = 176400;  // 44100 * 2 * 2
        uint16_t blockAlign  = 4;
        uint16_t bitsPerSample = 16;
        char     dataId[4]   = {'d', 'a', 't', 'a'};
        uint32_t dataSize    = 0;
    };

    // ============================================================
    // 生成碰撞冲击音效 WAV
    // ============================================================
    std::vector<uint8> AudioPhysicsSandboxApp::GenerateImpactWav(MaterialType material) {
        float32 baseFreq   = 0.0f;
        float32 duration   = 0.0f;
        float32 harmonics  = 0.0f;

        switch (material) {
            case MaterialType::Stone:
                baseFreq  = 180.0f;   duration = 0.25f; harmonics = 0.6f; break;
            case MaterialType::Wood:
                baseFreq  = 400.0f;   duration = 0.15f; harmonics = 0.3f; break;
            case MaterialType::Metal:
                baseFreq  = 800.0f;   duration = 0.40f; harmonics = 0.8f; break;
            case MaterialType::Rubber:
                baseFreq  = 120.0f;   duration = 0.10f; harmonics = 0.2f; break;
            default:
                baseFreq  = 300.0f;   duration = 0.20f; harmonics = 0.4f; break;
        }

        int32 sampleRate  = 44100;
        int32 totalSamples = static_cast<int32>(sampleRate * duration);
        int32 dataBytes    = totalSamples * sizeof(int16);

        WavHeader header;
        header.numChannels = 1;
        header.sampleRate  = sampleRate;
        header.byteRate    = sampleRate * 2;
        header.blockAlign  = 2;
        header.dataSize    = dataBytes;
        header.fileSize    = dataBytes + sizeof(WavHeader) - 8;

        std::vector<uint8> wavData(sizeof(WavHeader) + dataBytes);
        std::memcpy(wavData.data(), &header, sizeof(WavHeader));

        int16* samples = reinterpret_cast<int16*>(wavData.data() + sizeof(WavHeader));

        for (int32 i = 0; i < totalSamples; ++i) {
            float32 t = static_cast<float32>(i) / sampleRate;
            float32 envelope = std::exp(-6.0f * t / duration);

            float32 sample = std::sin(2.0f * 3.14159f * baseFreq * t);
            sample += harmonics * std::sin(2.0f * 3.14159f * baseFreq * 2.41f * t);
            sample += harmonics * 0.5f * std::sin(2.0f * 3.14159f * baseFreq * 5.73f * t);

            float32 gain = 0.7f;
            sample = sample * envelope * gain;
            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;
            samples[i] = static_cast<int16>(sample * 32767.0f);
        }

        return wavData;
    }

    // ============================================================
    // 生成背景音乐 WAV（简单的 8 小节循环旋律）
    // ============================================================
    std::vector<uint8> AudioPhysicsSandboxApp::GenerateBackgroundMusicWav() {
        float32 bpm = 120.0f;
        float32 beatDuration = 60.0f / bpm;
        float32 totalDuration = beatDuration * 16;  // 16 拍 = 4 小节
        int32 sampleRate = 44100;
        int32 totalSamples = static_cast<int32>(sampleRate * totalDuration);
        int32 dataBytes = totalSamples * 2 * sizeof(int16);  // 立体声

        WavHeader header;
        header.numChannels = 2;
        header.sampleRate  = sampleRate;
        header.byteRate    = sampleRate * 2 * 2;
        header.blockAlign  = 4;
        header.dataSize    = dataBytes;
        header.fileSize    = dataBytes + sizeof(WavHeader) - 8;

        std::vector<uint8> wavData(sizeof(WavHeader) + dataBytes);
        std::memcpy(wavData.data(), &header, sizeof(WavHeader));

        int16* samples = reinterpret_cast<int16*>(wavData.data() + sizeof(WavHeader));

        // 定义简单旋律（频率 * 节拍数）
        struct Note {
            float32 freq;     // 频率 (0 = 休止)
            float32 beats;    // 节拍数
        };

        // C 大调旋律
        Note melody[] = {
            {261.63f, 1.0f},  // C4
            {293.66f, 1.0f},  // D4
            {329.63f, 1.0f},  // E4
            {349.23f, 1.0f},  // F4
            {392.00f, 1.0f},  // G4
            {349.23f, 1.0f},  // F4
            {329.63f, 1.0f},  // E4
            {293.66f, 1.0f},  // D4
            {261.63f, 0.5f},  // C4
            {261.63f, 0.5f},  // C4
            {329.63f, 1.0f},  // E4
            {392.00f, 1.0f},  // G4
            {440.00f, 1.0f},  // A4
            {392.00f, 1.0f},  // G4
            {329.63f, 1.0f},  // E4
            {261.63f, 1.0f},  // C4
        };
        int32 noteCount = sizeof(melody) / sizeof(melody[0]);

        int32 sampleIndex = 0;
        for (int32 n = 0; n < noteCount; ++n) {
            float32 freq = melody[n].freq;
            int32 noteSamples = static_cast<int32>(sampleRate * beatDuration * melody[n].beats);

            for (int32 i = 0; i < noteSamples && sampleIndex < totalSamples; ++i, ++sampleIndex) {
                float32 t = static_cast<float32>(i) / sampleRate;
                float32 noteT = static_cast<float32>(i) / noteSamples;

                // 包络：起音 + 衰减 + 延音
                float32 envelope = 1.0f;
                if (noteT < 0.02f) envelope = noteT / 0.02f;
                else if (noteT < 0.1f) envelope = 1.0f - (noteT - 0.02f) / 0.08f * 0.3f;
                else envelope = 0.7f;

                float32 sample = 0.0f;
                if (freq > 0.0f) {
                    // 基频 + 柔和泛音
                    sample = std::sin(2.0f * 3.14159f * freq * t) * 0.5f;
                    sample += std::sin(2.0f * 3.14159f * freq * 2.0f * t) * 0.2f;
                    sample += std::sin(2.0f * 3.14159f * freq * 3.0f * t) * 0.1f;
                    sample *= envelope * 0.6f;
                }

                // 添加和弦伴奏（简单的和弦进行）
                float32 chordFreq = 0.0f;
                int32 chordIndex = n % 4;
                if (chordIndex == 0) chordFreq = 261.63f;  // C
                else if (chordIndex == 1) chordFreq = 293.66f;  // Dm
                else if (chordIndex == 2) chordFreq = 349.23f;  // F
                else chordFreq = 392.00f;  // G

                float32 chordSample = std::sin(2.0f * 3.14159f * chordFreq * 0.5f * t) * 0.15f;
                chordSample += std::sin(2.0f * 3.14159f * chordFreq * 0.75f * t) * 0.1f;
                chordSample *= envelope * 0.5f;

                // 写入立体声（左声道 = 旋律，右声道 = 和弦）
                int32 leftVal  = static_cast<int32>((sample) * 32767.0f);
                int32 rightVal = static_cast<int32>((chordSample) * 32767.0f);

                if (leftVal > 32767) leftVal = 32767;
                if (leftVal < -32768) leftVal = -32768;
                if (rightVal > 32767) rightVal = 32767;
                if (rightVal < -32768) rightVal = -32768;

                samples[sampleIndex * 2]     = static_cast<int16>(leftVal);
                samples[sampleIndex * 2 + 1] = static_cast<int16>(rightVal);
            }
        }

        std::cout << "[Audio] Generated background music: " << totalDuration
                  << "s, " << noteCount << " notes, stereo" << std::endl;
        return wavData;
    }

    // ============================================================
    // 生成键盘触发音效 WAV（5 种不同音色）
    // ============================================================
    std::vector<uint8> AudioPhysicsSandboxApp::GenerateKeyboardSoundWav(int32 soundIndex) {
        float32 baseFreq   = 0.0f;
        float32 duration   = 0.0f;
        float32 harmonics  = 0.0f;
        float32 volume     = 0.7f;
        const char* name   = "";

        switch (soundIndex) {
            case 0: // 钢琴音
                baseFreq = 523.25f; duration = 0.6f; harmonics = 0.4f; volume = 0.8f;
                name = "Piano (C5)"; break;
            case 1: // 鼓音
                baseFreq = 80.0f;   duration = 0.3f; harmonics = 0.9f; volume = 1.0f;
                name = "Drum (Kick)"; break;
            case 2: // 贝斯
                baseFreq = 130.81f; duration = 0.5f; harmonics = 0.3f; volume = 0.7f;
                name = "Bass (C3)"; break;
            case 3: // 扫弦
                baseFreq = 800.0f;  duration = 0.2f; harmonics = 0.7f; volume = 0.6f;
                name = "Strum"; break;
            case 4: // 镲
                baseFreq = 2000.0f; duration = 0.8f; harmonics = 1.5f; volume = 0.5f;
                name = "Cymbal"; break;
            default:
                baseFreq = 440.0f;  duration = 0.3f; harmonics = 0.5f; volume = 0.7f;
                name = "Tone"; break;
        }

        int32 sampleRate  = 44100;
        int32 totalSamples = static_cast<int32>(sampleRate * duration);
        int32 dataBytes    = totalSamples * sizeof(int16);

        WavHeader header;
        header.numChannels = 1;
        header.sampleRate  = sampleRate;
        header.byteRate    = sampleRate * 2;
        header.blockAlign  = 2;
        header.dataSize    = dataBytes;
        header.fileSize    = dataBytes + sizeof(WavHeader) - 8;

        std::vector<uint8> wavData(sizeof(WavHeader) + dataBytes);
        std::memcpy(wavData.data(), &header, sizeof(WavHeader));

        int16* samples = reinterpret_cast<int16*>(wavData.data() + sizeof(WavHeader));

        for (int32 i = 0; i < totalSamples; ++i) {
            float32 t = static_cast<float32>(i) / sampleRate;

            // 包络
            float32 envelope = 1.0f;
            if (soundIndex == 0) {
                // 钢琴：快速起音，缓慢衰减
                if (t < 0.005f) envelope = t / 0.005f;
                else envelope = std::exp(-2.5f * t);
            } else if (soundIndex == 1) {
                // 鼓：极短冲击
                envelope = std::exp(-15.0f * t);
            } else if (soundIndex == 4) {
                // 镲：长衰减 + 噪声
                envelope = std::exp(-4.0f * t);
            } else {
                envelope = std::exp(-6.0f * t / duration);
            }

            float32 sample = 0.0f;

            if (soundIndex == 4) {
                // 镲：用噪声模拟
                float32 noise = (static_cast<float32>(rand()) / RAND_MAX) * 2.0f - 1.0f;
                sample = noise * envelope;
            } else if (soundIndex == 1) {
                // 鼓：低频正弦波 + 噪声
                sample = std::sin(2.0f * 3.14159f * baseFreq * t) * 0.7f;
                float32 noise = (static_cast<float32>(rand()) / RAND_MAX) * 2.0f - 1.0f;
                sample += noise * 0.3f * envelope;
                sample *= envelope;
            } else {
                // 乐器音：基频 + 泛音
                sample = std::sin(2.0f * 3.14159f * baseFreq * t);
                sample += harmonics * std::sin(2.0f * 3.14159f * baseFreq * 2.0f * t);
                sample += harmonics * 0.3f * std::sin(2.0f * 3.14159f * baseFreq * 4.0f * t);
                sample *= envelope * volume;
            }

            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;
            samples[i] = static_cast<int16>(sample * 32767.0f);
        }

        std::cout << "[Audio] Generated keyboard sound #" << soundIndex
                  << ": " << name << " (" << (wavData.size() / 1024) << " KB)" << std::endl;
        return wavData;
    }

    // ============================================================
    // 键盘边沿触发辅助
    // ============================================================
    bool AudioPhysicsSandboxApp::WasKeyPressed(int key) {
        GLFWwindow* window = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());
        if (!window || key < 0 || key >= 256) return false;
        bool current = glfwGetKey(window, key) == GLFW_PRESS;
        bool pressed = current && !m_KeyLastState[key];
        m_KeyLastState[key] = current;
        return pressed;
    }

    // ============================================================
    // 屏幕坐标 → 世界坐标
    // ============================================================
    Vec2 AudioPhysicsSandboxApp::ScreenToWorld(float32 screenX, float32 screenY) const {
        float32 ndcX = 2.0f * screenX / static_cast<float32>(m_WindowWidth) - 1.0f;
        float32 ndcY = 1.0f - 2.0f * screenY / static_cast<float32>(m_WindowHeight);

        const float32* vp = m_Camera->GetViewProjectionMatrixPtr();
        glm::mat4 viewProj = glm::make_mat4(vp);
        glm::mat4 invVP = glm::inverse(viewProj);

        glm::vec4 world = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        return Vec2(world.x / world.w, world.y / world.w);
    }

    // ============================================================
    // 构造
    // ============================================================
    AudioPhysicsSandboxApp::AudioPhysicsSandboxApp(IGraphicsFactory& factory)
        : m_Factory(factory)
        , m_TextureManager(factory)
    {
        // ── 1. 创建窗口 ──
        m_Window = m_Factory.CreateWindow(m_WindowWidth, m_WindowHeight,
                                          "Audio Physics Sandbox");

        m_Window->SetEventCallback([this](Event& e) {
            if (e.type == EventType::WindowResize) {
                m_WindowWidth  = e.resize.width;
                m_WindowHeight = e.resize.height;

                float32 aspect = static_cast<float32>(m_WindowWidth) /
                                 static_cast<float32>(m_WindowHeight);
                float32 viewHeight = 12.0f;
                float32 viewWidth  = viewHeight * aspect;
                m_Camera = std::make_unique<OrthographicCamera>(
                    -viewWidth * 0.5f, viewWidth * 0.5f,
                    -viewHeight * 0.5f, viewHeight * 0.5f);

                auto* ctx = m_Window->GetContext();
                if (ctx) ctx->OnResize(m_WindowWidth, m_WindowHeight);
            }
        });

        // ── 2. 创建相机 ──
        float32 aspect = static_cast<float32>(m_WindowWidth) /
                         static_cast<float32>(m_WindowHeight);
        float32 viewHeight = 12.0f;
        float32 viewWidth  = viewHeight * aspect;
        m_Camera = std::make_unique<OrthographicCamera>(
            -viewWidth * 0.5f, viewWidth * 0.5f,
            -viewHeight * 0.5f, viewHeight * 0.5f);

        // ── 3. 创建渲染资源 ──
        auto* context = m_Window->GetContext();
        m_SpriteBatch = m_Factory.CreateSpriteBatch(*context);
        m_BatchShader = m_Factory.CreateShader(
            "assets/shaders/sprite_batch.vert",
            "assets/shaders/sprite_batch.frag"
        );
        m_Texture = m_TextureManager.Load("assets/textures/test.png");

        // ── 4. 初始化音频引擎 ──
        std::cout << "\n===== Audio Physics Sandbox =====" << std::endl;
        std::cout << "[Init] Initializing audio engine..." << std::endl;
        m_AudioEngine = std::make_shared<OpenALAudioEngine>();
        if (!m_AudioEngine->Init()) {
            std::cerr << "[Init] FAILED to init audio engine!" << std::endl;
        } else {
            std::cout << "[Init] Audio engine initialized successfully" << std::endl;
        }

        // ── 5. 生成音频资源 ──
        SetupAudio();

        // ── 6. 设置背景音乐 ──
        SetupBackgroundMusic();

        // ── 7. 设置键盘音效 ──
        SetupKeyboardSounds();

        // ── 8. 创建物理世界 ──
        SetupPhysicsWorld();

        // ── 9. 生成测试物体 ──
        SpawnTestBodies();

        PrintHelp();
    }

    // ============================================================
    // 析构 — 验证内存和资源释放（按依赖关系逆序释放）
    // ============================================================
    AudioPhysicsSandboxApp::~AudioPhysicsSandboxApp() {
        std::cout << "\n===== Shutdown - Resource Release Verification =====" << std::endl;

        // 1. 停止背景音乐音源
        if (m_BgmSource) {
            m_BgmSource->Stop();
            std::cout << "[Shutdown] Background music stopped" << std::endl;
        }

        // 2. 停止所有一次性音效
        if (m_AudioEngine) {
            int32 activeBefore = Audio::GetActiveOneShotCount();
            Audio::StopAllOneShots(*m_AudioEngine);
            std::cout << "[Shutdown] Stopped " << activeBefore
                      << " active one-shot sounds" << std::endl;
        }

        // 3. 释放音频剪辑（在关闭引擎前释放 OpenAL 缓冲区）
        for (auto& clip : m_KeyboardClips) {
            if (clip) {
                clip->Release();
                m_Stats.clipsReleased++;
            }
        }
        m_KeyboardClips.clear();

        for (auto& [mat, clip] : m_ImpactClips) {
            if (clip) {
                clip->Release();
                m_Stats.clipsReleased++;
            }
        }
        m_ImpactClips.clear();

        std::cout << "[Shutdown] Released " << m_Stats.clipsReleased
                  << " audio clips" << std::endl;

        // 4. 销毁 BGM 音源和缓冲区（在关闭引擎前释放 OpenAL 资源）
        if (m_BgmSource && m_AudioEngine) {
            m_AudioEngine->DestroySource(m_BgmSource.get());
            m_Stats.audioSourcesDestroyed++;
        }
        m_BgmSource.reset();
        m_BgmBuffer.reset();
        m_Stats.audioBuffersDestroyed++;

        // 5. 销毁所有关节
        for (auto& joint : m_AllJoints) {
            if (m_PhysicsWorld && joint) {
                m_PhysicsWorld->DestroyJoint(joint.get());
                m_Stats.jointsDestroyed++;
            }
        }
        m_AllJoints.clear();
        if (m_MouseJoint) {
            m_PhysicsWorld->DestroyJoint(m_MouseJoint.get());
            m_Stats.jointsDestroyed++;
        }
        m_MouseJoint.reset();
        m_DraggedBody = nullptr;

        // 6. 销毁所有物理体
        for (auto& body : m_AllBodies) {
            if (m_PhysicsWorld && body) {
                m_PhysicsWorld->DestroyBody(body.get());
                m_Stats.physicsBodiesDestroyed++;
            }
        }
        m_AllBodies.clear();
        m_MouseAnchorBody.reset();

        // 7. 关闭音频引擎（最后关闭，确保所有 OpenAL 资源已先释放）
        if (m_AudioEngine) {
            m_AudioEngine->Shutdown();
            std::cout << "[Shutdown] Audio engine shut down" << std::endl;
        }

        PrintFinalStats();
        std::cout << "================================================\n" << std::endl;
    }

    // ============================================================
    // SetupAudio — 生成所有材质冲击音效
    // ============================================================
    void AudioPhysicsSandboxApp::SetupAudio() {
        std::cout << "[Audio] Generating impact sounds for materials..." << std::endl;
        for (int32 i = 0; i < static_cast<int32>(MaterialType::Count); ++i) {
            MaterialType mat = static_cast<MaterialType>(i);
            std::vector<uint8> wavData = GenerateImpactWav(mat);

            auto clip = std::make_shared<AudioClip>();
            clip->LoadFromMemory(wavData.data(), wavData.size(), "wav");

            if (clip->IsValid()) {
                m_ImpactClips[mat] = clip;
                m_Stats.clipsLoaded++;
                std::cout << "  [Audio] " << GetMaterialName(mat)
                          << " impact loaded (" << (wavData.size() / 1024) << " KB)" << std::endl;
            }
        }
    }

    // ============================================================
    // SetupBackgroundMusic — 创建并播放循环背景音乐
    // ============================================================
    void AudioPhysicsSandboxApp::SetupBackgroundMusic() {
        std::cout << "[Music] Generating background music..." << std::endl;

        std::vector<uint8> wavData = GenerateBackgroundMusicWav();

        // 解析 WAV 数据获取 PCM 信息
        // WAV header 是 44 字节，PCM 数据从偏移 44 开始
        const WavHeader* header = reinterpret_cast<const WavHeader*>(wavData.data());
        const void* pcmData = wavData.data() + sizeof(WavHeader);
        int32 dataSize = static_cast<int32>(wavData.size() - sizeof(WavHeader));

        AudioClipInfo info;
        info.sampleRate = header->sampleRate;
        info.dataSize = dataSize;
        info.duration = static_cast<float32>(dataSize) / (header->sampleRate * 2 * sizeof(int16));
        // 立体声 16-bit
        info.format = AudioFormat::Stereo16;

        // 通过 IAudioEngine 创建 buffer
        m_BgmBuffer = m_AudioEngine->CreateBuffer(pcmData, dataSize, info);
        if (m_BgmBuffer) m_Stats.audioBuffersCreated++;

        // 创建音源
        m_BgmSource = m_AudioEngine->CreateSource();
        m_Stats.audioSourcesCreated++;

        m_BgmSource->SetLooping(true);
        m_BgmSource->SetVolume(0.4f);
        m_BgmSource->SetPosition(Vec3(0.0f, 0.0f, 0.0f));

        // 开始播放
        m_BgmSource->Play(m_BgmBuffer);
        std::cout << "[Music] Background music playing (looping, vol=0.4, "
                  << info.duration << "s)" << std::endl;
    }

    // ============================================================
    // SetupKeyboardSounds — 生成 5 种键盘触发音效
    // ============================================================
    void AudioPhysicsSandboxApp::SetupKeyboardSounds() {
        std::cout << "[Audio] Generating keyboard-triggered sounds..." << std::endl;
        for (int32 i = 0; i < 5; ++i) {
            std::vector<uint8> wavData = GenerateKeyboardSoundWav(i);

            auto clip = std::make_shared<AudioClip>();
            if (clip->LoadFromMemory(wavData.data(), wavData.size(), "wav")) {
                m_KeyboardClips.push_back(clip);
                m_Stats.clipsLoaded++;
            }
        }
        std::cout << "[Audio] " << m_KeyboardClips.size()
                  << " keyboard sounds ready" << std::endl;
    }

    // ============================================================
    // SetupPhysicsWorld — 创建物理世界
    // ============================================================
    void AudioPhysicsSandboxApp::SetupPhysicsWorld() {
        std::cout << "[Physics] Creating physics world..." << std::endl;
        m_PhysicsWorld = std::make_shared<Box2DPhysicsWorld>(
            Vec2(0.0f, -9.81f)
        );

        // 碰撞回调 — 播放冲击音效
        m_PhysicsWorld->SetContactBeginCallback(
            [this](const ContactInfo& info) {
                OnCollision(info);
            }
        );

        // 持久碰撞回调 — 检测持续碰撞的冲量
        m_PhysicsWorld->SetContactPersistCallback(
            [this](const ContactPersistData& data) {
                // 冲量较大时才播放音效
                if (data.impulse > 0.5f) {
                    ContactInfo info;
                    info.bodyA = data.bodyA;
                    info.bodyB = data.bodyB;
                    info.point = data.point;
                    info.normal = data.normal;
                    info.impulse = data.impulse;
                    OnCollision(info);
                }
            }
        );

        // 创建鼠标锚点 body
        {
            BodyDef anchorDef;
            anchorDef.type = BodyType::Static;
            anchorDef.position = Vec2(0.0f, 0.0f);
            m_MouseAnchorBody = m_PhysicsWorld->CreateBody(anchorDef);
            m_Stats.physicsBodiesCreated++;
            m_AllBodies.push_back(m_MouseAnchorBody);
        }

        // 调试绘制
        auto* renderCtx = m_Window->GetContext();
        auto& glContext = static_cast<OpenGLContext*>(renderCtx)->GetGL();
        m_DebugDraw = std::make_unique<OpenGLPhysicsDebugDraw>(glContext);
        m_DebugDraw->SetFlags(DebugDraw_Shape | DebugDraw_Joint);
        m_PhysicsWorld->SetDebugDraw(m_DebugDraw.get());

        std::cout << "[Physics] World created (gravity: 0, -9.81)" << std::endl;
    }

    // ============================================================
    // SpawnTestBodies — 创建地面和测试物体
    // ============================================================
    void AudioPhysicsSandboxApp::SpawnTestBodies() {
        std::cout << "[Scene] Spawning physics bodies..." << std::endl;

        // ── 地面（材质 = 无，不发声） ──
        {
            BodyDef def;
            def.type = BodyType::Static;
            def.position = Vec2(0.0f, -5.0f);
            def.shape.type = ShapeType::Box;
            def.shape.boxSize = Vec2(9.0f, 0.5f);
            def.friction = 0.5f;
            def.userData = nullptr;
            auto body = m_PhysicsWorld->CreateBody(def);
            m_Stats.physicsBodiesCreated++;
            m_AllBodies.push_back(body);
        }

        // ── 左侧墙壁 ──
        {
            BodyDef def;
            def.type = BodyType::Static;
            def.position = Vec2(-4.5f, 0.0f);
            def.shape.type = ShapeType::Box;
            def.shape.boxSize = Vec2(0.5f, 5.0f);
            def.friction = 0.5f;
            auto body = m_PhysicsWorld->CreateBody(def);
            m_Stats.physicsBodiesCreated++;
            m_AllBodies.push_back(body);
        }

        // ── 右侧墙壁 ──
        {
            BodyDef def;
            def.type = BodyType::Static;
            def.position = Vec2(4.5f, 0.0f);
            def.shape.type = ShapeType::Box;
            def.shape.boxSize = Vec2(0.5f, 5.0f);
            def.friction = 0.5f;
            auto body = m_PhysicsWorld->CreateBody(def);
            m_Stats.physicsBodiesCreated++;
            m_AllBodies.push_back(body);
        }

        // ── 4 种材质的方块 ──
        struct MatSpawn {
            MaterialType mat;
            Vec2 pos;
            Vec2 boxSize;
            float32 density;
        };

        MatSpawn boxes[] = {
            {MaterialType::Stone,  Vec2(-2.5f, 3.0f), Vec2(0.45f, 0.45f), 2.0f},
            {MaterialType::Wood,   Vec2(0.0f,  4.0f), Vec2(0.45f, 0.45f), 0.8f},
            {MaterialType::Metal,  Vec2(2.5f,  5.0f), Vec2(0.45f, 0.45f), 3.0f},
            {MaterialType::Rubber, Vec2(-1.5f, 6.0f), Vec2(0.50f, 0.50f), 0.5f},
        };

        for (auto& spawn : boxes) {
            BodyDef def;
            def.type = BodyType::Dynamic;
            def.position = spawn.pos;
            def.shape.type = ShapeType::Box;
            def.shape.boxSize = spawn.boxSize;
            def.density  = spawn.density;
            def.friction = 0.5f;
            def.restitution = 0.3f;
            def.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(
                static_cast<uint8>(spawn.mat)));
            auto body = m_PhysicsWorld->CreateBody(def);
            m_Stats.physicsBodiesCreated++;
            m_AllBodies.push_back(body);
        }

        // ── 圆球堆叠（混合材质） ──
        const MaterialType stackMats[] = {
            MaterialType::Stone, MaterialType::Wood,
            MaterialType::Metal, MaterialType::Rubber
        };
        for (int32 i = 0; i < 8; ++i) {
            MaterialType mat = stackMats[i % 4];
            BodyDef def;
            def.type = BodyType::Dynamic;
            def.position = Vec2(-3.0f + i * 0.8f, 8.0f + i * 0.6f);
            def.shape.type = ShapeType::Circle;
            def.shape.circleRadius = 0.3f;
            def.density  = 1.0f + static_cast<float32>(i % 4) * 0.3f;
            def.friction = 0.4f;
            def.restitution = 0.5f;
            def.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(
                static_cast<uint8>(mat)));
            auto body = m_PhysicsWorld->CreateBody(def);
            m_Stats.physicsBodiesCreated++;
            m_AllBodies.push_back(body);
        }

        // ── 生成一个额外的球用于拖拽演示（高弹性） ──
        {
            BodyDef def;
            def.type = BodyType::Dynamic;
            def.position = Vec2(3.0f, 2.0f);
            def.shape.type = ShapeType::Circle;
            def.shape.circleRadius = 0.5f;
            def.density  = 1.5f;
            def.friction = 0.3f;
            def.restitution = 0.8f;
            def.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(
                static_cast<uint8>(MaterialType::Rubber)));
            auto body = m_PhysicsWorld->CreateBody(def);
            m_Stats.physicsBodiesCreated++;
            m_AllBodies.push_back(body);
        }

        // ── 被弹簧连接的一对箱子（演示持续碰撞） ──
        {
            BodyDef defA;
            defA.type = BodyType::Dynamic;
            defA.position = Vec2(-1.5f, 0.5f);
            defA.shape.type = ShapeType::Box;
            defA.shape.boxSize = Vec2(0.4f, 0.4f);
            defA.density = 1.0f;
            defA.friction = 0.5f;
            defA.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(
                static_cast<uint8>(MaterialType::Wood)));
            auto bodyA = m_PhysicsWorld->CreateBody(defA);
            m_Stats.physicsBodiesCreated++;
            m_AllBodies.push_back(bodyA);

            BodyDef defB;
            defB.type = BodyType::Dynamic;
            defB.position = Vec2(1.5f, 0.5f);
            defB.shape.type = ShapeType::Box;
            defB.shape.boxSize = Vec2(0.4f, 0.4f);
            defB.density = 1.0f;
            defB.friction = 0.5f;
            defB.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(
                static_cast<uint8>(MaterialType::Metal)));
            auto bodyB = m_PhysicsWorld->CreateBody(defB);
            m_Stats.physicsBodiesCreated++;
            m_AllBodies.push_back(bodyB);

            // 距离关节（弹簧）
            DistanceJointDef jd;
            jd.bodyA = bodyA.get();
            jd.bodyB = bodyB.get();
            jd.localAnchorA = Vec2(0.5f, 0.0f);
            jd.localAnchorB = Vec2(-0.5f, 0.0f);
            jd.length = 3.0f;
            jd.stiffness = 40.0f;
            jd.damping = 1.5f;
            auto joint = m_PhysicsWorld->CreateJoint(jd);
            m_Stats.jointsCreated++;
            m_AllJoints.push_back(joint);
        }

        int32 totalBodies = 2 + 2 + 4 + 8 + 1 + 2; // 地面+墙壁+boxes+balls+rubber+spring
        int32 totalJoints = 1;
        std::cout << "[Scene] " << totalBodies << " bodies, "
                  << totalJoints << " joint created" << std::endl;
    }

    // ============================================================
    // OnCollision — 碰撞时播放音效
    // ============================================================
    void AudioPhysicsSandboxApp::OnCollision(const ContactInfo& info) {
        if (!m_AudioEngine) return;

        auto getMaterial = [](const void* bodyPtr) -> MaterialType {
            if (!bodyPtr) return MaterialType::Count;
            const IPhysicsBody* body = static_cast<const IPhysicsBody*>(bodyPtr);
            void* ud = body->GetUserData();
            if (!ud) return MaterialType::Count;
            uintptr_t val = reinterpret_cast<uintptr_t>(ud);
            if (val >= static_cast<uintptr_t>(MaterialType::Count)) {
                return MaterialType::Count;
            }
            return static_cast<MaterialType>(val);
        };

        MaterialType matA = getMaterial(info.bodyA);
        MaterialType matB = getMaterial(info.bodyB);

        if (matA == MaterialType::Count && matB == MaterialType::Count) {
            return;
        }

        // 选择主材质：取枚举值较大的
        MaterialType primaryMat = (matA != MaterialType::Count) ? matA : matB;
        if (matA != MaterialType::Count && matB != MaterialType::Count) {
            primaryMat = static_cast<MaterialType>(
                std::max(static_cast<uint8>(matA), static_cast<uint8>(matB)));
        }

        auto it = m_ImpactClips.find(primaryMat);
        if (it == m_ImpactClips.end() || !it->second->IsValid()) return;

        // 根据碰撞冲量调整音量
        float32 volume = std::min(1.0f, std::max(0.1f, info.impulse * 0.5f));

        Vec3 impactPos(info.point.x, info.point.y, 0.0f);
        Audio::PlayOneShot(*m_AudioEngine, *it->second, impactPos);

        m_Stats.totalCollisions++;

        // 碰撞日志（仅输出冲量 > 2 的显著碰撞，避免刷屏）
        if (info.impulse > 2.0f) {
            std::cout << "[Collision] " << GetMaterialName(matA)
                      << " vs " << GetMaterialName(matB)
                      << " | impulse: " << info.impulse
                      << " | playing: " << GetMaterialName(primaryMat)
                      << std::endl;
        }
    }

    // ============================================================
    // HandleMouseDrag — 鼠标拖拽
    // ============================================================
    void AudioPhysicsSandboxApp::HandleMouseDrag() {
        GLFWwindow* window = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());
        if (!window) return;

        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        m_MouseWorldPos = ScreenToWorld(static_cast<float32>(mouseX),
                                         static_cast<float32>(mouseY));

        bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

        if (leftDown && !m_MouseDown) {
            // 鼠标按下：AABB 查询
            m_MouseDown = true;
            const float32 queryHalf = 0.15f;
            auto hits = m_PhysicsWorld->QueryAABB(m_MouseWorldPos,
                                                   Vec2(queryHalf, queryHalf));
            for (auto* body : hits) {
                if (body && body->GetType() == BodyType::Dynamic) {
                    MouseJointDef jd;
                    jd.bodyA = m_MouseAnchorBody.get();
                    jd.bodyB = body;
                    jd.target = m_MouseWorldPos;
                    jd.maxForce = 500.0f;
                    jd.stiffness = 100.0f;
                    jd.damping = 5.0f;
                    m_MouseJoint = m_PhysicsWorld->CreateJoint(jd);
                    m_Stats.jointsCreated++;
                    m_DraggedBody = body;
                    std::cout << "[Mouse] Dragging object started" << std::endl;
                    break;
                }
            }
        } else if (!leftDown && m_MouseDown) {
            // 释放鼠标
            m_MouseDown = false;
            if (m_MouseJoint) {
                m_PhysicsWorld->DestroyJoint(m_MouseJoint.get());
                m_Stats.jointsDestroyed++;
                m_MouseJoint.reset();
                std::cout << "[Mouse] Object released" << std::endl;
                m_DraggedBody = nullptr;
            }
        } else if (leftDown && m_MouseJoint) {
            // 拖拽中
            m_MouseJoint->SetTarget(m_MouseWorldPos);
        }
    }

    // ============================================================
    // HandleKeyboardInput — 键盘触发音效
    // ============================================================
    void AudioPhysicsSandboxApp::HandleKeyboardInput() {
        GLFWwindow* window = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());
        if (!window) return;

        // 数字键 1~5 触发不同音效
        static const int keyMap[5] = {
            GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_5
        };
        static const char* soundNames[5] = {
            "Piano (C5)", "Drum (Kick)", "Bass (C3)", "Strum", "Cymbal"
        };

        for (int32 i = 0; i < 5; ++i) {
            if (WasKeyPressed(keyMap[i])) {
                if (i < static_cast<int32>(m_KeyboardClips.size()) &&
                    m_KeyboardClips[i] && m_KeyboardClips[i]->IsValid()) {
                    Audio::PlayOneShot(*m_AudioEngine, *m_KeyboardClips[i]);
                    std::cout << "[Keyboard] Played sound " << (i + 1)
                              << ": " << soundNames[i] << std::endl;
                }
            }
        }

        // Esc 退出
        if (WasKeyPressed(GLFW_KEY_ESCAPE)) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        // B 切换背景音乐
        static bool bgmToggle = true;
        if (WasKeyPressed(GLFW_KEY_B)) {
            bgmToggle = !bgmToggle;
            if (m_BgmSource) {
                if (bgmToggle) {
                    m_BgmSource->Play(m_BgmBuffer);
                    std::cout << "[Music] Background music resumed" << std::endl;
                } else {
                    m_BgmSource->Pause();
                    std::cout << "[Music] Background music paused" << std::endl;
                }
            }
        }
    }

    // ============================================================
    // PrintResourceStats — 控制台输出资源统计
    // ============================================================
    void AudioPhysicsSandboxApp::PrintResourceStats() {
        std::cout << "\r[Stats] FPS: " << std::fixed << std::setprecision(1)
                  << m_CurrentFps
                  << " | Collisions: " << m_Stats.totalCollisions
                  << " | Active sounds: " << Audio::GetActiveOneShotCount()
                  << " | AudioSources: " << m_Stats.ActiveAudioSources()
                  << "/" << m_Stats.audioSourcesCreated
                  << " | PhysicsBodies: " << m_Stats.ActivePhysicsBodies()
                  << "/" << m_Stats.physicsBodiesCreated
                  << " | Joints: " << m_Stats.ActiveJoints()
                  << "/" << m_Stats.jointsCreated
                  << " | Clips: " << m_Stats.clipsLoaded
                  << "    " << std::flush;
    }

    // ============================================================
    // PrintFinalStats — 退出时打印最终统计
    // ============================================================
    void AudioPhysicsSandboxApp::PrintFinalStats() {
        std::cout << "\n----- Final Resource Statistics -----" << std::endl;
        std::cout << "  Audio Sources:    " << m_Stats.audioSourcesCreated
                  << " created, " << m_Stats.audioSourcesDestroyed
                  << " destroyed (leaked: "
                  << (m_Stats.audioSourcesCreated - m_Stats.audioSourcesDestroyed)
                  << ")" << std::endl;
        std::cout << "  Audio Buffers:    " << m_Stats.audioBuffersCreated
                  << " created, " << m_Stats.audioBuffersDestroyed
                  << " destroyed (leaked: "
                  << (m_Stats.audioBuffersCreated - m_Stats.audioBuffersDestroyed)
                  << ")" << std::endl;
        std::cout << "  Physics Bodies:   " << m_Stats.physicsBodiesCreated
                  << " created, " << m_Stats.physicsBodiesDestroyed
                  << " destroyed (leaked: "
                  << (m_Stats.physicsBodiesCreated - m_Stats.physicsBodiesDestroyed)
                  << ")" << std::endl;
        std::cout << "  Joints:           " << m_Stats.jointsCreated
                  << " created, " << m_Stats.jointsDestroyed
                  << " destroyed (leaked: "
                  << (m_Stats.jointsCreated - m_Stats.jointsDestroyed)
                  << ")" << std::endl;
        std::cout << "  Audio Clips:      " << m_Stats.clipsLoaded
                  << " loaded, " << m_Stats.clipsReleased
                  << " released" << std::endl;
        std::cout << "  Total Collisions: " << m_Stats.totalCollisions << std::endl;

        bool leakFree = (m_Stats.ActiveAudioSources() == 0) &&
                        (m_Stats.ActiveAudioBuffers() == 0) &&
                        (m_Stats.ActivePhysicsBodies() == 0) &&
                        (m_Stats.ActiveJoints() == 0);
        if (leakFree) {
            std::cout << "  >>> Resource Check: PASSED (no leaks detected)" << std::endl;
        } else {
            std::cout << "  >>> Resource Check: WARNING (potential leaks detected)" << std::endl;
        }
        std::cout << "-------------------------------------" << std::endl;
    }

    // ============================================================
    // Update
    // ============================================================
    void AudioPhysicsSandboxApp::Update(float32 dt) {
        // ── 鼠标拖拽 ──
        HandleMouseDrag();

        // ── 键盘输入 ──
        HandleKeyboardInput();

        // ── 物理步进（固定步长） ──
        static float32 accumulator = 0.0f;
        accumulator += dt;
        while (accumulator >= FIXED_DT) {
            m_PhysicsWorld->Step(FIXED_DT, 8, 3);
            accumulator -= FIXED_DT;
        }

        // ── 回收已播放完毕的一次性音效 ──
        Audio::UpdateOneShots(*m_AudioEngine);

        // ── 更新音频引擎（后台处理） ──
        m_AudioEngine->Update();

        // ── FPS & 资源统计 ──
        m_FpsTimer += dt;
        m_FpsCount++;
        if (m_FpsTimer >= 1.0f) {
            m_CurrentFps = static_cast<float32>(m_FpsCount) / m_FpsTimer;
            PrintResourceStats();
            m_FpsTimer = 0.0f;
            m_FpsCount = 0;
        }
    }

    // ============================================================
    // Render
    // ============================================================
    void AudioPhysicsSandboxApp::Render() {
        auto* context = m_Window->GetContext();
        if (!context) return;

        context->ClearColor(0.08f, 0.08f, 0.12f, 1.0f);

        // ── 使用精灵批处理渲染 ──
        m_BatchShader->Bind();
        m_BatchShader->SetMat4("u_ViewProjection",
            m_Camera->GetViewProjectionMatrixPtr());

        m_SpriteBatch->Begin(m_Texture);

        // 我们使用物理调试绘制来渲染形状，所以不需要精灵渲染具体物体
        // 但为了视觉效果，可以画一些纹理

        m_SpriteBatch->End();

        // ── 物理调试绘制（显示碰撞体形状） ──
        if (m_DebugDraw) {
            m_DebugDraw->SetViewProjection(m_Camera->GetViewProjectionMatrixPtr());
            m_PhysicsWorld->DebugDraw();
        }
    }

    // ============================================================
    // Run — 主循环
    // ============================================================
    void AudioPhysicsSandboxApp::Run() {
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

    // ============================================================
    // PrintHelp
    // ============================================================
    void AudioPhysicsSandboxApp::PrintHelp() {
        std::cout << "\n==============================================" << std::endl;
        std::cout << "  Audio Physics Sandbox" << std::endl;
        std::cout << "==============================================" << std::endl;
        std::cout << "  【背景音乐】程序化生成的循环旋律" << std::endl;
        std::cout << "  【键盘触发音效】" << std::endl;
        std::cout << "    1: 钢琴 (C5)" << std::endl;
        std::cout << "    2: 鼓 (Kick)" << std::endl;
        std::cout << "    3: 贝斯 (C3)" << std::endl;
        std::cout << "    4: 扫弦 (Strum)" << std::endl;
        std::cout << "    5: 镲 (Cymbal)" << std::endl;
        std::cout << "    B: 暂停/恢复背景音乐" << std::endl;
        std::cout << "    Esc: 退出" << std::endl;
        std::cout << "  【鼠标拖拽】左键拖动物体，碰撞发声" << std::endl;
        std::cout << "  【材质类型】" << std::endl;
        std::cout << "    Stone (低频厚重)  — 暗红方块" << std::endl;
        std::cout << "    Wood  (中频清脆)  — 方块" << std::endl;
        std::cout << "    Metal (高频响亮)  — 方块" << std::endl;
        std::cout << "    Rubber (低沉弹软) — 方块" << std::endl;
        std::cout << "  【资源追踪】控制台每秒输出资源统计" << std::endl;
        std::cout << "  【内存验证】退出时打印资源释放详情" << std::endl;
        std::cout << "==============================================\n" << std::endl;
    }

} // namespace Engine
