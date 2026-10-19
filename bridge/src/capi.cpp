/**
 * @file capi.cpp
 * @brief C ABI 实现 —— 纯翻译层，禁止业务逻辑（AV-002）
 */

#include "editor_bridge/capi.h"
#include "EngineHost.h"
#include "Engine/Platform/GlfwLifetime.h"
#include "EditorSession.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <Engine/Scripting/LuaEngine.h>  // required: unique_ptr<LuaEngine> destructor in EditorSession

using editor_bridge::EditorSession;

static EditorSession* S(EditorSessionHandle h) {
    return static_cast<EditorSession*>(h);
}

static editor_bridge::EngineHost* H(EngineHostHandle h) {
    return static_cast<editor_bridge::EngineHost*>(h);
}

static std::mutex s_MainThreadMutex;
static std::thread::id s_MainThreadId;
static bool s_MainThreadRegistered = false;

static int32_t RegisterMainThread() {
    std::lock_guard<std::mutex> lock(s_MainThreadMutex);
    if (!s_MainThreadRegistered) {
        s_MainThreadId = std::this_thread::get_id();
        s_MainThreadRegistered = true;
    }
    return s_MainThreadId == std::this_thread::get_id() ? 1 : 0;
}

static bool IsMainThread() {
    std::lock_guard<std::mutex> lock(s_MainThreadMutex);
    return s_MainThreadRegistered &&
           s_MainThreadId == std::this_thread::get_id();
}

static void InitRuntimeResult(RuntimeResult* result) noexcept {
    if (!result) return;
    std::memset(result, 0, sizeof(*result));
    result->struct_size = static_cast<uint32_t>(sizeof(RuntimeResult));
    result->abi_version = (1u << 16);
}

static int32_t RuntimeFailure(RuntimeResult* result,
                              int32_t status,
                              int32_t stage,
                              const char* diagnostic) noexcept {
    InitRuntimeResult(result);
    if (!result) return -1;
    result->status = status;
    result->stage = stage;
    result->error_code = 0;
    result->diagnostic_length = diagnostic
        ? static_cast<uint32_t>(std::strlen(diagnostic))
        : 0u;
    return -1;
}

static int32_t HostFailure(EngineHostHandle h,
                           RuntimeResult* result,
                           int32_t status,
                           int32_t stage,
                           const char* diagnostic) noexcept {
    if (h) {
        return H(h)->RecordFailure(result, status, stage, diagnostic);
    }
    return RuntimeFailure(result, status, stage, diagnostic);
}

static bool LegacyFailureInjected() {
    const char* value = std::getenv("EDITOR_BRIDGE_FAIL_LEGACY_OPERATION");
    return value && std::strcmp(value, "1") == 0;
}

template <typename Function>
static int32_t LegacyInt32(Function&& function, int32_t fallback = -1) noexcept {
    try {
        return function();
    } catch (...) {
        return fallback;
    }
}

template <typename Function>
static void LegacyVoid(Function&& function) noexcept {
    try {
        function();
    } catch (...) {
    }
}

template <typename Function>
static float LegacyFloat(Function&& function) noexcept {
    try {
        return function();
    } catch (...) {
        return -1.0f;
    }
}

template <typename Function>
static uint32_t LegacyUint32(Function&& function) noexcept {
    try {
        return function();
    } catch (...) {
        return 0;
    }
}

static int32_t RuntimeStatusForStage(int32_t stage) noexcept {
    switch (stage) {
    case ENGINE_HOST_STAGE_TICK:
        return ENGINE_HOST_STATUS_TICK_FAULT;
    case ENGINE_HOST_STAGE_SHUTDOWN:
        return ENGINE_HOST_STATUS_SHUTDOWN_FAILED;
    case ENGINE_HOST_STAGE_OPEN_PROJECT:
    case ENGINE_HOST_STAGE_PLAY:
    case ENGINE_HOST_STAGE_SESSION:
    case ENGINE_HOST_STAGE_EDIT:
        return ENGINE_HOST_STATUS_SESSION_FAILED;
    case ENGINE_HOST_STAGE_GL_CONTEXT:
    case ENGINE_HOST_STAGE_GLAD:
    case ENGINE_HOST_STAGE_FBO:
        return ENGINE_HOST_STATUS_GL_FAILED;
    default:
        return ENGINE_HOST_STATUS_INIT_FAILED;
    }
}

template <typename Function>
static int32_t RuntimeGuard(editor_bridge::EngineHost* host,
                            RuntimeResult* result,
                            int32_t stage,
                            Function&& function) noexcept {
    try {
        return function();
    } catch (const std::exception& exception) {
        if (host) {
            return host->RecordFailure(result,
                                       RuntimeStatusForStage(stage),
                                       stage,
                                       exception.what());
        }
        return RuntimeFailure(result,
                              RuntimeStatusForStage(stage),
                              stage,
                              exception.what());
    } catch (...) {
        if (host) {
            return host->RecordFailure(result,
                                       RuntimeStatusForStage(stage),
                                       stage,
                                       "unknown native exception");
        }
        return RuntimeFailure(result,
                              RuntimeStatusForStage(stage),
                              stage,
                              "unknown native exception");
    }
}

