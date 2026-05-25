#include "Engine/Audio/AudioSystem.h"
#include <iostream>
#include <algorithm>

// OpenAL 头文件（用于绑定缓冲区和查询播放状态）
#include <AL/al.h>
#include <AL/alc.h>

namespace Engine { namespace Audio {

    // ============================================================
    // 内部：一次性音效跟踪条目
    // ============================================================
    struct OneShotEntry {
        std::shared_ptr<IAudioSource> source;
        AudioClip*                    clip = nullptr;  // 仅引用，不拥有
    };

    // 全局活跃列表
    static std::vector<OneShotEntry> s_ActiveOneShots;

    // ============================================================
    // PlayOneShot — 播放一次性音效
    // ============================================================

    void PlayOneShot(IAudioEngine& engine, AudioClip& clip,
                     const Vec3& position) {
        if (!clip.IsValid()) {
            std::cerr << "[Audio::PlayOneShot] AudioClip is not valid" << std::endl;
            return;
        }

        // 创建临时音源
        auto source = engine.CreateSource();
        if (!source) {
            std::cerr << "[Audio::PlayOneShot] Failed to create audio source"
                      << std::endl;
            return;
        }

        // 绑定 AudioClip 的 OpenAL 缓冲区
        ALuint bufferHandle = clip.GetBufferHandle();
        ALuint sourceHandle = source->GetNativeHandle();
        alSourcei(sourceHandle, AL_BUFFER, static_cast<ALint>(bufferHandle));

        ALenum err = alGetError();
        if (err != AL_NO_ERROR) {
            std::cerr << "[Audio::PlayOneShot] Failed to bind buffer (error: "
                      << err << ")" << std::endl;
            engine.DestroySource(source.get());
            return;
        }

        // 设置 3D 位置
        source->SetPosition(position);

        // 一次性音效：非循环、默认音量/音调
        source->SetLooping(false);
        source->SetVolume(1.0f);
        source->SetPitch(1.0f);

        // 开始播放（传 nullptr 因为 buffer 已通过 alSourcei 绑定）
        source->Play(nullptr);

        // 加入跟踪列表
        s_ActiveOneShots.push_back({std::move(source), &clip});
    }

    // ============================================================
    // UpdateOneShots — 回收已完成的音源
    // ============================================================

    void UpdateOneShots(IAudioEngine& engine) {
        if (s_ActiveOneShots.empty()) return;

        auto it = s_ActiveOneShots.begin();
        while (it != s_ActiveOneShots.end()) {
            bool finished = false;

            if (!it->source) {
                finished = true;
            } else {
                // 查询播放状态
                PlayState state = it->source->GetState();
                if (state == PlayState::Stopped || state == PlayState::Initial) {
                    finished = true;
                }
            }

            if (finished) {
                // 销毁音源释放 OpenAL 资源
                if (it->source) {
                    engine.DestroySource(it->source.get());
                }
                it = s_ActiveOneShots.erase(it);
            } else {
                ++it;
            }
        }
    }

    // ============================================================
    // StopAllOneShots — 停止并回收所有
    // ============================================================

    void StopAllOneShots(IAudioEngine& engine) {
        for (auto& entry : s_ActiveOneShots) {
            if (entry.source) {
                entry.source->Stop();
                engine.DestroySource(entry.source.get());
            }
        }
        s_ActiveOneShots.clear();
    }

    // ============================================================
    // GetActiveOneShotCount — 当前活跃数量
    // ============================================================

    int32 GetActiveOneShotCount() {
        return static_cast<int32>(s_ActiveOneShots.size());
    }

}} // namespace Engine::Audio
