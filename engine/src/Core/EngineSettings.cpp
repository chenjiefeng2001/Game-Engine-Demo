#include "Engine/Core/EngineSettings.h"
#include "Engine/Core/Log.h"

namespace {
    Engine::Logger s_Log("EngineSettings");
}

namespace Engine {

    EngineSettings::EngineSettings() {
        BuildDefaults();
        m_Config.SetDefaults(m_Defaults);
    }

    void EngineSettings::BuildDefaults() {
        // ── Graphics ──
        m_Defaults.SetInt("Graphics", "windowWidth", 1280);
        m_Defaults.SetInt("Graphics", "windowHeight", 720);
        m_Defaults.SetBool("Graphics", "fullscreen", false);
        m_Defaults.SetBool("Graphics", "vsync", true);
        m_Defaults.SetInt("Graphics", "targetFPS", 60);
        m_Defaults.SetFloat("Graphics", "renderScale", 1.0f);
        m_Defaults.SetBool("Graphics", "enablePostFX", false);
        m_Defaults.SetInt("Graphics", "shadowMapSize", 1024);

        // ── Audio ──
        m_Defaults.SetFloat("Audio", "masterVolume", 1.0f);
        m_Defaults.SetFloat("Audio", "musicVolume", 0.8f);
        m_Defaults.SetFloat("Audio", "sfxVolume", 1.0f);
        m_Defaults.SetInt("Audio", "audioChannels", 32);

        // ── Physics ──
        m_Defaults.SetInt("Physics", "velocityIterations", 8);
        m_Defaults.SetInt("Physics", "positionIterations", 3);
        m_Defaults.SetFloat("Physics", "gravityScale", 1.0f);
    }

    bool EngineSettings::Load(const std::string& filepath) {
        bool loaded = m_Config.Load(filepath);
        if (!loaded) {
            // 文件不存在或加载失败，用默认值创建初始文件
            RestoreDefaults();
            Save(filepath);
            s_Log.Info("Created default: {}", filepath);
        }
        return true;
    }

    bool EngineSettings::Save(const std::string& filepath) const {
        return m_Config.Save(filepath);
    }

    void EngineSettings::RestoreDefaults() {
        m_Config.RestoreDefaults();
    }

    // ── Graphics ──

    EngineSettings::Graphics EngineSettings::GetGraphics() const {
        Graphics g;
        g.windowWidth      = m_Config.GetInt("Graphics", "windowWidth", g.windowWidth);
        g.windowHeight     = m_Config.GetInt("Graphics", "windowHeight", g.windowHeight);
        g.fullscreen       = m_Config.GetBool("Graphics", "fullscreen", g.fullscreen);
        g.vsync            = m_Config.GetBool("Graphics", "vsync", g.vsync);
        g.targetFPS        = m_Config.GetInt("Graphics", "targetFPS", g.targetFPS);
        g.renderScale      = m_Config.GetFloat("Graphics", "renderScale", g.renderScale);
        g.enablePostFX     = m_Config.GetBool("Graphics", "enablePostFX", g.enablePostFX);
        g.shadowMapSize    = m_Config.GetInt("Graphics", "shadowMapSize", g.shadowMapSize);
        return g;
    }

    void EngineSettings::SetGraphics(const Graphics& g) {
        m_Config.SetInt("Graphics", "windowWidth", g.windowWidth);
        m_Config.SetInt("Graphics", "windowHeight", g.windowHeight);
        m_Config.SetBool("Graphics", "fullscreen", g.fullscreen);
        m_Config.SetBool("Graphics", "vsync", g.vsync);
        m_Config.SetInt("Graphics", "targetFPS", g.targetFPS);
        m_Config.SetFloat("Graphics", "renderScale", g.renderScale);
        m_Config.SetBool("Graphics", "enablePostFX", g.enablePostFX);
        m_Config.SetInt("Graphics", "shadowMapSize", g.shadowMapSize);
    }

    // ── Audio ──

    EngineSettings::Audio EngineSettings::GetAudio() const {
        Audio a;
        a.masterVolume  = m_Config.GetFloat("Audio", "masterVolume", a.masterVolume);
        a.musicVolume   = m_Config.GetFloat("Audio", "musicVolume", a.musicVolume);
        a.sfxVolume     = m_Config.GetFloat("Audio", "sfxVolume", a.sfxVolume);
        a.audioChannels = m_Config.GetInt("Audio", "audioChannels", a.audioChannels);
        return a;
    }

    void EngineSettings::SetAudio(const Audio& a) {
        m_Config.SetFloat("Audio", "masterVolume", a.masterVolume);
        m_Config.SetFloat("Audio", "musicVolume", a.musicVolume);
        m_Config.SetFloat("Audio", "sfxVolume", a.sfxVolume);
        m_Config.SetInt("Audio", "audioChannels", a.audioChannels);
    }

    // ── Physics ──

    EngineSettings::Physics EngineSettings::GetPhysics() const {
        Physics p;
        p.velocityIterations = m_Config.GetInt("Physics", "velocityIterations", p.velocityIterations);
        p.positionIterations = m_Config.GetInt("Physics", "positionIterations", p.positionIterations);
        p.gravityScale       = m_Config.GetFloat("Physics", "gravityScale", p.gravityScale);
        return p;
    }

    void EngineSettings::SetPhysics(const Physics& p) {
        m_Config.SetInt("Physics", "velocityIterations", p.velocityIterations);
        m_Config.SetInt("Physics", "positionIterations", p.positionIterations);
        m_Config.SetFloat("Physics", "gravityScale", p.gravityScale);
    }

} // namespace Engine
