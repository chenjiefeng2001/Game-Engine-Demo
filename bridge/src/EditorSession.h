#pragma once

/**
 * @file EditorSession.h
 * @brief 无头编辑会话核心 —— AV-005 防腐层（Phase 0）
 *
 * 职责：把 GP01ProductionSession 已验证的无头路径（manifest 加载 /
 * 场景实例化 / 实体 CRUD / 存盘）收编为 UI 无关的会话对象，
 * 供 C ABI（capi.cpp）与未来任何 frontend 消费。
 *
 * 禁止：此头文件 include imgui / EngineEditor / 任何面板头。
 *        .cpp 实现允许使用 Engine/Application.h 等引擎头文件（转发视口操作），
 *        但不得将引擎类型泄漏到此头文件的公共接口中。
 *        例外：Engine/Editor/EditorCameraInput.h 是纯 POD 结构体（零依赖），
 *        允许包含以支持输入转发。
 * 纪律：只使用冻结契约（ContentRegistry / SceneSerializerV1 / Scene）。
 */

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <functional>
#include <Engine/Editor/EditorCameraInput.h>

#include <Engine/Core/Content/ContentAsset.h>
#include <Engine/Core/Content/SceneSerializerV1.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include <Engine/Scripting/ScriptInstance.h>

namespace Engine {
class Application;
}

namespace editor_bridge {

class EditorSession {
public:
    using EventFn = std::function<void(int32_t type, const char* payload)>;

    EditorSession();
    EditorSession(Engine::OpenGLGraphicsFactory& factory,
                 Engine::Application* application);
    ~EditorSession();
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

    // ── F1（Component Contract）：约定组件（Camera 等）──
    /// 挂载契约组件（单实例语义；类型未注册拒绝）。会置 dirty + 事件。
    bool AddComponent(int32_t entityIndex, const std::string& typeName);
    /// 移除契约组件（未找到拒绝）。会置 dirty + 事件。
    bool RemoveComponent(int32_t entityIndex, const std::string& typeName);
    /// 实体是否已挂载该契约组件。
    bool HasComponent(int32_t entityIndex, const std::string& typeName) const;
    /// 实体上已挂载的契约组件数量（-1=失败）。
    int32_t GetComponentCount(int32_t entityIndex) const;
    /// 按序读契约组件稳定类型名（false=越界/无项目）。
    bool GetComponentTypeAt(int32_t entityIndex, int32_t compIndex,
                            std::string* out) const;
    /// 读契约组件反射属性（字符串值）。false=实体/组件/属性不存在。
    bool GetComponentProperty(int32_t entityIndex, const std::string& typeName,
                              const std::string& propName,
                              std::string* out) const;
    /// 写契约组件反射属性（编辑态；字符串按类型解析）。会置 dirty + 事件。
    bool SetComponentProperty(int32_t entityIndex, const std::string& typeName,
                              const std::string& propName,
                              const std::string& valueStr);
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

    // ── Phase 3-D (P3-D) Runtime：Play / Reload / Stop ──
    /// 启动运行时：克隆编辑场景为新运行场景，绑定 GameplayAPI 句柄，
    /// 用 ScriptInstance 加载并执行指定脚本资产（assetIndex<0 = 项目 director：
    /// 第一个非空 scriptGuid）。失败返回 false 并在 Error/RuntimeError 取原因。
    bool Play(int32_t assetIndex);
    /// 热重载当前运行脚本：保留 _PERSIST 表（S4），随后重新 OnCreate。
    /// 语法/运行错误经 pcall 捕获，Session 不崩溃；返回 false + RuntimeError。
    bool Reload();
    /// 停止运行时并回到编辑态（重新绑定编辑场景）。非运行态为空操作。
    void Stop();
    /// 运行态单帧推进（可选；编辑器不发常驻帧时 gate 可手动 tick 验证 OnUpdate）
    bool RuntimeTick(float dt);
    struct RuntimeStats {
        uint32_t stopCount = 0;
        uint32_t onCreateCount = 0;
        uint32_t onDestroyCount = 0;
        uint32_t gameplayHandleCount = 0;
        bool playing = false;
        bool runtimeScenePresent = false;
        bool scriptValid = false;
        bool callbackAttached = false;
        bool gameplayScenePresent = false;
        bool hudDirty = false;
    };
    bool GetRuntimeStats(RuntimeStats* out) const;
    /// 运行态探针：执行 `<probe> = _PERSIST[key]` 并返回整数值（D3b _PERSIST 验证）。
    /// 非运行态或取不到时返回 def。
    int32_t RuntimePersistInt(const std::string& key, int32_t def);
    /// 最近一次运行时动作（Play/Reload/Tick）的 Lua 诊断文本（成功时空串）
    const std::string& GetRuntimeError() const { return m_RuntimeError; }

