/**
 * @file capi.cpp
 * @brief C ABI 实现 —— 纯翻译层，禁止业务逻辑（AV-002）
 */

#include "editor_bridge/capi.h"
#include "EditorSession.h"

using editor_bridge::EditorSession;

static EditorSession* S(EditorSessionHandle h) {
    return static_cast<EditorSession*>(h);
}

EDITOR_BRIDGE_API EditorSessionHandle EditorSession_Create(void) {
    return new (std::nothrow) EditorSession();
}

EDITOR_BRIDGE_API void EditorSession_Destroy(EditorSessionHandle h) {
    delete S(h);
}

EDITOR_BRIDGE_API int32_t EditorSession_OpenProject(EditorSessionHandle h,
                                                    const char* manifestPath,
                                                    const char* scenePath) {
    if (!h || !manifestPath || !scenePath) return 0;
    return S(h)->OpenProject(manifestPath, scenePath) ? 1 : 0;
}

EDITOR_BRIDGE_API int32_t EditorSession_SaveProject(EditorSessionHandle h) {
    return (h && S(h)->SaveProject()) ? 1 : 0;
}

EDITOR_BRIDGE_API int32_t EditorSession_GetEntityCount(EditorSessionHandle h) {
    return h ? S(h)->GetEntityCount() : -1;
}

EDITOR_BRIDGE_API int32_t EditorSession_GetAssetCount(EditorSessionHandle h) {
    return h ? S(h)->GetAssetCount() : -1;
}

EDITOR_BRIDGE_API int32_t EditorSession_CreateEntity(EditorSessionHandle h,
                                                     const char* name) {
    return h ? S(h)->CreateEntity(name) : -1;
}

EDITOR_BRIDGE_API int32_t EditorSession_GetEntityName(EditorSessionHandle h,
                                                      int32_t index,
                                                      char* out,
                                                      int32_t cap) {
    if (!h || !out || cap <= 0) return -1;
    std::string name;
    if (!S(h)->GetEntityName(index, &name)) return -1;
    const int32_t len = static_cast<int32_t>(name.size());
    const int32_t copy = (len < cap - 1) ? len : cap - 1;
    std::memcpy(out, name.data(), static_cast<size_t>(copy));
    out[copy] = '\0';
    return len;
}

EDITOR_BRIDGE_API void EditorSession_SetEventCallback(
    EditorSessionHandle h, EditorEventCallback cb, void* userData) {
    if (!h) return;
    if (!cb) { S(h)->SetEventCallback(nullptr); return; }
    S(h)->SetEventCallback([cb, userData](int32_t type, const char* payload) {
        cb(type, payload, userData);
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetLastError(EditorSessionHandle h,
                                                     char* out,
                                                     int32_t cap) {
    if (!h || !out || cap <= 0) return -1;
    const std::string& err = S(h)->GetLastError();
    const int32_t len = static_cast<int32_t>(err.size());
    const int32_t copy = (len < cap - 1) ? len : cap - 1;
    std::memcpy(out, err.data(), static_cast<size_t>(copy));
    out[copy] = '\0';
    return len;
}
