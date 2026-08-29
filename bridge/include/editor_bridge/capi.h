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
 */

#include <stdint.h>

#if defined(_WIN32) && defined(EDITOR_BRIDGE_EXPORTS)
#define EDITOR_BRIDGE_API extern "C" __declspec(dllexport)
#else
#define EDITOR_BRIDGE_API extern "C"
#endif

typedef void* EditorSessionHandle;

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
};

// ── 会话生命周期 ──────────────────────────────────────────────

EDITOR_BRIDGE_API EditorSessionHandle EditorSession_Create(void);
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
