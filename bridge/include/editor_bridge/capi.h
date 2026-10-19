#pragma once

/**
 * @file capi.h
 * @file EditorBridge C ABI —— Avalonia ↔ C++ Engine 的唯一边界（AV-002）
 *
 * 规则（docs/Avalonia-Editor-Migration-v1.md §3/§4）：
 *   - 只暴露不透明句柄，绝不泄漏 C++ 指针/引用/STL 类型
 *   - 字符串一律 UTF-8，调用方分配缓冲区
 *   - 单线程约定：所有句柄操作必须在创建线程上执行（Phase 0 无锁）
 *   - 事件经回调投递（同步，调用栈内触发），Phase 0 不做跨线程队列
 *   - 字符串输出函数（out+cap 模式）支持两调用法：
 *       第一次调用 out=NULL/cap=0 获取完整长度，第二次分配缓冲区后拷贝。
 *       返回 -1 表示错误，>=0 表示实际长度（含截断）。
 */

#include <stdint.h>

#if defined(__cplusplus)
#if defined(_WIN32) && defined(EDITOR_BRIDGE_EXPORTS)
#define EDITOR_BRIDGE_API extern "C" __declspec(dllexport)
#else
#define EDITOR_BRIDGE_API extern "C"
#endif
#elif defined(_WIN32) && defined(EDITOR_BRIDGE_EXPORTS)
#define EDITOR_BRIDGE_API __declspec(dllexport)
#elif defined(_WIN32)
#define EDITOR_BRIDGE_API __declspec(dllimport)
#else
#define EDITOR_BRIDGE_API
#endif

typedef void* EditorSessionHandle;

#pragma pack(push, 1)
typedef struct EditorSessionRuntimeStats {
    uint32_t struct_size;
    uint32_t stop_count;
    uint32_t on_create_count;
    uint32_t on_destroy_count;
    uint32_t gameplay_handle_count;
    int32_t playing;
    int32_t runtime_scene_present;
    int32_t script_valid;
    int32_t callback_attached;
    int32_t gameplay_scene_present;
    int32_t hud_dirty;
    int32_t session_present;
    int32_t allocator_present;
} EditorSessionRuntimeStats;
#pragma pack(pop)

/// C++ → C# 事件回调（同步触发；payload 为 UTF-8 文本，仅当次调用有效）
typedef void (*EditorEventCallback)(int32_t eventType,
                                    const char* payload,
                                    void* userData);

enum {
    EV_PROJECT_LOADED  = 1, ///< payload: "objects=<n>;assets=<n>"
    EV_ENTITY_CREATED  = 2, ///< payload: 实体名
    EV_ENTITY_MOVED    = 3, ///< payload: "idx=<i>;x=<f>;y=<f>;z=<f>"

    // ── Phase 2 (AV-G2) ──
    EV_PROJECT_SAVED   = 4, ///< payload: "entities=<n>"
    EV_ENTITY_DELETED  = 5, ///< payload: "idx=<i>;name=<n>"
    EV_ENTITY_RENAMED  = 6, ///< payload: "idx=<i>;old=<s>;new=<s>"
    EV_ENTITY_ASSIGNED = 7, ///< payload: "idx=<i>;asset=<assetIndex>;sprite=<path>"
    EV_ENTITY_SCRIPT_ASSIGNED = 8, ///< payload: "idx=<i>;asset=<assetIndex>;script=<path>"
    /// P3-C：资产导入（幂等）。payload: "idx=<i>;guid=<hex>;path=<p>;type=<t>"
    EV_ASSET_IMPORTED = 9,
    /// P3-C：资产重命名（GUID 不变，路径变）。payload: "idx=<i>;guid=<hex>;old=<p>;new=<p>"
    EV_ASSET_RENAMED = 10,
    /// P3-D：进入运行态（Play 成功）。payload: "script=<path>"
    EV_PLAY_STARTED = 11,
    /// P3-D：回到编辑态（Stop）。payload: ""
    EV_PLAY_STOPPED = 12,
    /// F1（Component Contract）：实体组件增删/属性变更。
    /// payload: "idx=<i>;type=<t>;action=<add|remove|set>"（Camera 等契约组件）
    EV_COMPONENT_CHANGED = 13,
    EV_COUNT             = 14, ///< 事件类型总数（用于边界检查）
};

// ── 会话生命周期 ──────────────────────────────────────────────

/// 返回 EditorBridge ABI 版本号（高位.低位）。版本不匹配时消费方应拒绝加载。
/// 例：返回 (1 << 16) | 0 表示 v1.0。
EDITOR_BRIDGE_API int32_t EditorBridge_GetVersion(void);
EDITOR_BRIDGE_API int32_t EditorBridge_RegisterMainThread(void);

/// 创建编辑会话。失败返回 NULL（内存不足）。
EDITOR_BRIDGE_API EditorSessionHandle EditorSession_Create(void);
/// 销毁编辑会话（若正在运行则先 Stop）。h 为 NULL 时为空操作。
EDITOR_BRIDGE_API void EditorSession_Destroy(EditorSessionHandle h);

// ── 工程 ─────────────────────────────────────────────────────

/// 打开工程（manifest + scene）。成功后广播 EV_PROJECT_LOADED。
EDITOR_BRIDGE_API int32_t EditorSession_OpenProject(EditorSessionHandle h,
                                                    const char* manifestPath,
                                                    const char* scenePath);
