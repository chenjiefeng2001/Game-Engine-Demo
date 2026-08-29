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

EDITOR_BRIDGE_API int32_t EditorSession_GetEntityPosition(EditorSessionHandle h,
                                                          int32_t index,
                                                          float out3[3]) {
    if (!h || !out3) return -1;
    return S(h)->GetEntityPosition(index, out3) ? 0 : -1;
}

EDITOR_BRIDGE_API int32_t EditorSession_SetEntityPosition(EditorSessionHandle h,
                                                          int32_t index,
                                                          const float pos3[3]) {
    if (!h) return -1;
    return S(h)->SetEntityPosition(index, pos3) ? 0 : -1;
}

EDITOR_BRIDGE_API int32_t EditorSession_DeleteEntity(EditorSessionHandle h,
                                                     int32_t index) {
    if (!h) return -1;
    return S(h)->DeleteEntity(index) ? 0 : -1;
}

EDITOR_BRIDGE_API int32_t EditorSession_RenameEntity(EditorSessionHandle h,
                                                     int32_t index,
                                                     const char* newName) {
    if (!h || !newName) return -1;
    return S(h)->RenameEntity(index, newName) ? 0 : -1;
}

EDITOR_BRIDGE_API int32_t EditorSession_GetEntitySprite(EditorSessionHandle h,
                                                        int32_t index,
                                                        char* out,
                                                        int32_t cap) {
    if (!h || !out || cap <= 0) return -1;
    std::string s;
    if (!S(h)->GetEntitySprite(index, &s)) return -1;
    const int32_t len = static_cast<int32_t>(s.size());
    const int32_t copy = (len < cap - 1) ? len : cap - 1;
    std::memcpy(out, s.data(), static_cast<size_t>(copy));
    out[copy] = '\0';
    return len;
}

EDITOR_BRIDGE_API int32_t EditorSession_AssignSprite(EditorSessionHandle h,
                                                     int32_t assetIndex,
                                                     int32_t entityIndex) {
    if (!h) return -1;
    return S(h)->AssignSprite(assetIndex, entityIndex) ? 0 : -1;
}

EDITOR_BRIDGE_API int32_t EditorSession_GetEntityScript(EditorSessionHandle h,
                                                        int32_t index,
                                                        char* out,
                                                        int32_t cap) {
    if (!h || !out || cap <= 0) return -1;
    std::string s;
    if (!S(h)->GetEntityScript(index, &s)) return -1;
    const int32_t len = static_cast<int32_t>(s.size());
    const int32_t copy = (len < cap - 1) ? len : cap - 1;
    std::memcpy(out, s.data(), static_cast<size_t>(copy));
    out[copy] = '\0';
    return len;
}

EDITOR_BRIDGE_API int32_t EditorSession_AssignScript(EditorSessionHandle h,
                                                     int32_t assetIndex,
                                                     int32_t entityIndex) {
    if (!h) return -1;
    return S(h)->AssignScript(assetIndex, entityIndex) ? 0 : -1;
}

// ── Component Contract（F1）：约定组件（Camera 等）──

static inline int32_t CStrCopyTo(const std::string& s, char* out, int32_t cap) {
    if (!out || cap <= 0) return -1;
    const int32_t len = static_cast<int32_t>(s.size());
    const int32_t copy = (len < cap - 1) ? len : cap - 1;
    std::memcpy(out, s.data(), static_cast<size_t>(copy));
    out[copy] = '\0';
    return len;
}

EDITOR_BRIDGE_API int32_t EditorSession_AddComponent(EditorSessionHandle h,
                                                     int32_t entityIndex,
                                                     const char* typeName) {
    if (!h || !typeName) return -1;
    return S(h)->AddComponent(entityIndex, typeName) ? 0 : -1;
}

EDITOR_BRIDGE_API int32_t EditorSession_RemoveComponent(EditorSessionHandle h,
                                                        int32_t entityIndex,
                                                        const char* typeName) {
    if (!h || !typeName) return -1;
    return S(h)->RemoveComponent(entityIndex, typeName) ? 0 : -1;
}

EDITOR_BRIDGE_API int32_t EditorSession_HasComponent(EditorSessionHandle h,
                                                     int32_t entityIndex,
                                                     const char* typeName) {
    if (!h || !typeName) return -1;
    return S(h)->HasComponent(entityIndex, typeName) ? 1 : 0;
}

EDITOR_BRIDGE_API int32_t EditorSession_GetComponentCount(EditorSessionHandle h,
                                                          int32_t entityIndex) {
    return h ? S(h)->GetComponentCount(entityIndex) : -1;
}

EDITOR_BRIDGE_API int32_t EditorSession_GetComponentTypeAt(
    EditorSessionHandle h, int32_t entityIndex, int32_t compIndex,
    char* out, int32_t cap) {
    if (!h || !out || cap <= 0) return -1;
    std::string tn;
    if (!S(h)->GetComponentTypeAt(entityIndex, compIndex, &tn)) return -1;
    return CStrCopyTo(tn, out, cap);
}

EDITOR_BRIDGE_API int32_t EditorSession_GetComponentProperty(
    EditorSessionHandle h, int32_t entityIndex, const char* typeName,
    const char* propName, char* out, int32_t cap) {
    if (!h || !typeName || !propName || !out || cap <= 0) return -1;
    std::string val;
    if (!S(h)->GetComponentProperty(entityIndex, typeName, propName, &val))
        return -1;
    return CStrCopyTo(val, out, cap);
}

