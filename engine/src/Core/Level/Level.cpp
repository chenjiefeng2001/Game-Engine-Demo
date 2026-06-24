#include "Engine/Core/Level/Level.h"
#include "Engine/Core/Scene/Serializer.h"
#include "Engine/Core/Log.h"
#include <sstream>
#include <thread>

namespace Engine {

    // ──────────────────────────────────────────────
    // Level 构造/析构
    // ──────────────────────────────────────────────

    Level::Level(LevelInfo info)
        : m_Info(std::move(info))
    {
    }

    Level::~Level() {
        if (m_State.load() != LevelState::Unloaded) {
            Unload();
        }
    }

    // ──────────────────────────────────────────────
    // 状态转换
    // ──────────────────────────────────────────────

    void Level::SetState(LevelState newState) {
        LevelState old = m_State.exchange(newState);
        ENGINE_LOG_INFO("Level", "{}: {} → {}", m_Info.name,
                        LevelStateToString(old), LevelStateToString(newState));
    }

    // ──────────────────────────────────────────────
    // 同步加载
    // ──────────────────────────────────────────────

    bool Level::Load() {
        if (m_State.load() != LevelState::Unloaded) {
            ENGINE_LOG_WARN("Level", "{}: Load called but state is {}",
                           m_Info.name, LevelStateToString(m_State));
            return m_State.load() == LevelState::Loaded ||
                   m_State.load() == LevelState::Active;
        }

        SetState(LevelState::Loading);
        m_LoadProgress = 0.0f;

        // 创建 Scene 并从文件加载
        m_Scene = std::make_unique<Scene>(m_Info.name);
        if (!m_Info.filePath.empty()) {
            m_LoadProgress = 0.5f;
            if (!m_Scene->LoadFromFile(m_Info.filePath)) {
                ENGINE_LOG_ERROR("Level", "{}: Failed to load scene from {}",
                                m_Info.name, m_Info.filePath);
                SetState(LevelState::Failed);
                m_Scene.reset();
                return false;
            }
        }
        m_LoadProgress = 1.0f;

        SetState(LevelState::Loaded);
        return true;
    }

    // ──────────────────────────────────────────────
    // 异步加载
    // ──────────────────────────────────────────────

    void Level::LoadAsync(std::function<void(bool)> onComplete) {
        if (m_State.load() != LevelState::Unloaded) {
            if (onComplete) onComplete(m_State.load() == LevelState::Loaded ||
                                       m_State.load() == LevelState::Active);
            return;
        }

        m_OnLoadComplete = std::move(onComplete);
        SetState(LevelState::Loading);
        m_LoadProgress = 0.0f;

        // 后台线程执行加载
        std::thread loadThread([this]() {
            m_Scene = std::make_unique<Scene>(m_Info.name);
            if (!m_Info.filePath.empty()) {
                m_LoadProgress = 0.5f;
                if (!m_Scene->LoadFromFile(m_Info.filePath)) {
                    ENGINE_LOG_ERROR("Level", "{}: Async load failed from {}",
                                    m_Info.name, m_Info.filePath);
                    SetState(LevelState::Failed);
                    m_Scene.reset();
                    if (m_OnLoadComplete) m_OnLoadComplete(false);
                    return;
                }
            }
            m_LoadProgress = 1.0f;
            SetState(LevelState::Loaded);
            if (m_OnLoadComplete) m_OnLoadComplete(true);
        });
        loadThread.detach();
    }

    // ──────────────────────────────────────────────
    // 激活/取消激活
    // ──────────────────────────────────────────────

    bool Level::Activate() {
        if (m_State.load() != LevelState::Loaded) {
            ENGINE_LOG_WARN("Level", "{}: Activate called but state is {}",
                           m_Info.name, LevelStateToString(m_State));
            return false;
        }

        SetState(LevelState::Activating);
        if (m_Scene) {
            m_Scene->OnCreate();
        }
        SetState(LevelState::Active);
        return true;
    }

    void Level::Deactivate() {
        LevelState state = m_State.load();
        if (state != LevelState::Active && state != LevelState::Loaded) return;

        SetState(LevelState::Deactivating);
        if (m_Scene) {
            m_Scene->OnDestroy();
        }
        SetState(LevelState::Loaded); // 回到 Loaded 状态，保留 Scene 数据
    }

    void Level::Unload() {
        if (m_State.load() == LevelState::Unloaded) return;

        if (m_State.load() == LevelState::Active) {
            Deactivate();
        }

        SetState(LevelState::Unloading);
        m_Scene.reset();
        m_LoadProgress = 0.0f;
        SetState(LevelState::Unloaded);
    }

    // ──────────────────────────────────────────────
    // 每帧更新
    // ──────────────────────────────────────────────

    void Level::Update(float32 dt) {
        if (m_State.load() != LevelState::Active) return;
        if (!m_Scene) return;
        m_Scene->Update(dt);
    }

    void Level::Render() {
        if (m_State.load() != LevelState::Active) return;
        if (!m_Scene) return;
        m_Scene->Render();
    }

    void Level::PostPhysicsUpdate() {
        if (m_State.load() != LevelState::Active) return;
        if (!m_Scene) return;
        m_Scene->PostPhysicsUpdate();
    }

} // namespace Engine