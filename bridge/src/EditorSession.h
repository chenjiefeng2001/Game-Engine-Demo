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
    /// P3-B：读实体脚本绑定路径（空串=无绑定；契约内 scriptGuid 是真实数据）
    bool GetEntityScript(int32_t index, std::string* out) const;
    /// P3-B：Assign Script —— 只写 binding 表（运行时由 Play 消费 director），
    /// 必须 Script 类型资产；置 dirty + 广播 EV_ENTITY_SCRIPT_ASSIGNED。
    bool AssignScript(int32_t assetIndex, int32_t entityIndex);

    bool GetAssetPath(int32_t index, std::string* out) const;
    int32_t GetAssetType(int32_t index) const;
    bool GetAssetGuid(int32_t index, std::string* out) const;

    // ── Phase 3-C (Asset Browser) ──
    /// 导入资产（ContentRegistry::Import 幂等）。成功返回资产索引，失败 -1。
    int32_t ImportAsset(const std::string& path, int32_t type);
    /// 重命名资产（GUID 不变 → 场景绑定稳定）。物理文件改名 + 注册表更新。
    bool RenameAsset(int32_t assetIndex, const std::string& newName);

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
    /// 按会话内稳定索引取资产条目（按值拷贝；false=越界/未找到）。
    /// 索引唯一事实源是 m_AssetOrder（只追加序）：OpenProject 时按注册表
    /// 快照建立，Import 时只 append，Rename 不改 GUID/位置 → 既有索引永不
    /// 位移。禁止直接 m_Reg.GetAllEntries()[i]：unordered_map 迭代序随
    /// 插入/重排变化，二次导入后旧索引会指向别的资产（P3-C 实证）。
    bool AssetAt(int32_t index, Engine::Content::AssetEntry* out) const;
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
    /// 会话内稳定资产序（只追加 GUID；索引语义 = 此 vector 下标）
    std::vector<Engine::ResourceGUID> m_AssetOrder;

    std::string m_ScenePath;
    std::string m_ManifestPath;
    EventFn m_Event;
    std::string m_LastError;
    bool m_Playing = false;
    bool m_Dirty = false;
};

} // namespace editor_bridge