EDITOR_BRIDGE_API int32_t EditorSession_SaveProject(EditorSessionHandle h);

// ── 场景查询 / 实体操作 ────────────────────────────────────────

EDITOR_BRIDGE_API int32_t EditorSession_GetEntityCount(EditorSessionHandle h);
EDITOR_BRIDGE_API int32_t EditorSession_GetAssetCount(EditorSessionHandle h);

/// 创建实体；name==NULL 或空串时自动命名 Entity/_2/_3...
/// 返回索引（失败 -1）。成功广播 EV_ENTITY_CREATED。
EDITOR_BRIDGE_API int32_t EditorSession_CreateEntity(EditorSessionHandle h,
                                                     const char* name);
/// 按 index 读实体名到 out（UTF-8，截断安全）。返回实际长度（失败 -1）。
EDITOR_BRIDGE_API int32_t EditorSession_GetEntityName(EditorSessionHandle h,
                                                      int32_t index,
                                                      char* out,
                                                      int32_t cap);
/// 读实体位置（Scene 冻结契约：仅 px/py/pz）。失败返回非 0。
EDITOR_BRIDGE_API int32_t EditorSession_GetEntityPosition(EditorSessionHandle h,
                                                          int32_t index,
                                                          float out3[3]);
/// 写实体位置（编辑态；越界索引拒绝；会置 dirty）。成功广播 EV_ENTITY_MOVED。
EDITOR_BRIDGE_API int32_t EditorSession_SetEntityPosition(EditorSessionHandle h,
                                                          int32_t index,
                                                          const float pos3[3]);
/// 删除实体（编辑态；越界拒绝；会置 dirty）。成功广播 EV_ENTITY_DELETED。
EDITOR_BRIDGE_API int32_t EditorSession_DeleteEntity(EditorSessionHandle h,
                                                     int32_t index);
/// 重命名实体（编辑态；空名/重名拒绝；会置 dirty）。成功广播 EV_ENTITY_RENAMED。
EDITOR_BRIDGE_API int32_t EditorSession_RenameEntity(EditorSessionHandle h,
                                                      int32_t index,
                                                      const char* newName);
/// 读实体当前 Sprite 绑定的资产路径（无绑定返回空串，失败返回 -1）。
EDITOR_BRIDGE_API int32_t EditorSession_GetEntitySprite(EditorSessionHandle h,
                                                        int32_t index,
                                                        char* out,
                                                        int32_t cap);
/// 给实体 Assign Sprite：assetIndex 必须是 Texture 资产；会置 dirty。
/// 成功广播 EV_ENTITY_ASSIGNED。
EDITOR_BRIDGE_API int32_t EditorSession_AssignSprite(EditorSessionHandle h,
                                                     int32_t assetIndex,
                                                     int32_t entityIndex);
/// 读实体脚本绑定路径（无绑定返回空串，失败返回 -1）。P3-B。
EDITOR_BRIDGE_API int32_t EditorSession_GetEntityScript(EditorSessionHandle h,
                                                        int32_t index,
                                                        char* out,
                                                        int32_t cap);
/// 给实体 Assign Script（binding 表）：assetIndex 必须是 Script 资产；
/// 会置 dirty。成功广播 EV_ENTITY_SCRIPT_ASSIGNED。P3-B。
EDITOR_BRIDGE_API int32_t EditorSession_AssignScript(EditorSessionHandle h,
                                                     int32_t assetIndex,
                                                     int32_t entityIndex);
/// 导入资产（ContentRegistry::Import，幂等）：path 为相对 CWD 的工程路径，
/// type 0=Texture 1=Script。文件必须已存在。成功返回资产索引（≥0），
/// 失败 -1。会置 dirty。成功广播 EV_ASSET_IMPORTED。P3-C。
EDITOR_BRIDGE_API int32_t EditorSession_ImportAsset(EditorSessionHandle h,
                                                    const char* path,
                                                    int32_t type);
/// 重命名资产（GUID 不变 → 场景绑定稳定）：物理文件改名 + 注册表路径更新。
/// newName 不含扩展名（按原扩展名补全）。成功 0，失败 -1。会置 dirty。
/// 成功广播 EV_ASSET_RENAMED。P3-C。
EDITOR_BRIDGE_API int32_t EditorSession_RenameAsset(EditorSessionHandle h,
                                                    int32_t assetIndex,
                                                    const char* newName);

// ── 资产查询（Asset Browser Phase 4 的最小前驱）────────────────

EDITOR_BRIDGE_API int32_t EditorSession_GetAssetPath(EditorSessionHandle h,
                                                     int32_t index,
                                                     char* out,
                                                     int32_t cap);
/// 0=Texture 1=Script 2=Unknown
EDITOR_BRIDGE_API int32_t EditorSession_GetAssetType(EditorSessionHandle h,
                                                     int32_t index);
/// 读资产 GUID（32 字符 hex）。越界失败返回 -1，否则返回长度。
EDITOR_BRIDGE_API int32_t EditorSession_GetAssetGuid(EditorSessionHandle h,
                                                     int32_t index,
                                                     char* out,
                                                     int32_t cap);

// ── 脚本文档（Script Editor；纯内容文件 IO，走 registry 解析 + manifest 目录兜底）─

