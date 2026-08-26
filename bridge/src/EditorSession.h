#pragma once

/**
 * @file EditorSession.h
 * @brief 无头编辑会话核心 —— AV-005 防腐层（Phase 0）
 *
 * 职责：把 GP01ProductionSession 已验证的无头路径（manifest 加载 /
 * 场景实例化 / 实体 CRUD / 存盘）收编为 UI 无关的会话对象，
 * 供 C ABI（capi.cpp）与未来任何 frontend 消费。
 *
 * 禁止：include imgui / EngineEditor / 任何面板头。
 * 纪律：只使用冻结契约（ContentRegistry / SceneSerializerV1 / Scene）。
 */

#include <memory>
#include <string>
#include <vector>
#include <functional>

#include <Engine/Core/Content/ContentAsset.h>
#include <Engine/Core/Content/SceneSerializerV1.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/OpenGL/OpenGLGraphicsFactory.h>

namespace editor_bridge {

class EditorSession {
public:
    using EventFn = std::function<void(int32_t type, const char* payload)>;

    EditorSession() = default;
    ~EditorSession() = default;
    EditorSession(const EditorSession&) = delete;
    EditorSession& operator=(const EditorSession&) = delete;

    bool OpenProject(const std::string& manifestPath,
                     const std::string& scenePath);
    bool SaveProject();

    int32_t GetEntityCount() const;
    int32_t GetAssetCount() const { return static_cast<int32_t>(m_Reg.Count()); }
    int32_t CreateEntity(const char* name);
    bool GetEntityName(int32_t index, std::string* out) const;

    void SetEventCallback(EventFn cb) { m_Event = std::move(cb); }
    const std::string& GetLastError() const { return m_LastError; }

private:
    void Emit(int32_t type, const std::string& payload);
    void RealignBindings();
    /// 记录失败并返回 false（统一错误出口，供 ABI 层读取）
    bool Fail(const std::string& msg);

    Engine::OpenGLGraphicsFactory m_Gfx;   // 会话自有工厂（与宿主解耦，同 GP01 先例）
    Engine::TextureManager m_TexMgr{m_Gfx};
    Engine::Content::ContentRegistry m_Reg;
    std::shared_ptr<Engine::Scene> m_EditScene;
    std::vector<Engine::Content::EntityContentBinding> m_Bindings;

    std::string m_ScenePath;
    std::string m_ManifestPath;
    EventFn m_Event;
    std::string m_LastError;
};

} // namespace editor_bridge
