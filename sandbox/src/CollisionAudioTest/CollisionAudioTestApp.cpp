#include "CollisionAudioTestApp.h"
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
#include <iostream>
#include <cmath>
#include <cstring>

namespace Engine {

    // ============================================================
    // 工具：生成内存 WAV 文件（PCM 16-bit 单声道）
    //
    // WAV 文件结构：
    //   RIFF header (12 bytes)
    //   fmt  chunk (24 bytes)
    //   data chunk (8 bytes + data)
    // ============================================================

    struct WavHeader {
        // RIFF
        char     riff[4]     = {'R', 'I', 'F', 'F'};
        uint32_t fileSize    = 0;  // 总大小 - 8
        char     wave[4]     = {'W', 'A', 'V', 'E'};
        // fmt 子块
        char     fmtId[4]    = {'f', 'm', 't', ' '};
        uint32_t fmtSize     = 16;
        uint16_t audioFormat = 1;  // PCM
        uint16_t numChannels = 1;  // 单声道
        uint32_t sampleRate  = 44100;
        uint32_t byteRate    = 88200;  // sampleRate * channels * bytesPerSample
        uint16_t blockAlign  = 2;      // channels * bytesPerSample
        uint16_t bitsPerSample = 16;
        // data 子块
        char     dataId[4]   = {'d', 'a', 't', 'a'};
        uint32_t dataSize    = 0;
    };

    std::vector<uint8> CollisionAudioTestApp::GenerateImpactWav(MaterialType material) {
        // 各材质的音色参数
        float32 baseFreq   = 0.0f;
        float32 duration   = 0.0f;
        float32 harmonics  = 0.0f;  // 谐波强度

        switch (material) {
            case MaterialType::Stone:
                baseFreq  = 180.0f;   // 低频厚重
                duration  = 0.25f;
                harmonics = 0.6f;
                break;
            case MaterialType::Wood:
                baseFreq  = 400.0f;   // 中频清脆
                duration  = 0.15f;
                harmonics = 0.3f;
                break;
            case MaterialType::Metal:
                baseFreq  = 800.0f;   // 高频响亮
                duration  = 0.40f;
                harmonics = 0.8f;
                break;
            default:
                baseFreq  = 300.0f;
                duration  = 0.2f;
                harmonics = 0.4f;
                break;
        }

        // 计算采样参数
        int32 sampleRate  = 44100;
        int32 totalSamples = static_cast<int32>(sampleRate * duration);
        int32 dataBytes    = totalSamples * sizeof(int16);

        // 分配缓冲区
        WavHeader header;
        header.sampleRate  = sampleRate;
        header.byteRate    = sampleRate * 2;  // 16-bit mono
        header.dataSize    = dataBytes;
        header.fileSize    = dataBytes + sizeof(WavHeader) - 8;

        std::vector<uint8> wavData(sizeof(WavHeader) + dataBytes);
        std::memcpy(wavData.data(), &header, sizeof(WavHeader));

        int16* samples = reinterpret_cast<int16*>(wavData.data() + sizeof(WavHeader));

        // 生成冲击音效：频率衰减 + 指数包络
        for (int32 i = 0; i < totalSamples; ++i) {
            float32 t = static_cast<float32>(i) / sampleRate;

            // 指数衰减包络
            float32 envelope = std::exp(-6.0f * t / duration);

            // 基频 + 谐波
            float32 sample = std::sin(2.0f * 3.14159f * baseFreq * t);
            sample += harmonics * std::sin(2.0f * 3.14159f * baseFreq * 2.41f * t);
            sample += harmonics * 0.5f * std::sin(2.0f * 3.14159f * baseFreq * 5.73f * t);

            // 归一化并应用包络
            float32 gain = 0.7f;
            sample = sample * envelope * gain;

            // 裁剪
            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;

            samples[i] = static_cast<int16>(sample * 32767.0f);
        }

        return wavData;
    }

    // ============================================================
    // 构造
    // ============================================================