// ABI 版本：高位 = major，低位 = minor。变更签名时必须 bump。
static constexpr int32_t EDITOR_BRIDGE_VERSION = (1 << 16) | 1;  // v1.1

EDITOR_BRIDGE_API int32_t EditorBridge_GetVersion(void) {
    return EDITOR_BRIDGE_VERSION;
}

EDITOR_BRIDGE_API int32_t EditorBridge_RegisterMainThread(void) {
    if (RegisterMainThread() != 1) return 0;
    return Engine::RegisterGlfwOwnerThread() ? 1 : 0;
}

EDITOR_BRIDGE_API EditorSessionHandle EditorSession_Create(void) {
    if (!IsMainThread()) return nullptr;
    if (!Engine::RegisterGlfwOwnerThread()) return nullptr;
    try {
        auto* session = new (std::nothrow) EditorSession();
        if (!session) return nullptr;
        // 这里**不再**要求 GL 就绪。
        //
        // 旧代码在构造后立刻用 IsGraphicsReady() 判否，等于把"本机能建出 GL 4.6
        // 上下文"当成会话存在的前提。CI 上这让 EditorSession_Create() 直接返回
        // nullptr，test_editor_capi.cpp:57 的 fixture 断言随后失败，41 个用例在
        // SetUp 里全军覆没 —— 跑的不是契约，是这台机器有没有 GL。
        //
        // 而这层依赖本就多余：OpenGLGraphicsFactory 在建不出 4.6 上下文时是**优雅
        // 降级**的（记 m_LastError 后返回，不崩），TextureManager 的构造只存工厂
        // 引用，场景/实体的贴图绑定是延迟的 —— 也就是说会话、场景与 C ABI 契约
        // 本来就不需要 GL 上下文。
        //
        // 现在 GL 只在真正出图时才成为前提，这也正是 P1/P2 要求的 headless 宿主
        // 形态：Avalonia 侧不持有窗口系统的 GL 上下文。
        // 独占契约：EngineHost 持有 persistent GLFW lifetime 时，不允许再建一个
        // 独立的 legacy session（见 GlfwLifetime.cpp:42
        // "if (!persistent && s_GlfwPersistentUsers > 0) return false;"）。
        //
        // 这条以前是**顺带**由 IsGraphicsReady() 兜住的 —— factory 建不起来，
        // 于是 Create() 返回 nullptr。删掉那道闸时若不显式补回来，宿主持有平台的
        // 情况下外部仍能建出第二个 session，唯一所有者契约就漏了。
        // EngineHost 自己不走这个 C ABI（它直接用 EditorSession(factory, app)），
        // 所以这里不会误伤宿主路径。
        if (Engine::GlfwPersistentUsers() > 0) return nullptr;
        return session;
    } catch (...) {
        return nullptr;
    }
}

EDITOR_BRIDGE_API void EditorSession_Destroy(EditorSessionHandle h) {
    if (!h) return;
    auto* s = S(h);
    if (!s->IsCreationThread()) {
        std::fprintf(stderr, "EditorSession_Destroy called on non-owner thread\n");
        std::terminate();
    }
    try {
        s->SetEventCallback(nullptr);
        s->Stop();
        delete s;
    } catch (...) {
        std::terminate();
    }
}