    // ── Viewport（Phase 7）──
    /// 获取视口 FBO 颜色纹理的 OpenGL 句柄（GLuint）。未初始化返回 0。
    uint32_t GetViewportTextureHandle() const;
    /// 获取视口当前渲染尺寸。
    void GetViewportSize(int32_t* outWidth, int32_t* outHeight) const;
    /// 通知视口尺寸变化（Avalonia 控件 resize 时调用）。
    void SetViewportSize(int32_t width, int32_t height);
    /// 触发视口重绘（标记 dirty）。每帧由 Avalonia 调用一次。
    void ViewportTick(float dt);

    /// 设置视口相机位置（编辑态）。
    void SetViewportCameraPosition(float x, float y, float z);
    /// 获取视口相机位置。
    void GetViewportCameraPosition(float out3[3]) const;
    /// 设置视口相机投影类型：0=Perspective, 1=Orthographic。
    void SetViewportCameraProjection(int32_t type);
    /// 设置视口相机缩放（正交模式）。
    void SetViewportCameraZoom(float zoom);

    /// 转发鼠标移动事件到视口。
    void ViewportMouseMove(float x, float y);
    /// 转发鼠标按钮事件到视口（0=Left, 1=Right, 2=Middle; pressed=1/0）。
    void ViewportMouseButton(int32_t button, int32_t pressed);
    /// 转发鼠标滚轮事件到视口。
    void ViewportScroll(float delta);
    /// 转发键盘按键事件到视口。
    void ViewportKeyDown(int32_t key);
    void ViewportKeyUp(int32_t key);

    /// 视口鼠标拾取：返回实体索引（>=0），无命中返回 -1。
    int32_t ViewportPick(float ndcX, float ndcY);

    /// Gizmo 操作模式：0=Select, 1=Translate, 2=Rotate, 3=Scale。
    void SetGizmoMode(int32_t mode);
    /// Gizmo 坐标空间：0=World, 1=Local。
    void SetGizmoSpace(int32_t space);
    /// Gizmo 吸附开关及步长。
    void SetGizmoSnap(int32_t enabled, float value);
    /// 获取当前 Gizmo 状态。
    int32_t GetGizmoMode() const;
    int32_t GetGizmoSpace() const;
    int32_t GetGizmoSnapEnabled() const;
    float GetGizmoSnapValue() const;

    /// 视口网格可见性。
    void SetViewportGridVisible(int32_t visible);
    int32_t GetViewportGridVisible() const;

    /// Phase 7-B: 引擎 EditorCamera 外部输入模式。
    void SetCameraExternalInput(int32_t enabled);
    int32_t GetCameraExternalInput() const;

    /// Phase 7: 获取视口 FBO 像素（RGBA8）。返回所需字节数。
    int32_t GetViewportPixels(uint8_t* outBuffer, int32_t bufferSize);

    bool IsGraphicsReady() const { return m_Gfx && m_Gfx->IsReady(); }
    bool IsCreationThread() const { return m_CreationThreadId == std::this_thread::get_id(); }
    bool IsDirty() const { return m_Dirty; }

    void SetEventCallback(EventFn cb);
    const std::string& GetLastError() const { return m_LastError; }
    /// 最近一次 OpenProject 的非致命告警（如缺失 GUID 资产，契约内保留实体）。
    /// 空串 = 无告警。用于 E3 负路径观察。
    const std::string& GetWarnings() const { return m_Warnings; }
    /// 是否处于运行态（P3-D Play/Reload/Stop 维护）
    bool IsPlaying() const { return m_Playing.load(); }