/// 读取脚本资产全文（UTF-8）。失败返回 -1，否则返回实际长度。
/// 路径解析：先按 registry path；若为相对路径则锚定到 manifest 目录，
/// 使 scratch 工程自包含。
EDITOR_BRIDGE_API int32_t EditorSession_ScriptRead(EditorSessionHandle h,
                                                   int32_t assetIndex,
                                                   char* out,
                                                   int32_t cap);
/// 写回脚本资产全文（覆盖）。以真实写盘结果为准；成功再清 dirty。
EDITOR_BRIDGE_API int32_t EditorSession_ScriptSave(EditorSessionHandle h,
                                                   int32_t assetIndex,
                                                   const char* text);

// ── 运行时（Phase 3-D）：Play / Reload / Stop / Tick ────────────────

/// 启动运行时：资产索引（assetIndex>=0）或项目 director（assetIndex<0）。
/// 克隆编辑场景为运行场景，加载并 OnCreate 指定导演脚本。
/// 成功返回 1，失败 0（原因经 GetLastError / GetRuntimeError）。
/// 成功广播 EV_PLAY_STARTED。
EDITOR_BRIDGE_API int32_t EditorSession_Play(EditorSessionHandle h,
                                             int32_t assetIndex);
/// 热重载当前运行脚本（保留 _PERSIST 表）。成功 1，失败 0（语法/运行错误
/// 经 GetRuntimeError 取回；Session 不崩溃）。
EDITOR_BRIDGE_API int32_t EditorSession_Reload(EditorSessionHandle h);
/// 停止运行时，回到编辑态。非运行态为空操作。广播 EV_PLAY_STOPPED。
EDITOR_BRIDGE_API void     EditorSession_Stop(EditorSessionHandle h);
/// 运行态单帧推进（GameplayAPI::OnUpdate）。非运行态空操作。
EDITOR_BRIDGE_API void     EditorSession_RuntimeTick(EditorSessionHandle h,
                                                     float dt);
EDITOR_BRIDGE_API int32_t  EditorSession_GetRuntimeStats(
    EditorSessionHandle h,
    EditorSessionRuntimeStats* stats);
/// 是否处于运行态。
EDITOR_BRIDGE_API int32_t EditorSession_IsPlaying(EditorSessionHandle h);
/// 最近一次运行时动作（Play/Reload/Tick）的 Lua 诊断文本（成功时空串）。
EDITOR_BRIDGE_API int32_t EditorSession_GetRuntimeError(EditorSessionHandle h,
                                                        char* out,
                                                        int32_t cap);
/// 运行态探针：返回 `_PERSIST[key]`（整数）。key 为裸键名（不含引号）。
/// 非运行态/取不到时返回 defaultVal。
EDITOR_BRIDGE_API int32_t EditorSession_RuntimePersistInt(
    EditorSessionHandle h, const char* key, int32_t defaultVal);

// ── Component Contract（F1）：实体契约组件（Camera 等）─────────────
// 契约身份 = 稳定类型名（与 Serialization/Lua 共用）。typeName 必须为已
// 注册的契约组件类型，否则 Add 拒绝。属性经反射元数据读写（字符串值）。

/// 给实体挂载一个契约组件（类型已注册才成功）；单实例语义（重复 = 幂等返回既有）。
/// 会置 dirty。成功 0，失败 -1。成功广播 EV_COMPONENT_CHANGED(action=add)。
EDITOR_BRIDGE_API int32_t EditorSession_AddComponent(EditorSessionHandle h,
                                                     int32_t entityIndex,
                                                     const char* typeName);
/// 从实体移除契约组件（按其稳定类型名）。会置 dirty。成功 0，失败 -1。
/// 成功广播 EV_COMPONENT_CHANGED(action=remove)。
EDITOR_BRIDGE_API int32_t EditorSession_RemoveComponent(EditorSessionHandle h,
                                                        int32_t entityIndex,
                                                        const char* typeName);
/// 实体是否已挂载该契约组件：1/0；失败 -1。
EDITOR_BRIDGE_API int32_t EditorSession_HasComponent(EditorSessionHandle h,
                                                     int32_t entityIndex,
                                                     const char* typeName);
/// 实体上已挂载的契约组件数量；失败 -1。
EDITOR_BRIDGE_API int32_t EditorSession_GetComponentCount(EditorSessionHandle h,
                                                          int32_t entityIndex);
/// 按序读实体上第 compIndex 个契约组件的稳定类型名。返回长度或 -1。
EDITOR_BRIDGE_API int32_t EditorSession_GetComponentTypeAt(
    EditorSessionHandle h, int32_t entityIndex, int32_t compIndex,
    char* out, int32_t cap);
/// 读组件反射属性值（字符串形式：bool->"true/false"，float->%g，int->十进制，
/// string->原样）。返回长度或 -1（实体/组件/属性不存在）。
EDITOR_BRIDGE_API int32_t EditorSession_GetComponentProperty(
    EditorSessionHandle h, int32_t entityIndex, const char* typeName,
    const char* propName, char* out, int32_t cap);
/// 写组件反射属性值（编辑态；字符串按属性类型解析）。会置 dirty。成功 0，失败 -1。
/// 成功广播 EV_COMPONENT_CHANGED(action=set)。
EDITOR_BRIDGE_API int32_t EditorSession_SetComponentProperty(
    EditorSessionHandle h, int32_t entityIndex, const char* typeName,
    const char* propName, const char* valueStr);