EDITOR_BRIDGE_API int32_t EditorSession_OpenProject(EditorSessionHandle h,
                                                    const char* manifestPath,
                                                    const char* scenePath) {
    return LegacyInt32([&]() -> int32_t {
        if (LegacyFailureInjected()) {
            throw std::runtime_error("injected legacy operation failure");
        }
        if (!h || !manifestPath || !scenePath) return 0;
        return S(h)->OpenProject(manifestPath, scenePath) ? 1 : 0;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_SaveProject(EditorSessionHandle h) {
    return LegacyInt32([&]() -> int32_t {
        return (h && S(h)->SaveProject()) ? 1 : 0;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetEntityCount(EditorSessionHandle h) {
    return LegacyInt32([&]() -> int32_t {
        return h ? S(h)->GetEntityCount() : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetAssetCount(EditorSessionHandle h) {
    return LegacyInt32([&]() -> int32_t {
        return h ? S(h)->GetAssetCount() : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_CreateEntity(EditorSessionHandle h,
                                                     const char* name) {
    return LegacyInt32([&]() -> int32_t {
        return h ? S(h)->CreateEntity(name) : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetEntityName(EditorSessionHandle h,
                                                      int32_t index,
                                                      char* out,
                                                      int32_t cap) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !out || cap <= 0) return -1;
        std::string name;
        if (!S(h)->GetEntityName(index, &name)) return -1;
        const int32_t len = static_cast<int32_t>(name.size());
        const int32_t copy = (len < cap - 1) ? len : cap - 1;
        std::memcpy(out, name.data(), static_cast<size_t>(copy));
        out[copy] = '\0';
        return len;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetEntityPosition(EditorSessionHandle h,
                                                          int32_t index,
                                                          float out3[3]) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !out3) return -1;
        return S(h)->GetEntityPosition(index, out3) ? 0 : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_SetEntityPosition(EditorSessionHandle h,
                                                          int32_t index,
                                                          const float pos3[3]) {
    return LegacyInt32([&]() -> int32_t {
        if (!h) return -1;
        return S(h)->SetEntityPosition(index, pos3) ? 0 : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_DeleteEntity(EditorSessionHandle h,
                                                     int32_t index) {
    return LegacyInt32([&]() -> int32_t {
        if (!h) return -1;
        return S(h)->DeleteEntity(index) ? 0 : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_RenameEntity(EditorSessionHandle h,
                                                     int32_t index,
                                                     const char* newName) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !newName) return -1;
        return S(h)->RenameEntity(index, newName) ? 0 : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetEntitySprite(EditorSessionHandle h,
                                                        int32_t index,
                                                        char* out,
                                                        int32_t cap) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !out || cap <= 0) return -1;
        std::string s;
        if (!S(h)->GetEntitySprite(index, &s)) return -1;
        const int32_t len = static_cast<int32_t>(s.size());
        const int32_t copy = (len < cap - 1) ? len : cap - 1;
        std::memcpy(out, s.data(), static_cast<size_t>(copy));
        out[copy] = '\0';
        return len;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_AssignSprite(EditorSessionHandle h,
                                                     int32_t assetIndex,
                                                     int32_t entityIndex) {
    return LegacyInt32([&]() -> int32_t {
        if (!h) return -1;
        return S(h)->AssignSprite(assetIndex, entityIndex) ? 0 : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetEntityScript(EditorSessionHandle h,
                                                        int32_t index,
                                                        char* out,
                                                        int32_t cap) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !out || cap <= 0) return -1;
        std::string s;
        if (!S(h)->GetEntityScript(index, &s)) return -1;
        const int32_t len = static_cast<int32_t>(s.size());
        const int32_t copy = (len < cap - 1) ? len : cap - 1;
        std::memcpy(out, s.data(), static_cast<size_t>(copy));
        out[copy] = '\0';
        return len;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_AssignScript(EditorSessionHandle h,
                                                     int32_t assetIndex,
                                                     int32_t entityIndex) {
    return LegacyInt32([&]() -> int32_t {
        if (!h) return -1;
        return S(h)->AssignScript(assetIndex, entityIndex) ? 0 : -1;
    });
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
    return LegacyInt32([&]() -> int32_t {
        if (!h || !typeName) return -1;
        return S(h)->AddComponent(entityIndex, typeName) ? 0 : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_RemoveComponent(EditorSessionHandle h,
                                                        int32_t entityIndex,
                                                        const char* typeName) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !typeName) return -1;
        return S(h)->RemoveComponent(entityIndex, typeName) ? 0 : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_HasComponent(EditorSessionHandle h,
                                                     int32_t entityIndex,
                                                     const char* typeName) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !typeName) return -1;
        return S(h)->HasComponent(entityIndex, typeName) ? 1 : 0;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetComponentCount(EditorSessionHandle h,
                                                          int32_t entityIndex) {
    return LegacyInt32([&]() -> int32_t {
        return h ? S(h)->GetComponentCount(entityIndex) : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetComponentTypeAt(
    EditorSessionHandle h, int32_t entityIndex, int32_t compIndex,
    char* out, int32_t cap) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !out || cap <= 0) return -1;
        std::string tn;
        if (!S(h)->GetComponentTypeAt(entityIndex, compIndex, &tn)) return -1;
        return CStrCopyTo(tn, out, cap);
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetComponentProperty(
    EditorSessionHandle h, int32_t entityIndex, const char* typeName,
    const char* propName, char* out, int32_t cap) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !typeName || !propName || !out || cap <= 0) return -1;
        std::string val;
        if (!S(h)->GetComponentProperty(entityIndex, typeName, propName, &val))
            return -1;
        return CStrCopyTo(val, out, cap);
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_SetComponentProperty(
    EditorSessionHandle h, int32_t entityIndex, const char* typeName,
    const char* propName, const char* valueStr) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !typeName || !propName || !valueStr) return -1;
        return S(h)->SetComponentProperty(entityIndex, typeName, propName,
                                          valueStr) ? 0 : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetAssetGuid(EditorSessionHandle h,
                                                     int32_t index,
                                                     char* out,
                                                     int32_t cap) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !out || cap <= 0) return -1;
        std::string guid;
        if (!S(h)->GetAssetGuid(index, &guid)) return -1;
        const int32_t len = static_cast<int32_t>(guid.size());
        const int32_t copy = (len < cap - 1) ? len : cap - 1;
        std::memcpy(out, guid.data(), static_cast<size_t>(copy));
        out[copy] = '\0';
        return len;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_ImportAsset(EditorSessionHandle h,
                                                    const char* path,
                                                    int32_t type) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !path) return -1;
        return S(h)->ImportAsset(path, type);
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_RenameAsset(EditorSessionHandle h,
                                                    int32_t assetIndex,
                                                    const char* newName) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !newName) return -1;
        return S(h)->RenameAsset(assetIndex, newName) ? 0 : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_ScriptRead(EditorSessionHandle h,
                                                   int32_t assetIndex,
                                                   char* out,
                                                   int32_t cap) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !out || cap <= 0) return -1;
        std::string text;
        if (!S(h)->ScriptRead(assetIndex, &text)) return -1;
        const int32_t len = static_cast<int32_t>(text.size());
        const int32_t copy = (len < cap - 1) ? len : cap - 1;
        std::memcpy(out, text.data(), static_cast<size_t>(copy));
        out[copy] = '\0';
        return len;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_ScriptSave(EditorSessionHandle h,
                                                   int32_t assetIndex,
                                                   const char* text) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !text) return -1;
        return S(h)->ScriptSave(assetIndex, text) ? 0 : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_Play(EditorSessionHandle h,
                                             int32_t assetIndex) {
    return LegacyInt32([&]() -> int32_t {
        return (h && S(h)->Play(assetIndex)) ? 1 : 0;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_Reload(EditorSessionHandle h) {
    return LegacyInt32([&]() -> int32_t {
        return (h && S(h)->Reload()) ? 1 : 0;
    });
}

EDITOR_BRIDGE_API void EditorSession_Stop(EditorSessionHandle h) {
    LegacyVoid([&]() { if (h) S(h)->Stop(); });
}

EDITOR_BRIDGE_API void EditorSession_RuntimeTick(EditorSessionHandle h,
                                                 float dt) {
    LegacyVoid([&]() { if (h) S(h)->RuntimeTick(dt); });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetRuntimeStats(
    EditorSessionHandle h,
    EditorSessionRuntimeStats* stats) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !stats) return 0;
        EditorSession::RuntimeStats snapshot;
        if (!S(h)->GetRuntimeStats(&snapshot)) return 0;
        std::memset(stats, 0, sizeof(*stats));
        stats->struct_size = sizeof(*stats);
        stats->stop_count = snapshot.stopCount;
        stats->on_create_count = snapshot.onCreateCount;
        stats->on_destroy_count = snapshot.onDestroyCount;
        stats->gameplay_handle_count = snapshot.gameplayHandleCount;
        stats->playing = snapshot.playing ? 1 : 0;
        stats->runtime_scene_present = snapshot.runtimeScenePresent ? 1 : 0;
        stats->script_valid = snapshot.scriptValid ? 1 : 0;
        stats->callback_attached = snapshot.callbackAttached ? 1 : 0;
        stats->gameplay_scene_present = snapshot.gameplayScenePresent ? 1 : 0;
        stats->hud_dirty = snapshot.hudDirty ? 1 : 0;
        return 1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_IsPlaying(EditorSessionHandle h) {
    return LegacyInt32([&]() -> int32_t {
        return (h && S(h)->IsPlaying()) ? 1 : 0;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_RuntimePersistInt(
    EditorSessionHandle h, const char* key, int32_t defaultVal) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !key) return defaultVal;
        return S(h)->RuntimePersistInt(key, defaultVal);
    }, defaultVal);
}

EDITOR_BRIDGE_API int32_t EditorSession_GetRuntimeError(EditorSessionHandle h,
                                                        char* out,
                                                        int32_t cap) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !out || cap <= 0) return -1;
        const std::string& err = S(h)->GetRuntimeError();
        const int32_t len = static_cast<int32_t>(err.size());
        const int32_t copy = (len < cap - 1) ? len : cap - 1;
        std::memcpy(out, err.data(), static_cast<size_t>(copy));
        out[copy] = '\0';
        return len;
    });
}

// ── Viewport（Phase 7）──────────────────────────────────────────

EDITOR_BRIDGE_API uint32_t EditorSession_GetViewportTextureHandle(
    EditorSessionHandle h) {
    return LegacyUint32([&]() -> uint32_t {
        return h ? S(h)->GetViewportTextureHandle() : 0;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetViewportSize(
    EditorSessionHandle h, int32_t* outWidth, int32_t* outHeight) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !outWidth || !outHeight) return -1;
        S(h)->GetViewportSize(outWidth, outHeight);
        return 0;
    });
}

EDITOR_BRIDGE_API void EditorSession_SetViewportSize(
    EditorSessionHandle h, int32_t width, int32_t height) {
    LegacyVoid([&]() { if (h) S(h)->SetViewportSize(width, height); });
}

EDITOR_BRIDGE_API void EditorSession_ViewportTick(EditorSessionHandle h, float dt) {
    LegacyVoid([&]() { if (h) S(h)->ViewportTick(dt); });
}

EDITOR_BRIDGE_API void EditorSession_SetViewportCameraPosition(
    EditorSessionHandle h, float x, float y, float z) {
    LegacyVoid([&]() { if (h) S(h)->SetViewportCameraPosition(x, y, z); });
}

EDITOR_BRIDGE_API void EditorSession_GetViewportCameraPosition(
    EditorSessionHandle h, float out3[3]) {
    LegacyVoid([&]() { if (h && out3) S(h)->GetViewportCameraPosition(out3); });
}

EDITOR_BRIDGE_API void EditorSession_SetViewportCameraProjection(
    EditorSessionHandle h, int32_t type) {
    LegacyVoid([&]() { if (h) S(h)->SetViewportCameraProjection(type); });
}

EDITOR_BRIDGE_API void EditorSession_SetViewportCameraZoom(
    EditorSessionHandle h, float zoom) {
    LegacyVoid([&]() { if (h) S(h)->SetViewportCameraZoom(zoom); });
}

EDITOR_BRIDGE_API void EditorSession_ViewportMouseMove(
    EditorSessionHandle h, float x, float y) {
    LegacyVoid([&]() { if (h) S(h)->ViewportMouseMove(x, y); });
}

EDITOR_BRIDGE_API void EditorSession_ViewportMouseButton(
    EditorSessionHandle h, int32_t button, int32_t pressed) {
    LegacyVoid([&]() { if (h) S(h)->ViewportMouseButton(button, pressed); });
}

EDITOR_BRIDGE_API void EditorSession_ViewportScroll(
    EditorSessionHandle h, float delta) {
    LegacyVoid([&]() { if (h) S(h)->ViewportScroll(delta); });
}

EDITOR_BRIDGE_API void EditorSession_ViewportKeyDown(
    EditorSessionHandle h, int32_t key) {
    LegacyVoid([&]() { if (h) S(h)->ViewportKeyDown(key); });
}

EDITOR_BRIDGE_API void EditorSession_ViewportKeyUp(
    EditorSessionHandle h, int32_t key) {
    LegacyVoid([&]() { if (h) S(h)->ViewportKeyUp(key); });
}

EDITOR_BRIDGE_API int32_t EditorSession_ViewportPick(
    EditorSessionHandle h, float ndcX, float ndcY) {
    return LegacyInt32([&]() -> int32_t {
        return h ? S(h)->ViewportPick(ndcX, ndcY) : -1;
    });
}

EDITOR_BRIDGE_API void EditorSession_SetGizmoMode(
    EditorSessionHandle h, int32_t mode) {
    LegacyVoid([&]() { if (h) S(h)->SetGizmoMode(mode); });
}

EDITOR_BRIDGE_API void EditorSession_SetGizmoSpace(
    EditorSessionHandle h, int32_t space) {
    LegacyVoid([&]() { if (h) S(h)->SetGizmoSpace(space); });
}

EDITOR_BRIDGE_API void EditorSession_SetGizmoSnap(
    EditorSessionHandle h, int32_t enabled, float value) {
    LegacyVoid([&]() { if (h) S(h)->SetGizmoSnap(enabled, value); });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetGizmoMode(EditorSessionHandle h) {
    return LegacyInt32([&]() -> int32_t {
        return h ? S(h)->GetGizmoMode() : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetGizmoSpace(EditorSessionHandle h) {
    return LegacyInt32([&]() -> int32_t {
        return h ? S(h)->GetGizmoSpace() : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetGizmoSnapEnabled(EditorSessionHandle h) {
    return LegacyInt32([&]() -> int32_t {
        return h ? S(h)->GetGizmoSnapEnabled() : -1;
    });
}

EDITOR_BRIDGE_API float EditorSession_GetGizmoSnapValue(EditorSessionHandle h) {
    return LegacyFloat([&]() -> float {
        return h ? S(h)->GetGizmoSnapValue() : -1.0f;
    });
}

EDITOR_BRIDGE_API void EditorSession_SetViewportGridVisible(
    EditorSessionHandle h, int32_t visible) {
    LegacyVoid([&]() { if (h) S(h)->SetViewportGridVisible(visible); });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetViewportGridVisible(
    EditorSessionHandle h) {
    return LegacyInt32([&]() -> int32_t {
        return h ? S(h)->GetViewportGridVisible() : -1;
    });
}

EDITOR_BRIDGE_API void EditorSession_SetCameraExternalInput(
    EditorSessionHandle h, int32_t enabled) {
    LegacyVoid([&]() { if (h) S(h)->SetCameraExternalInput(enabled); });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetCameraExternalInput(
    EditorSessionHandle h) {
    return LegacyInt32([&]() -> int32_t {
        return h ? S(h)->GetCameraExternalInput() : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetViewportPixels(
    EditorSessionHandle h, uint8_t* outBuffer, int32_t bufferSize) {
    return LegacyInt32([&]() -> int32_t {
        return h ? S(h)->GetViewportPixels(outBuffer, bufferSize) : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_IsDirty(EditorSessionHandle h) {
    return LegacyInt32([&]() -> int32_t {
        return h ? (S(h)->IsDirty() ? 1 : 0) : -1;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetAssetPath(EditorSessionHandle h,
                                                     int32_t index,
                                                     char* out,
                                                     int32_t cap) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !out || cap <= 0) return -1;
        std::string path;
        if (!S(h)->GetAssetPath(index, &path)) return -1;
        const int32_t len = static_cast<int32_t>(path.size());
        const int32_t copy = (len < cap - 1) ? len : cap - 1;
        std::memcpy(out, path.data(), static_cast<size_t>(copy));
        out[copy] = '\0';
        return len;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetAssetType(EditorSessionHandle h,
                                                     int32_t index) {
    return LegacyInt32([&]() -> int32_t {
        return h ? S(h)->GetAssetType(index) : -1;
    });
}

EDITOR_BRIDGE_API void EditorSession_SetEventCallback(
    EditorSessionHandle h, EditorEventCallback cb, void* userData) {
    LegacyVoid([&]() {
        if (!h) return;
        if (!cb) { S(h)->SetEventCallback(nullptr); return; }
        S(h)->SetEventCallback([cb, userData](int32_t type, const char* payload) {
            try {
                cb(type, payload, userData);
            } catch (...) {
            }
        });
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetLastError(EditorSessionHandle h,
                                                     char* out,
                                                     int32_t cap) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !out || cap <= 0) return -1;
        const std::string& err = S(h)->GetLastError();
        const int32_t len = static_cast<int32_t>(err.size());
        const int32_t copy = (len < cap - 1) ? len : cap - 1;
        std::memcpy(out, err.data(), static_cast<size_t>(copy));
        out[copy] = '\0';
        return len;
    });
}

EDITOR_BRIDGE_API int32_t EditorSession_GetWarnings(EditorSessionHandle h,
                                                     char* out,
                                                     int32_t cap) {
    return LegacyInt32([&]() -> int32_t {
        if (!h || !out || cap <= 0) return -1;
        const std::string& w = S(h)->GetWarnings();
        const int32_t len = static_cast<int32_t>(w.size());
        const int32_t copy = (len < cap - 1) ? len : cap - 1;
        std::memcpy(out, w.data(), static_cast<size_t>(copy));
        out[copy] = '\0';
        return len;
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_GetApiVersion(void) {
    return ENGINE_HOST_ABI_VERSION;
}

EDITOR_BRIDGE_API EngineHostHandle EngineHost_Create(RuntimeResult* result) {
    if (!result) return nullptr;
    if (!IsMainThread()) {
        RuntimeFailure(result,
                       ENGINE_HOST_STATUS_WRONG_THREAD,
                       ENGINE_HOST_STAGE_PLATFORM,
                       "EngineHost_Create must run on the registered main thread");
        return nullptr;
    }

    editor_bridge::EngineHost* host = nullptr;
    try {
        host = new (std::nothrow) editor_bridge::EngineHost();
        if (!host) {
            RuntimeFailure(result,
                           ENGINE_HOST_STATUS_INIT_FAILED,
                           ENGINE_HOST_STAGE_BOOTSTRAP,
                           "host allocation failed");
            return nullptr;
        }
        if (host->Initialize(result) != 0) {
            delete host;
            return nullptr;
        }
        return host;
    } catch (const std::exception& exception) {
        delete host;
        RuntimeFailure(result,
                       ENGINE_HOST_STATUS_INIT_FAILED,
                       ENGINE_HOST_STAGE_BOOTSTRAP,
                       exception.what());
    } catch (...) {
        delete host;
        RuntimeFailure(result,
                       ENGINE_HOST_STATUS_INIT_FAILED,
                       ENGINE_HOST_STAGE_BOOTSTRAP,
                       "unknown host bootstrap exception");
    }
    return nullptr;
}

EDITOR_BRIDGE_API int32_t EngineHost_OpenProject(EngineHostHandle h,
                                                  const char* manifestPath,
                                                  const char* scenePath,
                                                  RuntimeResult* result) {
    if (!h || !manifestPath || !scenePath || !result) {
        return HostFailure(h,
                           result,
                           ENGINE_HOST_STATUS_SESSION_FAILED,
                           ENGINE_HOST_STAGE_OPEN_PROJECT,
                           "invalid host or project path");
    }
    return RuntimeGuard(H(h), result, ENGINE_HOST_STAGE_OPEN_PROJECT, [&]() {
        return H(h)->OpenProject(manifestPath, scenePath, result);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_Play(EngineHostHandle h,
                                          int32_t assetIndex,
                                          RuntimeResult* result) {
    if (!h || !result) {
        return RuntimeFailure(result,
                              ENGINE_HOST_STATUS_INVALID_STATE,
                              ENGINE_HOST_STAGE_PLAY,
                              "host handle or result is null");
    }
    return RuntimeGuard(H(h), result, ENGINE_HOST_STAGE_PLAY, [&]() {
        return H(h)->Play(assetIndex, result);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_Reload(EngineHostHandle h,
                                            RuntimeResult* result) {
    if (!h || !result) {
        return RuntimeFailure(result,
                              ENGINE_HOST_STATUS_INVALID_STATE,
                              ENGINE_HOST_STAGE_PLAY,
                              "host handle or result is null");
    }
    return RuntimeGuard(H(h), result, ENGINE_HOST_STAGE_PLAY, [&]() {
        return H(h)->Reload(result);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_Stop(EngineHostHandle h,
                                          RuntimeResult* result) {
    if (!h || !result) {
        return RuntimeFailure(result,
                              ENGINE_HOST_STATUS_INVALID_STATE,
                              ENGINE_HOST_STAGE_SHUTDOWN,
                              "host handle or result is null");
    }
    return RuntimeGuard(H(h), result, ENGINE_HOST_STAGE_SHUTDOWN, [&]() {
        return H(h)->Stop(result);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_PumpOneFrame(EngineHostHandle h,
                                                  float requestedDtSeconds,
                                                  RuntimeResult* result) {
    if (!h || !result) {
        return RuntimeFailure(result,
                              ENGINE_HOST_STATUS_INVALID_STATE,
                              ENGINE_HOST_STAGE_TICK,
                              "host handle or result is null");
    }
    return RuntimeGuard(H(h), result, ENGINE_HOST_STAGE_TICK, [&]() {
        return H(h)->PumpOneFrame(requestedDtSeconds, result);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_CloseProject(EngineHostHandle h,
                                                   RuntimeResult* result) {
    if (!h || !result) {
        return RuntimeFailure(result,
                              ENGINE_HOST_STATUS_INVALID_STATE,
                              ENGINE_HOST_STAGE_OPEN_PROJECT,
                              "host handle or result is null");
    }
    return RuntimeGuard(H(h), result, ENGINE_HOST_STAGE_OPEN_PROJECT, [&]() {
        return H(h)->CloseProject(result);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_Shutdown(EngineHostHandle h,
                                               RuntimeResult* result) {
    if (!h || !result) {
        return RuntimeFailure(result,
                              ENGINE_HOST_STATUS_INVALID_STATE,
                              ENGINE_HOST_STAGE_SHUTDOWN,
                              "host handle or result is null");
    }
    return RuntimeGuard(H(h), result, ENGINE_HOST_STAGE_SHUTDOWN, [&]() {
        return H(h)->Shutdown(result);
    });
}

EDITOR_BRIDGE_API void EngineHost_Destroy(EngineHostHandle h) {
    if (!h) return;
    auto* host = H(h);
    if (host->IsOwnerThread() || !host->IsCreationThread()) {
        std::fprintf(stderr, "EngineHost_Destroy called on invalid thread\n");
        std::terminate();
    }
    delete host;
}

EDITOR_BRIDGE_API int32_t EngineHost_GetLastFrameResult(EngineHostHandle h,
                                                        RuntimeResult* result) {
    if (!h || !result) return -1;
    return RuntimeGuard(H(h), result, ENGINE_HOST_STAGE_TICK, [&]() {
        return H(h)->GetLastFrameResult(result);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_SetEventCallback(
    EngineHostHandle h, EngineHostEventCallback callback, void* userData) {
    if (!h) return -1;
    return RuntimeGuard(H(h), nullptr, ENGINE_HOST_STAGE_BOOTSTRAP, [&]() {
        return H(h)->SetEventCallback(callback, userData);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_GetLastDiagnostic(EngineHostHandle h,
                                                       char* out,
                                                       int32_t cap) {
    if (!h || cap < 0 || (!out && cap != 0)) return -1;
    return RuntimeGuard(H(h), nullptr, ENGINE_HOST_STAGE_BOOTSTRAP, [&]() {
        return H(h)->GetLastDiagnostic(out, cap);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_GetGraphicsState(EngineHostHandle h,
                                                       int32_t* ready,
                                                       int32_t* current) {
    if (!h || !ready || !current) return -1;
    return RuntimeGuard(H(h), nullptr, ENGINE_HOST_STAGE_GL_CONTEXT, [&]() {
        return H(h)->GetGraphicsState(ready, current);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_GetGraphicsCounters(EngineHostHandle h,
                                                         uint64_t* makeCurrent,
                                                         uint64_t* uncurrent) {
    if (!h || !makeCurrent || !uncurrent) return -1;
    return RuntimeGuard(H(h), nullptr, ENGINE_HOST_STAGE_GL_CONTEXT, [&]() {
        return H(h)->GetGraphicsCounters(makeCurrent, uncurrent);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_GetGlobalProofState(
    EngineHostHandle h,
    int32_t* applicationPresent,
    int32_t* timeInitialized,
    int32_t* sceneInitialized,
    int32_t* jobSystemPresent,
    int32_t* resourceManagerPresent,
    int32_t* fileWatcherPresent) {
    if (!h) return -1;
    return RuntimeGuard(H(h), nullptr, ENGINE_HOST_STAGE_ENGINE_SUBSYSTEM, [&]() {
        return H(h)->GetGlobalProofState(applicationPresent,
                                          timeInitialized,
                                          sceneInitialized,
                                          jobSystemPresent,
                                          resourceManagerPresent,
                                          fileWatcherPresent);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_GetSessionProofState(
    EngineHostHandle h,
    EditorSessionRuntimeStats* stats) {
    if (!h || !stats) return -1;
    return RuntimeGuard(H(h), nullptr, ENGINE_HOST_STAGE_SESSION, [&]() {
        return H(h)->GetSessionProofState(stats);
    });
}

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
    uint64_t* tickCount) {
    if (!h) return -1;
    return RuntimeGuard(H(h), nullptr, ENGINE_HOST_STAGE_TICK, [&]() {
        return H(h)->GetRuntimeProofState(applicationReady,
                                          fboReady,
                                          sessionAttached,
                                          readbackVerified,
                                          fboId,
                                          fboWidth,
                                          fboHeight,
                                          frameCount,
                                          tickCount);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_GetTeardownState(
    EngineHostHandle h,
    int32_t* emergencyRequested,
    int32_t* emergencyCompleted,
    int32_t* graphicsTeardownAttempted,
    int32_t* graphicsTeardownSucceeded,
    int32_t* lifecycleTimedOut) {
    if (!h || !emergencyRequested || !emergencyCompleted ||
        !graphicsTeardownAttempted || !graphicsTeardownSucceeded ||
        !lifecycleTimedOut) return -1;
    return RuntimeGuard(H(h), nullptr, ENGINE_HOST_STAGE_SHUTDOWN, [&]() {
        return H(h)->GetTeardownState(emergencyRequested,
                                      emergencyCompleted,
                                      graphicsTeardownAttempted,
                                      graphicsTeardownSucceeded,
                                      lifecycleTimedOut);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_GetGlfwLifetimeState(int32_t* users,
                                                          int32_t* initCount,
                                                          int32_t* terminateCount) {
    if (!users || !initCount || !terminateCount) return -1;
    *users = Engine::GlfwLifetimeUsers();
    *initCount = Engine::GlfwInitCount();
    *terminateCount = Engine::GlfwTerminateCount();
    return 0;
}

EDITOR_BRIDGE_API int32_t EngineHost_Dispatch(EngineHostHandle h,
                                              int32_t op,
                                              int32_t arg0,
                                              int32_t arg1,
                                              const char* strArg,
                                              const char* strArg2,
                                              const char* strArg3,
                                              const float* floatArg,
                                              RuntimeResult* result) {
    if (!h || !result) {
        return RuntimeFailure(result,
                              ENGINE_HOST_STATUS_INVALID_STATE,
                              ENGINE_HOST_STAGE_EDIT,
                              "host handle or result is null");
    }
    return RuntimeGuard(H(h), result, ENGINE_HOST_STAGE_EDIT, [&]() {
        return H(h)->Dispatch(op,
                              arg0,
                              arg1,
                              strArg,
                              strArg2,
                              strArg3,
                              floatArg,
                              result);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_Fetch(EngineHostHandle h,
                                           int32_t op,
                                           int32_t arg0,
                                           const char* strArg,
                                           const char* strArg2,
                                           const char* strArg3,
                                           uint8_t* outBuffer,
                                           int32_t capacity,
                                           RuntimeResult* result) {
    if (!h || !result) {
        return RuntimeFailure(result,
                              ENGINE_HOST_STATUS_INVALID_STATE,
                              ENGINE_HOST_STAGE_EDIT,
                              "host handle or result is null");
    }
    return RuntimeGuard(H(h), result, ENGINE_HOST_STAGE_EDIT, [&]() {
        return H(h)->Fetch(op,
                           arg0,
                           strArg,
                           strArg2,
                           strArg3,
                           outBuffer,
                           capacity,
                           result);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_PollPendingEdit(EngineHostHandle h,
                                                      int64_t editId,
                                                      RuntimeResult* result) {
    if (!h || !result) {
        return RuntimeFailure(result,
                              ENGINE_HOST_STATUS_INVALID_STATE,
                              ENGINE_HOST_STAGE_EDIT,
                              "host handle or result is null");
    }
    return RuntimeGuard(H(h), result, ENGINE_HOST_STAGE_EDIT, [&]() {
        return H(h)->PollPendingEdit(editId, result);
    });
}

EDITOR_BRIDGE_API int32_t EngineHost_GetPendingEditProofState(
    EngineHostHandle h,
    int32_t* accepted,
    int32_t* executed,
    int32_t* failed,
    int32_t* lastExecutedWithContextCurrent,
    uint64_t* lastExecutedFrameId) {
    if (!h) return -1;
    return RuntimeGuard(H(h), nullptr, ENGINE_HOST_STAGE_EDIT, [&]() {
        return H(h)->GetPendingEditProofState(accepted,
                                              executed,
                                              failed,
                                              lastExecutedWithContextCurrent,
                                              lastExecutedFrameId);
    });
}
