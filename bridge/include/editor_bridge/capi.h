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
/// 写实体位置（编辑态；越界索引拒绝）。成功广播 EV_ENTITY_MOVED。
EDITOR_BRIDGE_API int32_t EditorSession_SetEntityPosition(EditorSessionHandle h,
                                                          int32_t index,
                                                          const float pos3[3]);

// ── 资产查询（Asset Browser Phase 4 的最小前驱）────────────────

EDITOR_BRIDGE_API int32_t EditorSession_GetAssetPath(EditorSessionHandle h,
                                                     int32_t index,
                                                     char* out,
                                                     int32_t cap);
/// 0=Texture 1=Script 2=Unknown
EDITOR_BRIDGE_API int32_t EditorSession_GetAssetType(EditorSessionHandle h,
                                                     int32_t index);

// ── 事件 ─────────────────────────────────────────────────────

EDITOR_BRIDGE_API void EditorSession_SetEventCallback(
    EditorSessionHandle h, EditorEventCallback cb, void* userData);

// ── 诊断 ─────────────────────────────────────────────────────

/// 取最近一次失败的 UTF-8 错误文本（无错误返回空串）。
EDITOR_BRIDGE_API int32_t EditorSession_GetLastError(EditorSessionHandle h,
                                                     char* out,
                                                     int32_t cap);