// ── Viewport（Phase 7）：纹理共享 / 相机 / 输入 / Gizmo ──────────

/// 获取视口 FBO 颜色纹理的 OpenGL 句柄（GLuint）。用于 Avalonia 纹理互操作。
/// 非编辑态/未初始化时返回 0。尺寸经 GetViewportSize 获取。
EDITOR_BRIDGE_API uint32_t EditorSession_GetViewportTextureHandle(
    EditorSessionHandle h);
/// 获取视口当前渲染尺寸（像素）。outWidth/outHeight 不可为 NULL。
EDITOR_BRIDGE_API int32_t EditorSession_GetViewportSize(
    EditorSessionHandle h, int32_t* outWidth, int32_t* outHeight);
/// 通知视口尺寸变化（Avalonia 控件 resize 时调用）。引擎按需重建 FBO。
EDITOR_BRIDGE_API void EditorSession_SetViewportSize(
    EditorSessionHandle h, int32_t width, int32_t height);
/// 触发视口重绘（标记 dirty）。每帧由 Avalonia 调用一次。
EDITOR_BRIDGE_API void EditorSession_ViewportTick(EditorSessionHandle h, float dt);

/// 设置视口相机位置（编辑态）。x/y/z 世界坐标。
EDITOR_BRIDGE_API void EditorSession_SetViewportCameraPosition(
    EditorSessionHandle h, float x, float y, float z);
/// 获取视口相机位置。out3 不可为 NULL。
EDITOR_BRIDGE_API void EditorSession_GetViewportCameraPosition(
    EditorSessionHandle h, float out3[3]);
/// 设置视口相机投影类型：0=Perspective, 1=Orthographic。
EDITOR_BRIDGE_API void EditorSession_SetViewportCameraProjection(
    EditorSessionHandle h, int32_t type);
/// 设置视口相机缩放（正交模式）。
EDITOR_BRIDGE_API void EditorSession_SetViewportCameraZoom(
    EditorSessionHandle h, float zoom);

/// 转发鼠标移动事件到视口（Avalonia PointerMoved 调用）。x/y 为控件内局部坐标。
EDITOR_BRIDGE_API void EditorSession_ViewportMouseMove(
    EditorSessionHandle h, float x, float y);
/// 转发鼠标按钮事件到视口（0=Left, 1=Right, 2=Middle; pressed=1/0）。
EDITOR_BRIDGE_API void EditorSession_ViewportMouseButton(
    EditorSessionHandle h, int32_t button, int32_t pressed);
/// 转发鼠标滚轮事件到视口。delta>0 向上，delta<0 向下。
EDITOR_BRIDGE_API void EditorSession_ViewportScroll(
    EditorSessionHandle h, float delta);
/// 转发键盘按键事件到视口（Avalonia KeyDown/KeyUp 调用）。key 为 Avalonia Key 枚举值。
EDITOR_BRIDGE_API void EditorSession_ViewportKeyDown(
    EditorSessionHandle h, int32_t key);
EDITOR_BRIDGE_API void EditorSession_ViewportKeyUp(
    EditorSessionHandle h, int32_t key);

/// 视口鼠标拾取：ndcX/ndcY 为 [-1,1] 归一化设备坐标。返回实体索引（>=0），
/// 无命中返回 -1。仅在编辑态有效。
EDITOR_BRIDGE_API int32_t EditorSession_ViewportPick(
    EditorSessionHandle h, float ndcX, float ndcY);

/// Gizmo 操作模式：0=Select, 1=Translate, 2=Rotate, 3=Scale。
EDITOR_BRIDGE_API void EditorSession_SetGizmoMode(
    EditorSessionHandle h, int32_t mode);
/// Gizmo 坐标空间：0=World, 1=Local。
EDITOR_BRIDGE_API void EditorSession_SetGizmoSpace(
    EditorSessionHandle h, int32_t space);
/// Gizmo 吸附开关及步长。enabled=1/0, value=吸附步长（如 0.5, 45.0）。
EDITOR_BRIDGE_API void EditorSession_SetGizmoSnap(
    EditorSessionHandle h, int32_t enabled, float value);
/// 获取当前 Gizmo 模式/空间/吸附状态。
EDITOR_BRIDGE_API int32_t EditorSession_GetGizmoMode(EditorSessionHandle h);
EDITOR_BRIDGE_API int32_t EditorSession_GetGizmoSpace(EditorSessionHandle h);
EDITOR_BRIDGE_API int32_t EditorSession_GetGizmoSnapEnabled(EditorSessionHandle h);
EDITOR_BRIDGE_API float   EditorSession_GetGizmoSnapValue(EditorSessionHandle h);

/// 视口网格可见性：visible=1/0。
EDITOR_BRIDGE_API void EditorSession_SetViewportGridVisible(
    EditorSessionHandle h, int32_t visible);
EDITOR_BRIDGE_API int32_t EditorSession_GetViewportGridVisible(
    EditorSessionHandle h);

/// Phase 7-B: 启用/禁用引擎 EditorCamera 的外部输入模式。
/// 启用后 EditorCamera 不再读取 ImGui::GetIO()，而是使用 ViewportMouseMove/Button/Scroll/Key 系列调用。
EDITOR_BRIDGE_API void EditorSession_SetCameraExternalInput(
    EditorSessionHandle h, int32_t enabled);
