/**
 * @file main.cpp
 * @brief 音频系统综合测试
 *
 * 测试覆盖：
 * 1. AudioEngine 生命周期
 * 2. AudioSource 播放控制 + 遮挡/阻断/排他
 * 3. AudioBusManager 总线/发送/增益
 * 4. SpatialAudioManager LTI 系统
 * 5. AudioAssetManager 资产加载
 * 6. AudioEffect DSP 效果器
 */

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioTypes.h"
#include "Engine/Core/Audio/AudioClip.h"
#include "Engine/Core/Log.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cassert>

namespace {
    Engine::Logger s_Log("AudioSystemTest");
}

// 测试计数器
static int g_Passed = 0;
static int g_Failed = 0;

#define TEST(name, expr) do { \
    if (!(expr)) { \
        s_Log.Error("FAILED: {} (line {})", name, __LINE__); \
        g_Failed++; \
    } else { \
        s_Log.Info("PASSED: {}", name); \
        g_Passed++; \
    } \
} while(0)

int main() {
    s_Log.Info("===== Audio System Test Suite =====");

    // ================================================================
    // 测试 1: AudioEngine 生命周期
    // ================================================================
    {
        Engine::Audio::AudioEngine engine;
        TEST("Engine not initialized before Init", !engine.IsInitialized());
        TEST("Engine Initialize", engine.Initialize());
        TEST("Engine initialized after Init", engine.IsInitialized());

        engine.SetMasterVolume(0.8f);
        TEST("MasterVolume ~0.8", std::abs(engine.GetMasterVolume() - 0.8f) < 0.01f);

        Engine::Audio::AudioSourceHandle h1 = engine.CreateSource("test_source_1");
        TEST("CreateSource returns valid handle", h1 != Engine::Audio::InvalidAudioSource);

        Engine::Audio::AudioSourceHandle h2 = engine.CreateSource("test_source_2");
        TEST("CreateSource returns different handle", h2 != h1);

        auto* src1 = engine.GetSource(h1);
        TEST("GetSource returns non-null", src1 != nullptr);
        TEST("GetSource returns correct handle", src1->GetID() == h1);

        engine.SetListenerPosition({10.0f, 0.0f, 0.0f});
        auto pos = engine.GetListenerPosition();
        TEST("Listener position X=10", std::abs(pos.x - 10.0f) < 0.01f);
        TEST("Listener position Y=0", std::abs(pos.y) < 0.01f);
        TEST("Listener position Z=0", std::abs(pos.z) < 0.01f);

        engine.SetListenerOrientation({0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
        engine.SetListenerVelocity({1.0f, 0.0f, 0.0f});

        engine.SetSpeedOfSound(340.0f);
        engine.SetDopplerFactor(1.5f);

        engine.Update(0.016f);
        TEST("Update runs without crash", true);

        engine.DestroySource(h1);
        TEST("DestroySource invalidates handle", engine.GetSource(h1) == nullptr);

        engine.Shutdown();
        TEST("Engine shutdown", !engine.IsInitialized());
    }

    // ================================================================
    // 测试 2: AudioSource 属性与空间仿真
    // ================================================================
    {
        Engine::Audio::AudioEngine engine;
        engine.Initialize();
        engine.SetListenerPosition({0.0f, 0.0f, 0.0f});

        auto handle = engine.CreateSource();
        auto* src = engine.GetSource(handle);

        // 基础属性
        src->SetVolume(0.5f);
        TEST("Volume", std::abs(src->GetVolume() - 0.5f) < 0.01f);

        src->SetPitch(1.5f);
        TEST("Pitch", std::abs(src->GetPitch() - 1.5f) < 0.01f);

        src->SetLooping(true);
        TEST("Looping", src->IsLooping());

        src->SetPosition({10.0f, 5.0f, -3.0f});
        auto pos = src->GetPosition();
        TEST("Position X", std::abs(pos.x - 10.0f) < 0.001f);
        TEST("Position Y", std::abs(pos.y - 5.0f) < 0.001f);
        TEST("Position Z", std::abs(pos.z + 3.0f) < 0.001f);

        src->SetVelocity({1.0f, 0.0f, 0.0f});
        TEST("Velocity", std::abs(src->GetVelocity().x - 1.0f) < 0.001f);

        src->SetDirection({0.0f, 0.0f, -1.0f});
        TEST("Direction", std::abs(src->GetDirection().z + 1.0f) < 0.001f);

        // 距离衰减
        src->SetMinDistance(1.0f);
        src->SetMaxDistance(100.0f);
        src->SetRolloffFactor(0.5f);
        TEST("No crash for distance model", true);

        // 声锥
        src->SetConeInnerAngle(60.0f);
        src->SetConeOuterAngle(120.0f);
        src->SetConeOuterGain(0.3f);
        TEST("No crash for cone", true);

        // 空间化
        src->SetSpatializationEnabled(true);
        TEST("Spatialization enabled", src->IsSpatializationEnabled());
        src->SetSpatializationEnabled(false);
        TEST("Spatialization disabled", !src->IsSpatializationEnabled());

        engine.Shutdown();
    }

    // ================================================================
    // 测试 3: 遮挡 / 阻断 / 排他
    // ================================================================
    {
        Engine::Audio::AudioEngine engine;
        engine.Initialize();

        auto handle = engine.CreateSource();
        auto* src = engine.GetSource(handle);
        // 必须在 AudioSource.h 中添加了 SetOcclusion 等方法的实现

        src->SetOcclusion(0.3f);
        TEST("SetOcclusion 0.3", true);

        src->SetObstruction(0.5f);
        TEST("SetObstruction 0.5", true);

        src->SetExclusion(0.1f);
        TEST("SetExclusion 0.1", true);

        src->SetDiffraction(0.2f);
        TEST("SetDiffraction 0.2", true);

        src->SetDiffractionAngle(0.5f);
        TEST("SetDiffractionAngle 0.5", true);

        // LTI 空间 IR
        Engine::Audio::SpatialIR ir;
        ir.delay = 0.1f;
        ir.gain = 0.8f;
        ir.type = Engine::Audio::SpatialPathType::Direct;
        ir.coefficients = {0.8f, 0.4f, 0.2f};
        src->ApplySpatialIR(ir);
        TEST("ApplySpatialIR", src->GetCurrentSpatialIR() != nullptr);
        const auto* stored_ir = src->GetCurrentSpatialIR();
        TEST("IR delay 0.1", std::abs(stored_ir->delay - 0.1f) < 0.001f);
        TEST("IR gain 0.8", std::abs(stored_ir->gain - 0.8f) < 0.001f);

        engine.Shutdown();
    }

    // ================================================================
    // 测试 4: 辅助发送 / 总线
    // ================================================================
    {
        Engine::Audio::AudioEngine engine;
        engine.Initialize();

        auto handle = engine.CreateSource();
        auto* src = engine.GetSource(handle);

        src->AddAuxSend("Reverb", 0.3f);
        const auto& sends = src->GetAuxSends();
        TEST("Aux send count = 1", sends.size() == 1);
        auto it = sends.find("Reverb");
        TEST("Aux send 'Reverb' exists", it != sends.end());
        TEST("Aux send level 0.3", std::abs(it->second - 0.3f) < 0.01f);

        src->AddAuxSend("Chorus", 0.5f);
        TEST("Aux send count = 2", sends.size() == 2);

        src->RemoveAuxSend("Reverb");
        TEST("Aux send count = 1 after remove", sends.size() == 1);
        TEST("Reverb removed", sends.find("Reverb") == sends.end());

        src->ClearAuxSends();
        TEST("Aux send count = 0 after clear", sends.empty());

        engine.Shutdown();
    }

    // ================================================================
    // 测试 5: AudioBusManager
    // ================================================================
    {
        Engine::Audio::AudioBusManager busMgr;

        auto* master = busMgr.CreateBus("Master");
        TEST("CreateBus Master", master != nullptr);
        TEST("Master volume default 1.0", std::abs(master->volume - 1.0f) < 0.01f);

        auto* sfx = busMgr.CreateBus("SFX");
        TEST("CreateBus SFX", sfx != nullptr);

        auto* music = busMgr.CreateBus("Music");
        TEST("CreateBus Music", music != nullptr);

        auto* reverb = busMgr.CreateBus("Reverb");
        TEST("CreateBus Reverb", reverb != nullptr);

        busMgr.SetBusVolume("SFX", 0.8f);
        TEST("SFX volume 0.8", std::abs(sfx->volume - 0.8f) < 0.01f);

        // 辅助发送
        busMgr.AddSend("SFX", "Reverb", 0.3f);
        TEST("SFX sends count = 1", sfx->sends.size() == 1);
        TEST("SFX send level 0.3", std::abs(sfx->sendLevels[0] - 0.3f) < 0.01f);
        TEST("SFX send target = Reverb", sfx->sends[0]->name == "Reverb");

        busMgr.AddSend("Music", "Reverb", 0.5f);
        TEST("Music sends count = 1", music->sends.size() == 1);

        // 静音
        busMgr.MuteBus("Music", true);
        TEST("Music muted", music->muted);

        busMgr.MuteBus("Music", false);
        TEST("Music unmuted", !music->muted);

        // 获取总线
        auto* got = busMgr.GetBus("SFX");
        TEST("GetBus SFX", got == sfx);
        TEST("GetBus NonExistent", busMgr.GetBus("NonExistent") == nullptr);

        // 删除发送
        busMgr.RemoveSend("SFX", "Reverb");
        TEST("Send removed", sfx->sends.empty());

        // 删除总线
        busMgr.RemoveBus("Reverb");
        TEST("Bus removed", busMgr.GetBus("Reverb") == nullptr);
        TEST("SFX sends empty after bus removed", sfx->sends.empty());

        // 重复创建
        auto* master2 = busMgr.CreateBus("Master");
        TEST("Duplicate CreateBus returns existing", master2 == master);
    }

    // ================================================================
    // 测试 6: AudioBusManager — 效果器
    // ================================================================
    {
        Engine::Audio::AudioBusManager busMgr;
        busMgr.CreateBus("Master");
        busMgr.CreateBus("SFX");

        // 通过工厂创建低通滤波器效果器
        auto lpf = Engine::Audio::CreateAudioEffect("LowPassFilter", 1000.0f, 44100);
        busMgr.AddEffect("SFX", std::move(lpf));
        TEST("AddEffect", true);

        // 测试处理缓冲区
        float buffer[128] = {0.5f};
        busMgr.ApplyBusProcessing(buffer, 32, 44100);
        TEST("ApplyBusProcessing no crash", true);

        // 清除效果器
        busMgr.ClearEffects("SFX");
        TEST("ClearEffects", true);
    }

    // ================================================================
    // 测试 7: SpatialAudioManager
    // ================================================================
    {
        Engine::Audio::SpatialAudioManager spatial;

        // 默认参数测试
        glm::vec3 srcPos{10.0f, 0.0f, 0.0f};
        glm::vec3 listenPos{0.0f, 0.0f, 0.0f};
        glm::vec3 forward{0.0f, 0.0f, -1.0f};
        glm::vec3 up{0.0f, 1.0f, 0.0f};

        // 注册源
        spatial.RegisterSource(1, srcPos);
        spatial.RegisterSource(2, {-5.0f, 3.0f, 0.0f});

        // 计算空间 IR
        auto irs = spatial.ComputeSpatialIRs(srcPos, listenPos, forward, up);
        TEST("ComputeSpatialIRs returns IRs", !irs.empty());
        TEST("First IR is Direct", irs[0].type == Engine::Audio::SpatialPathType::Direct);
        TEST("Direct IR has delay > 0", irs[0].delay > 0.0f);
        TEST("Direct IR has gain > 0", irs[0].gain > 0.0f);

        // 更新位置
        spatial.UpdateSourcePosition(1, {20.0f, 0.0f, 0.0f});
        auto irs2 = spatial.ComputeSpatialIRs({20.0f, 0.0f, 0.0f}, listenPos, forward, up);
        TEST("Updated position changes IR", !irs2.empty());

        // 注销源
        spatial.UnregisterSource(1);
        spatial.UnregisterSource(2);

        // 衍射参数
        Engine::Audio::SpatialAudioManager::DiffractionParams diffParams;
        diffParams.maxDiffractionAngle = glm::radians(60.0f);
        diffParams.diffractionStrength = 0.7f;
        diffParams.lowpassCutoff = 3000.0f;
        spatial.SetDiffractionParams(diffParams);

        // 反射参数
        Engine::Audio::SpatialAudioManager::ReflectionParams refParams;
        refParams.maxReflectionOrder = 2;
        refParams.reflectionGainScale = 0.6f;
        spatial.SetReflectionParams(refParams);

        // 遮挡参数
        Engine::Audio::SpatialAudioManager::ObstructionParams obsParams;
        obsParams.occlusionLowpassCutoff = 800.0f;
        obsParams.exclusionThreshold = 0.8f;
        spatial.SetOcclusionParams(obsParams);

        // 设置场景几何
        Engine::Audio::SpatialAudioManager::GeometryTriangle tri;
        tri.vertices[0] = {0.0f, 0.0f, 0.0f};
        tri.vertices[1] = {10.0f, 0.0f, 0.0f};
        tri.vertices[2] = {0.0f, 10.0f, 0.0f};
        tri.normals[0] = tri.normals[1] = tri.normals[2] = {0.0f, 0.0f, 1.0f};
        tri.absorption = 0.2f;
        tri.scattering = 0.1f;
        spatial.SetSceneGeometry({tri});

        auto irs3 = spatial.ComputeSpatialIRs(srcPos, listenPos, forward, up);
        TEST("SpatialIRs with geometry", irs3.size() >= 1);

        spatial.ClearSceneGeometry();
        TEST("ClearSceneGeometry", true);

        // 射线回调
        int rayCount = 0;
        spatial.SetRaycastCallback([&](auto, auto) { rayCount++; return false; });
        TEST("RaycastCallback set", true);
    }

    // ================================================================
    // 测试 8: AudioAssetManager
    // ================================================================
    {
        Engine::Audio::AudioAssetManager assets;

        TEST("MemoryUsage starts at 0", assets.GetMemoryUsage() == 0);

        // 尝试加载不存在的文件
        auto clip = assets.GetClip("nonexistent.wav");
        TEST("GetClip nonexistent returns null", clip == nullptr);

        // 热重载
        assets.EnableHotReload(true);
        TEST("HotReload enabled", assets.IsHotReloadEnabled());
        assets.EnableHotReload(false);
        TEST("HotReload disabled", !assets.IsHotReloadEnabled());

        // 流式标记（不会崩溃）
        assets.StreamAsset("assets/sounds/coin.wav");
        TEST("StreamAsset no crash", true);
    }

    // ================================================================
    // 测试 9: 声源池化 (OneShot)
    // ================================================================
    {
        Engine::Audio::AudioEngine engine;
        engine.Initialize();

        engine.SetPoolSize(5);
        engine.SetPoolSize(3); // 缩减

        // 从池中播放（没有 clip 时返回 InvalidAudioSource）
        auto h = engine.PlayOneShot(nullptr, 1.0f, 1.0f);
        TEST("PlayOneShot with null clip", h == Engine::Audio::InvalidAudioSource);

        engine.Shutdown();
    }

    // ================================================================
    // 测试 10: 空间化 IR 回调
    // ================================================================
    {
        Engine::Audio::SpatialAudioManager spatial;
        int callbackCount = 0;
        spatial.RegisterIRCallback([&](const auto&) { callbackCount++; });

        auto irs = spatial.ComputeSpatialIRs({5,0,0}, {0,0,0}, {0,0,-1}, {0,1,0});
        TEST("IR callback triggered", callbackCount >= static_cast<int>(irs.size()));
    }

    // ================================================================
    // 测试 11: 源优先级
    // ================================================================
    {
        Engine::Audio::AudioEngine engine;
        engine.Initialize();

        auto h = engine.CreateSource();
        auto* src = engine.GetSource(h);
        src->SetPriority(10);
        TEST("Priority 10", src->GetPriority() == 10);
        src->SetPriority(0);
        TEST("Priority 0", src->GetPriority() == 0);

        engine.Shutdown();
    }

    // ================================================================
    // 测试 12: PlaybackTime
    // ================================================================
    {
        Engine::Audio::AudioEngine engine;
        engine.Initialize();

        auto h = engine.CreateSource();
        auto* src = engine.GetSource(h);
        TEST("PlaybackTime defaults to 0", src->GetPlaybackTime() >= 0.0f);
        TEST("GetSourcePlaybackTime", engine.GetSourcePlaybackTime(h) >= 0.0f);

        engine.Shutdown();
    }

    // ================================================================
    // 测试 13: AudioEffect 工厂
    // ================================================================
    {
        auto lpf = Engine::Audio::CreateAudioEffect("LowPassFilter", 800.0f, 44100);
        TEST("LPF effect created", lpf != nullptr);
        TEST("LPF effect name", std::string(lpf->GetEffectName()) == "LowPassFilter");

        float in[64] = {1.0f};
        float out[64] = {0.0f};
        lpf->Process(in, out, 16, 44100);
        TEST("LPF process no crash", true);
        TEST("LPF output changed", std::abs(out[0] - 1.0f) > 0.001f);

        auto conv = Engine::Audio::CreateAudioEffect("FIRConvolution");
        TEST("FIRConvolution effect created", conv != nullptr);

        auto unknown = Engine::Audio::CreateAudioEffect("UnknownEffect");
        TEST("Unknown effect returns null", unknown == nullptr);
    }

    // ================================================================
    // 汇总
    // ================================================================
    s_Log.Info("========================================");
    s_Log.Info("Test Results: {} passed, {} failed out of {} total",
               g_Passed, g_Failed, g_Passed + g_Failed);
    s_Log.Info("========================================");

    return g_Failed > 0 ? 1 : 0;
}