    CollisionAudioTestApp::CollisionAudioTestApp(IGraphicsFactory& factory)
        : m_Factory(factory)
        , m_TextureManager(factory)
    {
        // ── 1. 创建窗口 ──
        m_Window = m_Factory.CreateWindow(m_WindowWidth, m_WindowHeight,
                                          "Collision Audio Test");

        m_Window->SetEventCallback([this](Event& e) {
            if (e.type == EventType::WindowResize) {
                m_WindowWidth  = e.resize.width;
                m_WindowHeight = e.resize.height;

                float32 aspect = static_cast<float32>(m_WindowWidth) /
                                 static_cast<float32>(m_WindowHeight);
                float32 viewHeight = 12.0f;
                float32 viewWidth  = viewHeight * aspect;
                m_Camera = OrthographicCamera(
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
        m_Camera = OrthographicCamera(
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
        m_AudioEngine = std::make_shared<OpenALAudioEngine>();
        if (!m_AudioEngine->Init()) {
            std::cerr << "[CollisionAudioTest] Failed to init audio engine" << std::endl;
        }

        // ── 5. 生成材质冲击音效 ──
        SetupAudio();

        // ── 6. 创建物理世界 ──
        SetupPhysicsWorld();

        // ── 7. 生成测试物体 ──
        SpawnTestBodies();

        PrintHelp();
    }

    CollisionAudioTestApp::~CollisionAudioTestApp() {
        if (m_AudioEngine) {
            Audio::StopAllOneShots(*m_AudioEngine);
            m_AudioEngine->Shutdown();
        }
    }

    // ============================================================
    // SetupAudio — 生成并缓存各材质冲击音效
    // ============================================================

    void CollisionAudioTestApp::SetupAudio() {
        // 为每种材质生成 WAV 并加载到 AudioClip
        for (int32 i = 0; i < static_cast<int32>(MaterialType::Count); ++i) {
            MaterialType mat = static_cast<MaterialType>(i);
            std::vector<uint8> wavData = GenerateImpactWav(mat);

            auto clip = std::make_shared<AudioClip>();
            // LoadFromMemory 需要 WAV 格式数据（含头）
            clip->LoadFromMemory(wavData.data(), wavData.size(), "wav");

            if (clip->IsValid()) {
                m_ImpactClips[mat] = clip;
                std::cout << "[Audio] Generated " << GetMaterialName(mat)
                          << " impact sound (" << (wavData.size() / 1024)
                          << " KB)" << std::endl;
            }
        }
    }

    // ============================================================
    // SetupPhysicsWorld — 创建物理世界 + 碰撞回调
    // ============================================================

    void CollisionAudioTestApp::SetupPhysicsWorld() {
        m_PhysicsWorld = std::make_shared<Box2DPhysicsWorld>(
            Vec2(0.0f, -9.81f)  // 地球重力
        );

        // 注册碰撞回调 — 碰撞时播放音效
        m_PhysicsWorld->SetContactBeginCallback(
            [this](const ContactInfo& info) {
                OnCollision(info);
            }
        );

        // 调试绘制
        auto* renderCtx = m_Window->GetContext();
        auto& glContext = static_cast<OpenGLContext*>(renderCtx)->GetGL();
        m_DebugDraw = std::make_unique<OpenGLPhysicsDebugDraw>(glContext);
        m_PhysicsWorld->SetDebugDraw(m_DebugDraw.get());
    }

    // ============================================================
    // SpawnTestBodies — 创建地面和不同材质的测试物体
    // ============================================================

    void CollisionAudioTestApp::SpawnTestBodies() {
        // ── 地面（无材质，不发声） ──
        {
            BodyDef def;
            def.type = BodyType::Static;
            def.position = Vec2(0.0f, -5.0f);
            def.shape.type = ShapeType::Box;
            def.shape.boxSize = Vec2(8.0f, 0.5f);
            def.friction = 0.5f;
            def.userData = nullptr;  // 无材质
            m_PhysicsWorld->CreateBody(def);
        }

        // ── Stone 方块 ──
        {
            BodyDef def;
            def.type = BodyType::Dynamic;
            def.position = Vec2(-3.0f, 2.0f);
            def.shape.type = ShapeType::Box;
            def.shape.boxSize = Vec2(0.45f, 0.45f);
            def.density  = 2.0f;
            def.friction = 0.8f;
            def.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(
                static_cast<uint8>(MaterialType::Stone)));
            m_PhysicsWorld->CreateBody(def);
        }

        // ── Wood 方块 ──
        {
            BodyDef def;
            def.type = BodyType::Dynamic;
            def.position = Vec2(0.0f, 4.0f);
            def.shape.type = ShapeType::Box;
            def.shape.boxSize = Vec2(0.45f, 0.45f);
            def.density  = 0.8f;
            def.friction = 0.6f;
            def.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(
                static_cast<uint8>(MaterialType::Wood)));
            m_PhysicsWorld->CreateBody(def);
        }

        // ── Metal 方块 ──
        {
            BodyDef def;
            def.type = BodyType::Dynamic;
            def.position = Vec2(3.0f, 6.0f);
            def.shape.type = ShapeType::Box;
            def.shape.boxSize = Vec2(0.45f, 0.45f);
            def.density  = 3.0f;
            def.friction = 0.4f;
            def.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(
                static_cast<uint8>(MaterialType::Metal)));
            m_PhysicsWorld->CreateBody(def);
        }

        // ── 堆叠更多不同材质的圆球 ──
        const MaterialType stackMats[] = {
            MaterialType::Stone, MaterialType::Wood, MaterialType::Metal
        };
        for (int32 i = 0; i < 6; ++i) {
            MaterialType mat = stackMats[i % 3];
            BodyDef def;
            def.type = BodyType::Dynamic;
            def.position = Vec2(-1.5f + i * 0.6f, 8.0f + i * 0.8f);
            def.shape.type = ShapeType::Circle;
            def.shape.circleRadius = 0.35f;
            def.density  = 1.0f + static_cast<float32>(i % 3) * 0.5f;
            def.friction = 0.5f;
            def.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(
                static_cast<uint8>(mat)));
            m_PhysicsWorld->CreateBody(def);
        }

        std::cout << "[Scene] Spawned " << (1 + 3 + 6)
                  << " physics bodies with mixed materials" << std::endl;
    }

    // ============================================================
    // OnCollision — 碰撞事件处理：播放对应材质的音效
    // ============================================================

    void CollisionAudioTestApp::OnCollision(const ContactInfo& info) {
        if (!m_AudioEngine) return;

        // ContactInfo::bodyA/bodyB 是 const void*，实际指向 const IPhysicsBody*
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

        // 如果两个物体都无材质，跳过
        if (matA == MaterialType::Count && matB == MaterialType::Count) {
            return;
        }

        // 选择主材质
        MaterialType primaryMat = (matA != MaterialType::Count) ? matA : matB;
        if (matA != MaterialType::Count && matB != MaterialType::Count) {
            // 两个都有材质时，取枚举值较大的（Metal > Wood > Stone）
            primaryMat = static_cast<MaterialType>(
                std::max(static_cast<uint8>(matA), static_cast<uint8>(matB)));
        }

        // 查找对应的音效 clip
        auto it = m_ImpactClips.find(primaryMat);
        if (it == m_ImpactClips.end() || !it->second->IsValid()) return;

        // 使用 One-Shot 播放碰撞音效
        Vec3 impactPos(info.point.x, info.point.y, 0.0f);
        Audio::PlayOneShot(*m_AudioEngine, *it->second, impactPos);

        m_TotalCollisions++;
    }

    // ============================================================
    // Update
    // ============================================================

    void CollisionAudioTestApp::Update(float32 dt) {
        // ── 物理步进 ──
        m_PhysicsWorld->Step(dt, 8, 3);

        // ── 回收已播放完毕的一次性音效 ──
        Audio::UpdateOneShots(*m_AudioEngine);

        // ── FPS 统计 ──
        m_FpsTimer += dt;
        m_FpsCount++;
        if (m_FpsTimer >= 1.0f) {
            m_CurrentFps = static_cast<float32>(m_FpsCount) / m_FpsTimer;
            std::cout << "\r[Stats] FPS: " << m_CurrentFps
                      << "  Collisions: " << m_TotalCollisions
                      << "  Active sounds: " << Audio::GetActiveOneShotCount()
                      << "    " << std::flush;
            m_FpsTimer = 0.0f;
            m_FpsCount = 0;
        }
    }

    // ============================================================
    // Render
    // ============================================================

    void CollisionAudioTestApp::Render() {
        auto* context = m_Window->GetContext();
        if (!context) return;

        context->ClearColor(0.1f, 0.1f, 0.12f, 1.0f);

        // 精灵渲染
        m_SpriteBatch->Begin(m_Texture);
        m_SpriteBatch->Flush();

        // 物理调试绘制
        m_DebugDraw->SetViewProjection(m_Camera.GetViewProjectionMatrixPtr());
        m_PhysicsWorld->DebugDraw();
    }

    // ============================================================
    // Run — 主循环
    // ============================================================

    void CollisionAudioTestApp::Run() {
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

    void CollisionAudioTestApp::PrintHelp() {
        std::cout << "\n==============================================" << std::endl;
        std::cout << "  Collision Audio Test" << std::endl;
        std::cout << "==============================================" << std::endl;
        std::cout << "  3D物理碰撞音效测试" << std::endl;
        std::cout << "  材质类型：" << std::endl;
        std::cout << "    Stone (低频厚重)  — 方块" << std::endl;
        std::cout << "    Wood  (中频清脆)  — 方块" << std::endl;
        std::cout << "    Metal (高频响亮)  — 方块" << std::endl;
        std::cout << "  不同材质碰撞会自动播放对应音效" << std::endl;
        std::cout << "  所有音效由程序实时生成，无需外部音频文件" << std::endl;
        std::cout << "==============================================\n" << std::endl;
    }

} // namespace Engine
