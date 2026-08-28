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
    bool GetEntityPosition(int32_t index, float out3[3]) const;
    bool SetEntityPosition(int32_t index, const float pos3[3]);

    // ── Phase 2 (AV-G2) ──
    bool DeleteEntity(int32_t index);
    bool RenameEntity(int32_t index, const std::string& newName);
    bool GetEntitySprite(int32_t index, std::string* out) const;
    bool AssignSprite(int32_t assetIndex, int32_t entityIndex);

    bool GetAssetPath(int32_t index, std::string* out) const;
    int32_t GetAssetType(int32_t index) const;
    bool GetAssetGuid(int32_t index, std::string* out) const;

    bool ScriptRead(int32_t assetIndex, std::string* out);
    bool ScriptSave(int32_t assetIndex, const std::string& text);

    bool IsDirty() const { return m_Dirty; }

    void SetEventCallback(EventFn cb) { m_Event = std::move(cb); }
    const std::string& GetLastError() const { return m_LastError; }
    /// 运行态占位（Play/Stop 属 Phase 7 Runtime track；当前恒 false）
    bool IsPlaying() const { return m_Playing; }

private:
    void Emit(int32_t type, const std::string& payload);
    void RealignBindings();
    /// 记录失败并返回 false（统一错误出口，供 ABI 层读取）
    bool Fail(const std::string& msg);
    /// 置 dirty（任何编辑路径统一入口）
    void MarkDirty();
    /// 把 registry path 解析为可读写文件路径：相对路径锚定到 manifest 目录
    std::string ResolveContentPath(const std::string& path) const;

    Engine::OpenGLGraphicsFactory m_Gfx;   // 会话自有工厂（与宿主解耦，同 GP01 先例）
    Engine::TextureManager m_TexMgr{m_Gfx};
    Engine::Content::ContentRegistry m_Reg;
    std::shared_ptr<Engine::Scene> m_EditScene;
    std::vector<Engine::Content::EntityContentBinding> m_Bindings;

    std::string m_ScenePath;
    std::string m_ManifestPath;
    EventFn m_Event;
    std::string m_LastError;
    bool m_Playing = false;
    bool m_Dirty = false;
};

} // namespace editor_bridge