EDITOR_BRIDGE_API int32_t EditorSession_SetComponentProperty(
    EditorSessionHandle h, int32_t entityIndex, const char* typeName,
    const char* propName, const char* valueStr) {
    if (!h || !typeName || !propName || !valueStr) return -1;
    return S(h)->SetComponentProperty(entityIndex, typeName, propName,
                                      valueStr) ? 0 : -1;
}

EDITOR_BRIDGE_API int32_t EditorSession_GetAssetGuid(EditorSessionHandle h,
                                                     int32_t index,
                                                     char* out,
                                                     int32_t cap) {
    if (!h || !out || cap <= 0) return -1;
    std::string guid;
    if (!S(h)->GetAssetGuid(index, &guid)) return -1;
    const int32_t len = static_cast<int32_t>(guid.size());
    const int32_t copy = (len < cap - 1) ? len : cap - 1;
    std::memcpy(out, guid.data(), static_cast<size_t>(copy));
    out[copy] = '\0';
    return len;
}

EDITOR_BRIDGE_API int32_t EditorSession_ImportAsset(EditorSessionHandle h,
                                                    const char* path,
                                                    int32_t type) {
    if (!h || !path) return -1;
    return S(h)->ImportAsset(path, type);
}

EDITOR_BRIDGE_API int32_t EditorSession_RenameAsset(EditorSessionHandle h,
                                                    int32_t assetIndex,
                                                    const char* newName) {
    if (!h || !newName) return -1;
    return S(h)->RenameAsset(assetIndex, newName) ? 0 : -1;
}

EDITOR_BRIDGE_API int32_t EditorSession_ScriptRead(EditorSessionHandle h,
                                                   int32_t assetIndex,
                                                   char* out,
                                                   int32_t cap) {
    if (!h || !out || cap <= 0) return -1;
    std::string text;
    if (!S(h)->ScriptRead(assetIndex, &text)) return -1;
    const int32_t len = static_cast<int32_t>(text.size());
    const int32_t copy = (len < cap - 1) ? len : cap - 1;
    std::memcpy(out, text.data(), static_cast<size_t>(copy));
    out[copy] = '\0';
    return len;
}

EDITOR_BRIDGE_API int32_t EditorSession_ScriptSave(EditorSessionHandle h,
                                                   int32_t assetIndex,
                                                   const char* text) {
    if (!h || !text) return -1;
    return S(h)->ScriptSave(assetIndex, text) ? 0 : -1;
}

EDITOR_BRIDGE_API int32_t EditorSession_Play(EditorSessionHandle h,
                                             int32_t assetIndex) {
    return (h && S(h)->Play(assetIndex)) ? 1 : 0;
}

EDITOR_BRIDGE_API int32_t EditorSession_Reload(EditorSessionHandle h) {
    return (h && S(h)->Reload()) ? 1 : 0;
}

EDITOR_BRIDGE_API void EditorSession_Stop(EditorSessionHandle h) {
    if (h) S(h)->Stop();
}

EDITOR_BRIDGE_API void EditorSession_RuntimeTick(EditorSessionHandle h,
                                                 float dt) {
    if (h) S(h)->RuntimeTick(dt);
}

EDITOR_BRIDGE_API int32_t EditorSession_IsPlaying(EditorSessionHandle h) {
    return (h && S(h)->IsPlaying()) ? 1 : 0;
}

EDITOR_BRIDGE_API int32_t EditorSession_RuntimePersistInt(
    EditorSessionHandle h, const char* key, int32_t defaultVal) {
    if (!h || !key) return defaultVal;
    return S(h)->RuntimePersistInt(key, defaultVal);
}

EDITOR_BRIDGE_API int32_t EditorSession_GetRuntimeError(EditorSessionHandle h,
                                                        char* out,
                                                        int32_t cap) {
    if (!h || !out || cap <= 0) return -1;
    const std::string& err = S(h)->GetRuntimeError();
    const int32_t len = static_cast<int32_t>(err.size());
    const int32_t copy = (len < cap - 1) ? len : cap - 1;
    std::memcpy(out, err.data(), static_cast<size_t>(copy));
    out[copy] = '\0';
    return len;
}

EDITOR_BRIDGE_API int32_t EditorSession_IsDirty(EditorSessionHandle h) {
    return (h && S(h)->IsDirty()) ? 1 : 0;
}

EDITOR_BRIDGE_API int32_t EditorSession_GetAssetPath(EditorSessionHandle h,
                                                     int32_t index,
                                                     char* out,
                                                     int32_t cap) {
    if (!h || !out || cap <= 0) return -1;
    std::string path;
    if (!S(h)->GetAssetPath(index, &path)) return -1;
    const int32_t len = static_cast<int32_t>(path.size());
    const int32_t copy = (len < cap - 1) ? len : cap - 1;
    std::memcpy(out, path.data(), static_cast<size_t>(copy));
    out[copy] = '\0';
    return len;
}

EDITOR_BRIDGE_API int32_t EditorSession_GetAssetType(EditorSessionHandle h,
                                                     int32_t index) {
    return h ? S(h)->GetAssetType(index) : -1;
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

EDITOR_BRIDGE_API int32_t EditorSession_GetWarnings(EditorSessionHandle h,
                                                    char* out,
                                                    int32_t cap) {
    if (!h || !out || cap <= 0) return -1;
    const std::string& w = S(h)->GetWarnings();
    const int32_t len = static_cast<int32_t>(w.size());
    const int32_t copy = (len < cap - 1) ? len : cap - 1;
    std::memcpy(out, w.data(), static_cast<size_t>(copy));
    out[copy] = '\0';
    return len;
}
