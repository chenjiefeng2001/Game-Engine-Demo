#pragma once

/**
 * @file EngineSettings.h
 * @brief 引擎全局设置 — 图形、音频、物理等核心引擎参数
 *
 * 使用 Config 系统管理，内置不可变默认模板。
 * 全局设置通常保存到 "config/engine_settings.json"。
 */

#include "Engine/Core/Config.h"
#include "Engine/Types.h"
#include <string>

namespace Engine {

    class EngineSettings {
    public:
        EngineSettings();
        ~EngineSettings() = default;

        EngineSettings(const EngineSettings&) = delete;
        EngineSettings& operator=(const EngineSettings&) = delete;

        // ── 生命周期 ──

        /** 加载设置（若文件不存在则创建默认模板并保存） */
        bool Load(const std::string& filepath);

        /** 保存当前设置到文件 */
        bool Save(const std::string& filepath) const;

        /** 恢复所有设置为默认值 */
        void RestoreDefaults();

        // ── 图形设置 ──

        struct Graphics {
            int32  windowWidth      = 1280;
            int32  windowHeight     = 720;
            bool   fullscreen       = false;
            bool   vsync            = true;
            int32  targetFPS        = 60;
            float  renderScale      = 1.0f;
            bool   enablePostFX     = false;
            int32  shadowMapSize    = 1024;
        };

        Graphics GetGraphics() const;
        void     SetGraphics(const Graphics& gfx);

        // ── 音频设置 ──

        struct Audio {
            float32 masterVolume    = 1.0f;
            float32 musicVolume     = 0.8f;
            float32 sfxVolume       = 1.0f;
            int32   audioChannels   = 32;
        };

        Audio GetAudio() const;
        void  SetAudio(const Audio& audio);

        // ── 物理设置 ──

        struct Physics {
            int32   velocityIterations = 8;
            int32   positionIterations = 3;
            float32 gravityScale       = 1.0f;
        };

        Physics GetPhysics() const;
        void    SetPhysics(const Physics& physics);

    private:
        Config m_Config;
        Config m_Defaults;

        void BuildDefaults();
    };

} // namespace Engine