EDITOR_BRIDGE_API int32_t EditorSession_GetCameraExternalInput(
    EditorSessionHandle h);

/// Phase 7 纹理读取：将视口 FBO 的 RGBA8 像素拷贝到 outBuffer（调用者分配）。
/// 返回值 = 实际拷贝的字节数；outBuffer 为 NULL 或 bufferSize 不足时返回所需字节数（含 4 字节宽高头）。
/// 数据布局：[int32 width][int32 height][uint8 RGBA × width×height]。
EDITOR_BRIDGE_API int32_t EditorSession_GetViewportPixels(
    EditorSessionHandle h, uint8_t* outBuffer, int32_t bufferSize);

// ── 脏状态（P2-E：Clean->Edit->Dirty->Save->Clean）─────────────

/// 会话是否发生过未保存的编辑（Create/Delete/Rename/SetPosition/Assign 置位；Save 清位）。
EDITOR_BRIDGE_API int32_t EditorSession_IsDirty(EditorSessionHandle h);

// ── 事件 ─────────────────────────────────────────────────────

EDITOR_BRIDGE_API void EditorSession_SetEventCallback(
    EditorSessionHandle h, EditorEventCallback cb, void* userData);

// ── 诊断 ─────────────────────────────────────────────────────

/// 取最近一次失败的 UTF-8 错误文本（无错误返回空串）。
EDITOR_BRIDGE_API int32_t EditorSession_GetLastError(EditorSessionHandle h,
                                                     char* out,
                                                     int32_t cap);
/// 取最近一次 OpenProject 的非致命告警（如缺失 GUID 资产；实体保留）。空串=无。
EDITOR_BRIDGE_API int32_t EditorSession_GetWarnings(EditorSessionHandle h,
                                                     char* out,
                                                     int32_t cap);

typedef void* EngineHostHandle;
typedef int32_t EngineHostStatus;
typedef int32_t EngineHostStage;

/// A7-1 起冻结的 EngineHost ABI 版本。单一来源；native 与 managed 都必须引用此值。
///
/// **HRC-3 P2：v1.1 → v1.2（明确的 ABI increment）**
/// 增量内容**只有一个**：在既有 `EngineHost_Fetch` 的 op 空间内新增
/// `ENGINE_HOST_OP_GET_PRESENTATION_FRAME`（additive op）。
/// 明确**没有**做的事：
///   - 不新增任何 export function（`EngineHost_Fetch` 复用既有导出）；
///   - 不改 `RuntimeResult`（仍为 76B，字段/顺序/packing 全部不变）；
///   - 不改任何既有 op 的编号、容量或语义（新增 op 取 28，既有 0..27 不动）；
///   - 不改 `ENGINE_HOST_STAGE_*` 与既有 `ENGINE_HOST_STATUS_*` 的取值。
/// 新增的 `ENGINE_HOST_STATUS_NO_NEW_FRAME = 11` 是**追加**取值，不占用既有值。
#define ENGINE_HOST_ABI_VERSION ((1 << 16) | 3)

/// HRC-3 P1（D4 已后置 ⇒ v1 目标尺寸固定）：production presentation target 的**初始**尺寸。
/// 单一来源，供 P1 的 presentation FBO、P2 的 Fetch 静态容量、P3 的 gate 断言共用。
/// 这是**编译期常量**，不改变任何导出、结构体布局或既有 op 语义，因此
/// **不构成 ABI increment**（P2 新增 op 时才做 v1.1 → v1.2 的版本递增）。
#define ENGINE_HOST_PRESENTATION_WIDTH 800
#define ENGINE_HOST_PRESENTATION_HEIGHT 600

enum {
    ENGINE_HOST_STATUS_SUCCESS = 0,
    ENGINE_HOST_STATUS_SKIPPED_NOT_PLAYING = 1,
    ENGINE_HOST_STATUS_INVALID_STATE = 2,
    ENGINE_HOST_STATUS_WRONG_THREAD = 3,
    ENGINE_HOST_STATUS_INIT_FAILED = 4,
    ENGINE_HOST_STATUS_GL_FAILED = 5,
    ENGINE_HOST_STATUS_SESSION_FAILED = 6,
    ENGINE_HOST_STATUS_TICK_FAULT = 7,
    ENGINE_HOST_STATUS_SHUTDOWN_COMPLETE = 8,
    ENGINE_HOST_STATUS_SHUTDOWN_FAILED = 9,
    ENGINE_HOST_STATUS_EDIT_ACCEPTED = 10,
    /// HRC-3 P2 新增（追加取值，v1.2）：请求的 presentation frame 自上次成功 fetch 之后
    /// **没有新的帧**。此时 `EngineHost_Fetch` 返回 0，**不写出任何 packet 字节**，
    /// 调用方必须复用自己已持有的上一帧 packet —— 但**由调用方决定**，本层绝不
    /// 静默重发旧像素来伪装成"新帧"。
ENGINE_HOST_STATUS_NO_NEW_FRAME = 11,
/// HRC-3 D3：已有 pending pick 结果尚未被取走时再次提交请求。
/// append-only，接在 `NO_NEW_FRAME = 11` 之后；**不重新编号任何既有 status**。
/// 配对 stage = `ENGINE_HOST_STAGE_EDIT`（既有值：busy 的成因就是 pending/edit 队列已占用）。
/// 注意：本 status **不**消费、也不替换 outstanding request。
ENGINE_HOST_STATUS_PICK_BUSY = 12
};