    // ── HRC-3 P1：production render 的内容来源（bridge 内部，非 C ABI）──
    //
    // EngineHost 的 production render step 需要拿到"当前打开的编辑场景"。
    // 刻意只暴露指针访问，不新增 C 导出（HRC-3 P2 才引入 managed 侧 Fetch op）。
    Engine::Scene* GetEditScene() const { return m_EditScene.get(); }
    /// 编辑场景里的可渲染内容数量（HRC-3 P1 的前置：0 ⇒ insufficient scene content）。
    int32_t CountEditSceneRenderables() const;
    /// 把编辑场景里"已有 sprite GUID 但尚未绑定纹理"的 sprite 绑上纹理。
    ///
    /// 背景（HRC-3 P1 实测发现）：场景反序列化只保留 sprite GUID
    /// （`SceneSerializerV1` → `EntityContentBinding::spriteGuid`），
    /// 纹理只在 `AssignSprite` 这种显式编辑动作里才被绑到 `SpriteComponent` 上。
    /// 于是 production render 收集到的 `RenderCommand::texture` 全是 null，
    /// `SceneRenderer::Render` 会跳过它们 —— 帧里一个 sprite 都画不出来。
    ///
    /// 本函数是**幂等**的、只读 GUID 不改语义，因此不属于新的编辑能力；
    /// 返回成功绑定的数量（0 表示场景里没有待绑定的 sprite）。
    int32_t BindEditSceneSpriteTextures();

private:
    void Emit(int32_t type, const std::string& payload);
    void PublishRuntimeState() noexcept;
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

    std::thread::id m_CreationThreadId = std::this_thread::get_id();
    std::unique_ptr<Engine::OpenGLGraphicsFactory> m_OwnedGfx;
    Engine::OpenGLGraphicsFactory* m_Gfx = nullptr;
    Engine::Application* m_Application = nullptr;
    Engine::TextureManager m_TexMgr;
    Engine::Content::ContentRegistry m_Reg;
    std::shared_ptr<Engine::Scene> m_EditScene;
    std::vector<Engine::Content::EntityContentBinding> m_Bindings;
    /// 会话内稳定资产序（只追加 GUID；索引语义 = 此 vector 下标）
    std::vector<Engine::ResourceGUID> m_AssetOrder;

    std::string m_ScenePath;
    std::string m_ManifestPath;
    EventFn m_Event;
    mutable std::recursive_mutex m_EventMutex;
    mutable std::mutex m_ProofMutex;
    std::string m_LastError;
    std::string m_Warnings;
    /// 最近一次运行时动作的 Lua 诊断文本（Play/Reload/Tick 失败原因）
    std::string m_RuntimeError;
    Engine::Scripting::ScriptInstance m_Inst;   ///< 运行态导演脚本实例
    std::shared_ptr<Engine::Scene> m_Runtime;    ///< 运行态场景（编辑场景克隆）
    std::string m_RuntimeScriptPath;             ///< 当前运行脚本的绝对路径
    std::atomic<bool> m_Playing{false};
    std::atomic<bool> m_RuntimeScenePresent{false};
    std::atomic<bool> m_ScriptValid{false};
    std::atomic<bool> m_PlayingSnapshot{false};
    std::atomic<bool> m_CallbackAttached{false};
    std::atomic<uint32_t> m_StopCount{0};
    RuntimeStats m_LastStats;
    bool m_Dirty = false;

    // ── Viewport state（Phase 7）──
    int32_t  m_ViewportWidth = 0;        ///< Bridge 侧缓存的视口宽度（用于 GetViewportSize 快速查询）
    int32_t  m_ViewportHeight = 0;       ///< Bridge 侧缓存的视口高度
    bool     m_ViewportInitialized = false;
    int32_t  m_GizmoMode = 0;            ///< 0=Select, 1=Translate, 2=Rotate, 3=Scale
    int32_t  m_GizmoSpace = 0;           ///< 0=World, 1=Local
    int32_t  m_GizmoSnapEnabled = 0;
    float    m_GizmoSnapValue = 0.5f;
    int32_t  m_ViewportGridVisible = 1;
    float    m_LastMouseX = 0.0f;
    float    m_LastMouseY = 0.0f;
    bool     m_CameraExternalInput = false;
    Engine::EditorCameraInput m_PendingInput;
};

} // namespace editor_bridge
