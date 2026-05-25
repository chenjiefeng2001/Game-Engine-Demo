#pragma once

/**
 * @file AudioSystem.h
 * @brief 音频系统工具 — 快速播放、一次性音效管理
 *
 * 提供便捷的静态方法用于快速播放一次性音效（One-Shot），
 * 播放完成后自动回收音源资源，无需手动管理生命周期。
 *
 * 使用方式：
 * @code
 *   // 一次性播放（最简用法）
 *   Audio::PlayOneShot(engine, clip);
 *
 *   // 带 3D 位置
 *   Audio::PlayOneShot(engine, clip, Vec3(10.0f, 0.0f, 0.0f));
 *
 *   // 每帧更新（回收已播放完的音源）
 *   Audio::UpdateOneShots(engine);
 *
 *   // 停止所有一次性音效
 *   Audio::StopAllOneShots(engine);
 * @endcode
 */

#include "Engine/Core/Audio/IAudioEngine.h"
#include "Engine/Core/Audio/AudioClip.h"
#include <memory>
#include <vector>

namespace Engine { namespace Audio {

    /**
     * @brief 播放一次性音效（One-Shot）
     * @param engine  音频引擎（必须已 Init）
     * @param clip    音频资源（必须已 LoadFromFile/LoadFromMemory）
     * @param position 世界坐标位置（可选，默认原点）
     *
     * 内部创建临时 IAudioSource，播放后自动加入跟踪列表。
     * 需要调用 UpdateOneShots() 来回收已完成播放的音源。
     *
     * @note 如果 clip 或 engine 无效，静默忽略。
     */
    void PlayOneShot(class IAudioEngine& engine, class AudioClip& clip,
                     const Vec3& position = Vec3(0.0f, 0.0f, 0.0f));

    /**
     * @brief 更新并回收已播放完的一次性音效
     * @param engine 音频引擎（用于销毁已完成的音源）
     *
     * 建议每帧调用一次。内部检查所有活跃的一次性音源状态，
     * 将已播放完毕的自动销毁并回收。
     */
    void UpdateOneShots(class IAudioEngine& engine);

    /**
     * @brief 停止并回收所有活跃的一次性音效
     * @param engine 音频引擎
     */
    void StopAllOneShots(class IAudioEngine& engine);

    /**
     * @brief 获取当前活跃的一次性音效数量
     */
    int32 GetActiveOneShotCount();

}} // namespace Engine::Audio