enum {
    ENGINE_HOST_STAGE_BOOTSTRAP = 0,
    ENGINE_HOST_STAGE_PLATFORM = 1,
    ENGINE_HOST_STAGE_GL_CONTEXT = 2,
    ENGINE_HOST_STAGE_GLAD = 3,
    ENGINE_HOST_STAGE_ENGINE_SUBSYSTEM = 4,
    ENGINE_HOST_STAGE_FBO = 5,
    ENGINE_HOST_STAGE_SESSION = 6,
    ENGINE_HOST_STAGE_OPEN_PROJECT = 7,
    ENGINE_HOST_STAGE_PLAY = 8,
    ENGINE_HOST_STAGE_TICK = 9,
    ENGINE_HOST_STAGE_SHUTDOWN = 10,
    ENGINE_HOST_STAGE_EDIT = 11
};

/// A7 op-dispatch seam：不带输出缓冲的 editor session 操作。
/// 全部经 EngineHost 的单一 Submit executor 进入 native runtime thread。
/// 参数按固定槽位解释（无序列化协议）：
///   arg0/arg1 = int32 索引或类型码；strArg = UTF-8 字符串；floatArg = 3 float。
enum {
    ENGINE_HOST_OP_NONE = 0,
    ENGINE_HOST_OP_CREATE_ENTITY = 1,
    ENGINE_HOST_OP_DELETE_ENTITY = 2,
    ENGINE_HOST_OP_RENAME_ENTITY = 3,
    ENGINE_HOST_OP_SET_ENTITY_POSITION = 4,
    ENGINE_HOST_OP_GET_ENTITY_COUNT = 5,
    ENGINE_HOST_OP_GET_ENTITY_NAME = 6,
    ENGINE_HOST_OP_GET_ASSET_COUNT = 7,
    ENGINE_HOST_OP_GET_ASSET_PATH = 8,
    ENGINE_HOST_OP_GET_ASSET_TYPE = 9,
    ENGINE_HOST_OP_GET_ASSET_GUID = 10,
    ENGINE_HOST_OP_IMPORT_ASSET = 11,
    ENGINE_HOST_OP_RENAME_ASSET = 12,
    ENGINE_HOST_OP_GET_ENTITY_SPRITE = 13,
    ENGINE_HOST_OP_GET_ENTITY_SCRIPT = 14,
    ENGINE_HOST_OP_ASSIGN_SCRIPT = 15,
    ENGINE_HOST_OP_SCRIPT_READ = 16,
    ENGINE_HOST_OP_SCRIPT_SAVE = 17,
    ENGINE_HOST_OP_SAVE_PROJECT = 18,
    ENGINE_HOST_OP_ADD_COMPONENT = 19,
    ENGINE_HOST_OP_REMOVE_COMPONENT = 20,
    ENGINE_HOST_OP_HAS_COMPONENT = 21,
    ENGINE_HOST_OP_GET_COMPONENT_PROPERTY = 22,
    ENGINE_HOST_OP_SET_COMPONENT_PROPERTY = 23,
    ENGINE_HOST_OP_IS_DIRTY = 24,
    ENGINE_HOST_OP_GET_WARNINGS = 25,
    ENGINE_HOST_OP_ASSIGN_SPRITE = 26,
    ENGINE_HOST_OP_GET_ENTITY_POSITION = 27,
    /// HRC-3 P2 新增（**Fetch-only**，v1.2）。通过既有 `EngineHost_Fetch` 取一整个
    /// presentation 帧。packet 是**静态容量**的（尺寸编译期固定，见
    /// ENGINE_HOST_PRESENTATION_* 与 ENGINE_HOST_FRAME_PACKET_*），无协商、无分配。
    /// 布局（packed，小端，偏移以字节计）：
    ///     0  int32  width
    ///     4  int32  height
    ///     8  uint64 frame_id      ← 与像素在**同一次 runtime-thread Submit** 内取得
    ///    16  uint8  rgba[width * height * 4]   ← GL 原生布局：行优先、左下角起
    /// 语义：
    ///   - 有新帧：返回 packet 字节数，`result->status = ENGINE_HOST_STATUS_SUCCESS`；
    ///   - 无新帧：返回 0，`result->status = ENGINE_HOST_STATUS_NO_NEW_FRAME`，
    ///     **不写任何字节**（调用方不会读到被污染/过期的数据）。
    /// `strArg*`/`arg0` 全部忽略。
ENGINE_HOST_OP_GET_PRESENTATION_FRAME = 28,
/// HRC-3 D3：pick 请求 / 结果。
///
/// 同一个 op 值在两个入口上语义不同（这正是"结果读取不得走普通 Dispatch 执行路径"
/// 的实现方式）：
///   - `EngineHost_Dispatch(PICK, floatArg=[x,y])` → 提交请求，返回 pending
///     operation_id（`RuntimeResult.operation_id`），status = `EDIT_ACCEPTED`；
///     仅表示"已受理"，执行发生在下一次 `PumpOneFrame` 内。
///   - `EngineHost_Fetch(PICK, out=packet)` → 取回 typed pick 结果。
ENGINE_HOST_OP_PICK = 29,
ENGINE_HOST_OP_COUNT = 30
};

/// HRC-3 P2：presentation frame packet 的容量常量（编译期，非 ABI 结构）。
/// header = width(4) + height(4) + frame_id(8) = 16 字节；像素 = w * h * 4。
/// 这是**最小/初始**容量（bootstrap 尺寸 800x600）；运行时容量按实际 FBO 尺寸算（P1.3）。
#define ENGINE_HOST_FRAME_PACKET_HEADER_BYTES 16
#define ENGINE_HOST_FRAME_PACKET_BYTES \
    (ENGINE_HOST_FRAME_PACKET_HEADER_BYTES + \
     ENGINE_HOST_PRESENTATION_WIDTH * ENGINE_HOST_PRESENTATION_HEIGHT * 4)

/// 需要 GL context 的 op：只能在 PumpOneFrame 的 current-context 窗口内执行。
enum {
    ENGINE_HOST_GL_OP_NONE = 0,
    ENGINE_HOST_GL_OP_ASSIGN_SPRITE = 1
};

enum {
    ENGINE_HOST_STATE_CREATED = 0,
    ENGINE_HOST_STATE_BOOTSTRAPPING = 1,
    ENGINE_HOST_STATE_READY = 2,
    ENGINE_HOST_STATE_STOPPED = 3,
    ENGINE_HOST_STATE_FAULTED = 4,
    ENGINE_HOST_STATE_DISPOSED = 5
};

/// HRC-3 D3：`PickResultPacket` 字节数（编译期常量，非 ABI 结构）。
/// struct_size(4) + status(4) + frame_id(8) + request_id(8) + entity_id(4) + reserved(4) = 32
#define ENGINE_HOST_PICK_RESULT_PACKET_BYTES 32

/// HRC-3 D3：pick 结果状态（**packet 内部**枚举，与 RuntimeResult.status 是两套东西）。
///
/// `NoHit` 与 `OutsideViewport` **必须**结构化区分：前者是"查询有效、该像素无实体"
/// （正常结果，sentinel -1），后者是"查询无效"（坐标越界，已显式拒绝，永不 clamp）。
/// 合并二者就等于把 D3-C6 已经拆开的东西重新糊在一起。
enum {
    ENGINE_HOST_PICK_HIT = 0,               ///< entity_id >= 1（D3-C2 有效区间）
    ENGINE_HOST_PICK_NO_HIT = 1,           ///< 正常未命中（sentinel -1），**不是失败**
    ENGINE_HOST_PICK_OUTSIDE_VIEWPORT = 2, ///< 越界：查询无效，已显式拒绝
    ENGINE_HOST_PICK_INVALID_COORDINATE = 3,///< 坐标非有限值
    ENGINE_HOST_PICK_NOT_READY = 4,        ///< 无 pick 纹理 / 非 owner thread / 无 context
    ENGINE_HOST_PICK_FAULT = 5             ///< host faulted / teardown 中
};

#pragma pack(push, 1)
/// HRC-3 D3：typed pick 结果。
///
/// 只搬**一个像素**的 readback —— ID 表面**不**跨 ABI 传输（与 P2 的 1,920,016B
/// 像素包形成对比，这是刻意的容量纪律）。
///
/// `frame_id` **必带**：D3-C4 要求 pick 结果可证明属于哪一帧，且不依赖调用方去读
/// `RuntimeResult.frame_id`，使 frame 绑定无法被忘记。
/// `request_id` 回显 `Dispatch` 侧的 `operation_id`；这是**唯一**的配对依据。
typedef struct PickResultPacket {
    uint32_t struct_size;   ///< 必须 == sizeof(PickResultPacket) == 32
    int32_t status;         ///< ENGINE_HOST_PICK_* 之一
    uint64_t frame_id;      ///< 产生该结果的那一帧（必填）
    uint64_t request_id;    ///< 回显 Dispatch 的 operation_id
    uint32_t entity_id;     ///< 仅 status == HIT 时有效，且 >= 1
    uint32_t reserved;      ///< 预留，避免再升版本
} PickResultPacket;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct RuntimeResult {
    uint32_t struct_size;
    uint32_t abi_version;
    int32_t status;
    int32_t stage;
    uint64_t operation_id;
    uint64_t frame_id;
    uint64_t generation;
    uint64_t owner_thread_id;
    int32_t state_before;
    int32_t state_after;
    int32_t error_code;
    uint32_t diagnostic_length;
    uint32_t diagnostic_truncated;
    float raw_dt_seconds;
    float effective_dt_seconds;
} RuntimeResult;

#pragma pack(pop)

typedef void (*EngineHostEventCallback)(int32_t eventType,
                                        uint64_t generation,
                                        const char* payload,
                                        void* userData);

EDITOR_BRIDGE_API int32_t EngineHost_GetApiVersion(void);
EDITOR_BRIDGE_API EngineHostHandle EngineHost_Create(RuntimeResult* result);
EDITOR_BRIDGE_API int32_t EngineHost_OpenProject(EngineHostHandle h,
                                                  const char* manifestPath,
                                                  const char* scenePath,
                                                  RuntimeResult* result);
EDITOR_BRIDGE_API int32_t EngineHost_Play(EngineHostHandle h,
                                          int32_t assetIndex,
                                          RuntimeResult* result);
EDITOR_BRIDGE_API int32_t EngineHost_Reload(EngineHostHandle h,
                                            RuntimeResult* result);
EDITOR_BRIDGE_API int32_t EngineHost_Stop(EngineHostHandle h,
                                          RuntimeResult* result);
EDITOR_BRIDGE_API int32_t EngineHost_PumpOneFrame(EngineHostHandle h,
                                                  float requestedDtSeconds,
                                                  RuntimeResult* result);
EDITOR_BRIDGE_API int32_t EngineHost_CloseProject(EngineHostHandle h,
                                                   RuntimeResult* result);
EDITOR_BRIDGE_API int32_t EngineHost_Shutdown(EngineHostHandle h,
                                               RuntimeResult* result);
EDITOR_BRIDGE_API void EngineHost_Destroy(EngineHostHandle h);
EDITOR_BRIDGE_API int32_t EngineHost_GetLastFrameResult(EngineHostHandle h,
                                                        RuntimeResult* result);
EDITOR_BRIDGE_API int32_t EngineHost_SetEventCallback(
    EngineHostHandle h, EngineHostEventCallback callback, void* userData);
EDITOR_BRIDGE_API int32_t EngineHost_GetLastDiagnostic(EngineHostHandle h,
                                                       char* out,
                                                       int32_t cap);
EDITOR_BRIDGE_API int32_t EngineHost_GetGraphicsState(EngineHostHandle h,
                                                       int32_t* ready,
                                                       int32_t* current);
EDITOR_BRIDGE_API int32_t EngineHost_GetGraphicsCounters(EngineHostHandle h,
                                                          uint64_t* makeCurrent,
                                                          uint64_t* uncurrent);
EDITOR_BRIDGE_API int32_t EngineHost_GetGlobalProofState(
    EngineHostHandle h,
    int32_t* applicationPresent,
    int32_t* timeInitialized,
    int32_t* sceneInitialized,
    int32_t* jobSystemPresent,
    int32_t* resourceManagerPresent,
    int32_t* fileWatcherPresent);
EDITOR_BRIDGE_API int32_t EngineHost_GetSessionProofState(
    EngineHostHandle h,
    EditorSessionRuntimeStats* stats);
EDITOR_BRIDGE_API int32_t EngineHost_GetRuntimeProofState(
    EngineHostHandle h,
    int32_t* applicationReady,
    int32_t* fboReady,
    int32_t* sessionAttached,
    int32_t* readbackVerified,
    uint32_t* fboId,
    uint32_t* fboWidth,
    uint32_t* fboHeight,
    uint64_t* frameCount,
    uint64_t* tickCount);
EDITOR_BRIDGE_API int32_t EngineHost_GetTeardownState(
    EngineHostHandle h,
    int32_t* emergencyRequested,
    int32_t* emergencyCompleted,
    int32_t* graphicsTeardownAttempted,
    int32_t* graphicsTeardownSucceeded,
    int32_t* lifecycleTimedOut);
EDITOR_BRIDGE_API int32_t EngineHost_GetGlfwLifetimeState(int32_t* users,
                                                          int32_t* initCount,
                                                          int32_t* terminateCount);

/// A7 op-dispatch seam。无输出缓冲的 session 操作。
/// 返回值语义：SESSION op = op 自身的标量结果（失败为负）；
/// GL op = pending edit id（>0），且 result.status 置为
/// ENGINE_HOST_STATUS_EDIT_ACCEPTED —— 仅表示“已受理”，不表示已执行。
EDITOR_BRIDGE_API int32_t EngineHost_Dispatch(EngineHostHandle h,
                                              int32_t op,
                                              int32_t arg0,
                                              int32_t arg1,
                                              const char* strArg,
                                              const char* strArg2,
                                              const char* strArg3,
                                              const float* floatArg,
                                              RuntimeResult* result);

/// A7 op-dispatch seam。带输出缓冲的 session 查询。
/// outBuffer 语义按 op 固定：UTF-8 字符串（cap 含 NUL）或 float3（cap >= 12）。
EDITOR_BRIDGE_API int32_t EngineHost_Fetch(EngineHostHandle h,
                                           int32_t op,
                                           int32_t arg0,
                                           const char* strArg,
                                           const char* strArg2,
                                           const char* strArg3,
                                           uint8_t* outBuffer,
                                           int32_t capacity,
                                           RuntimeResult* result);

/// A7 pending-edit completion seam。非阻塞。
/// 返回 0 = 已完成（result 填最终 status/diagnostic）；
/// 返回 1 = 仍在队列中；返回 -1 = 未知 edit id。
EDITOR_BRIDGE_API int32_t EngineHost_PollPendingEdit(EngineHostHandle h,
                                                      int64_t editId,
                                                      RuntimeResult* result);

/// A7 pending-edit proof：受理数 / 已执行 / 失败 / 最近一次执行时 context 是否 current /
/// 最近一次执行的 frameId。用于证明 GL 依赖 edit 只在 frame 内执行。
EDITOR_BRIDGE_API int32_t EngineHost_GetPendingEditProofState(
    EngineHostHandle h,
    int32_t* accepted,
    int32_t* executed,
    int32_t* failed,
    int32_t* lastExecutedWithContextCurrent,
    uint64_t* lastExecutedFrameId);
