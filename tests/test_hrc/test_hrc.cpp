#include <gtest/gtest.h>
#include "editor_bridge/capi.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <atomic>

namespace {

std::atomic<int> g_EventCount{0};

void CountEvent(int32_t, uint64_t, const char*, void*) {
    g_EventCount.fetch_add(1);
}

void SetFailureFlag(const char* name, bool enabled) {
#ifdef _WIN32
    _putenv_s(name, enabled ? "1" : "");
#else
    if (enabled) setenv(name, "1", 1);
    else unsetenv(name);
#endif
}

struct RuntimeState {
    int32_t applicationReady = 0;
    int32_t fboReady = 0;
    int32_t sessionAttached = 0;
    int32_t readbackVerified = 0;
    uint32_t fboId = 0;
    uint32_t fboWidth = 0;
    uint32_t fboHeight = 0;
    uint64_t frameCount = 0;
    uint64_t tickCount = 0;
};

int32_t ReadState(EngineHostHandle host, RuntimeState* state) {
    return EngineHost_GetRuntimeProofState(
        host,
        &state->applicationReady,
        &state->fboReady,
        &state->sessionAttached,
        &state->readbackVerified,
        &state->fboId,
        &state->fboWidth,
        &state->fboHeight,
        &state->frameCount,
        &state->tickCount);
}

}

TEST(Hrc2A4, ThreeRuntimeCycles) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    int32_t baselineUsers = 0;
    int32_t baselineInitCount = 0;
    int32_t baselineTerminateCount = 0;
    ASSERT_EQ(EngineHost_GetGlfwLifetimeState(&baselineUsers,
                                              &baselineInitCount,
                                              &baselineTerminateCount), 0);

    for (int cycle = 0; cycle < 3; ++cycle) {
        RuntimeResult result = {};
        EngineHostHandle host = EngineHost_Create(&result);
        ASSERT_NE(host, nullptr) << "cycle=" << cycle;
        EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SUCCESS);
        g_EventCount.store(0);
        ASSERT_EQ(EngineHost_SetEventCallback(host, CountEvent, nullptr), 0);

        RuntimeState state;
        ASSERT_EQ(ReadState(host, &state), 0);
        EXPECT_EQ(state.applicationReady, 1);
        EXPECT_EQ(state.fboReady, 1);
        EXPECT_EQ(state.sessionAttached, 1);
        EXPECT_EQ(state.readbackVerified, 0);
        EXPECT_NE(state.fboId, 0u);
        EXPECT_EQ(state.fboWidth, 64u);
        EXPECT_EQ(state.fboHeight, 64u);
        int32_t graphicsReady = 0;
        int32_t graphicsCurrent = 0;
        ASSERT_EQ(EngineHost_GetGraphicsState(host, &graphicsReady, &graphicsCurrent), 0);
        EXPECT_EQ(graphicsReady, 1);
        EXPECT_EQ(graphicsCurrent, 1);
        const uint64_t ownerThreadId = result.owner_thread_id;

        RuntimeResult wrongThreadResult = {};
        int32_t wrongThreadCode = 0;
        std::thread wrongThread([&]() {
            wrongThreadCode = EngineHost_Shutdown(host, &wrongThreadResult);
        });
        wrongThread.join();
        EXPECT_EQ(wrongThreadCode, -1);
        EXPECT_EQ(wrongThreadResult.status, ENGINE_HOST_STATUS_WRONG_THREAD);

        ASSERT_EQ(EngineHost_OpenProject(host,
                                         "assets/gp01/manifest.json",
                                         "assets/gp01/Main.scene",
                                         &result), 0);
        ASSERT_EQ(EngineHost_Play(host, -1, &result), 0);
        ASSERT_EQ(EngineHost_Reload(host, &result), 0);
        EXPECT_GT(g_EventCount.load(), 0);
        for (uint64_t frame = 1; frame <= 3; ++frame) {
            ASSERT_EQ(EngineHost_PumpOneFrame(host, 0.5f, &result), 0);
            EXPECT_EQ(result.raw_dt_seconds, 0.5f);
            EXPECT_EQ(result.effective_dt_seconds, 0.1f);
            EXPECT_EQ(result.frame_id, frame);
            EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SUCCESS);
            EXPECT_EQ(result.owner_thread_id, ownerThreadId);
            ASSERT_EQ(EngineHost_GetGraphicsState(host, &graphicsReady, &graphicsCurrent), 0);
            EXPECT_EQ(graphicsCurrent, 0);
        }
        ASSERT_EQ(ReadState(host, &state), 0);
        EXPECT_EQ(state.readbackVerified, 1);
        EXPECT_EQ(state.frameCount, 3u);
        EXPECT_EQ(state.tickCount, 3u);

        EXPECT_EQ(EngineHost_Stop(host, &result), 0);
        const int eventsBeforeClose = g_EventCount.load();
        EXPECT_EQ(EngineHost_CloseProject(host, &result), 0);
        EXPECT_EQ(g_EventCount.load(), eventsBeforeClose);
        EXPECT_EQ(EngineHost_Shutdown(host, &result), 0);
        EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SHUTDOWN_COMPLETE);
        int32_t emergencyRequested = 0;
        int32_t emergencyCompleted = 0;
        int32_t graphicsTeardownAttempted = 0;
        int32_t graphicsTeardownSucceeded = 0;
        int32_t lifecycleTimedOut = 0;
        ASSERT_EQ(EngineHost_GetTeardownState(host,
                                              &emergencyRequested,
                                              &emergencyCompleted,
                                              &graphicsTeardownAttempted,
                                              &graphicsTeardownSucceeded,
                                              &lifecycleTimedOut), 0);
        EXPECT_EQ(emergencyRequested, 0);
        EXPECT_EQ(emergencyCompleted, 0);
        EXPECT_EQ(graphicsTeardownAttempted, 1);
        EXPECT_EQ(graphicsTeardownSucceeded, 1);
        EXPECT_EQ(lifecycleTimedOut, 0);
        int32_t cycleUsers = 0;
        int32_t cycleInitCount = 0;
        int32_t cycleTerminateCount = 0;
        ASSERT_EQ(EngineHost_GetGlfwLifetimeState(&cycleUsers,
                                                   &cycleInitCount,
                                                   &cycleTerminateCount), 0);
        EXPECT_EQ(cycleUsers, baselineUsers);
        EXPECT_EQ(cycleInitCount, baselineInitCount + cycle + 1);
        EXPECT_EQ(cycleTerminateCount, baselineTerminateCount + cycle + 1);
        ASSERT_EQ(ReadState(host, &state), 0);
        EXPECT_EQ(state.applicationReady, 0);
        EXPECT_EQ(state.fboReady, 0);
        EXPECT_EQ(state.sessionAttached, 0);
        EXPECT_EQ(state.fboId, 0u);
        const int eventsBeforeDestroy = g_EventCount.load();
        EngineHost_Destroy(host);
        EXPECT_EQ(g_EventCount.load(), eventsBeforeDestroy);

        int32_t users = 0;
        int32_t initCount = 0;
        int32_t terminateCount = 0;
        ASSERT_EQ(EngineHost_GetGlfwLifetimeState(&users,
                                                  &initCount,
                                                  &terminateCount), 0);
        EXPECT_EQ(users, baselineUsers);
    }
}

TEST(Hrc2A4, ProjectFailureDoesNotPoisonHost) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    RuntimeResult result = {};
    EngineHostHandle host = EngineHost_Create(&result);
    ASSERT_NE(host, nullptr);
    EXPECT_EQ(EngineHost_OpenProject(host,
                                    "assets/gp01/missing-manifest.json",
                                    "assets/gp01/missing-scene.json",
                                    &result), -1);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SESSION_FAILED);
    EXPECT_EQ(EngineHost_OpenProject(host,
                                     "assets/gp01/manifest.json",
                                     "assets/gp01/Main.scene",
                                     &result), 0);
    EXPECT_EQ(EngineHost_PumpOneFrame(host, 0.016f, &result), 0);
    EXPECT_EQ(EngineHost_Shutdown(host, &result), 0);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SHUTDOWN_COMPLETE);
    EngineHost_Destroy(host);
}

TEST(Hrc2A4, ProjectFailurePreservesActiveSession) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    RuntimeResult result = {};
    EngineHostHandle host = EngineHost_Create(&result);
    ASSERT_NE(host, nullptr);
    ASSERT_EQ(EngineHost_OpenProject(host,
                                     "assets/gp01/manifest.json",
                                     "assets/gp01/Main.scene",
                                     &result), 0);
    ASSERT_EQ(EngineHost_Play(host, -1, &result), 0);
    EXPECT_EQ(EngineHost_OpenProject(host,
                                     "assets/gp01/missing-manifest.json",
                                     "assets/gp01/missing-scene.json",
                                     &result), -1);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SESSION_FAILED);
    EXPECT_EQ(EngineHost_PumpOneFrame(host, 0.016f, &result), 0);
    EXPECT_EQ(EngineHost_Shutdown(host, &result), 0);
    EngineHost_Destroy(host);
}

TEST(Hrc2A4, RuntimeTickFailureIsTyped) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    RuntimeResult result = {};
    EngineHostHandle host = EngineHost_Create(&result);
    ASSERT_NE(host, nullptr);
    ASSERT_EQ(EngineHost_OpenProject(host,
                                     "assets/gp01/manifest.json",
                                     "assets/gp01/Main.scene",
                                     &result), 0);
    ASSERT_EQ(EngineHost_Play(host, -1, &result), 0);
    SetFailureFlag("EDITOR_BRIDGE_FAIL_RUNTIME_TICK", true);
    EXPECT_EQ(EngineHost_PumpOneFrame(host, 0.016f, &result), -1);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_TICK_FAULT);
    EXPECT_EQ(result.stage, ENGINE_HOST_STAGE_TICK);
    int32_t graphicsReady = 0;
    int32_t graphicsCurrent = 0;
    EXPECT_EQ(EngineHost_GetGraphicsState(host,
                                          &graphicsReady,
                                          &graphicsCurrent), -1);
    RuntimeState state;
    ASSERT_EQ(ReadState(host, &state), 0);
    EXPECT_EQ(state.readbackVerified, 0);
    EXPECT_EQ(state.tickCount, 0u);
    SetFailureFlag("EDITOR_BRIDGE_FAIL_RUNTIME_TICK", false);
    EXPECT_EQ(EngineHost_Shutdown(host, &result), -1);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SHUTDOWN_FAILED);
    EngineHost_Destroy(host);
}

TEST(Hrc2A4, ReloadFailureStopsSession) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    RuntimeResult result = {};
    EngineHostHandle host = EngineHost_Create(&result);
    ASSERT_NE(host, nullptr);
    ASSERT_EQ(EngineHost_OpenProject(host,
                                     "assets/gp01/manifest.json",
                                     "assets/gp01/Main.scene",
                                     &result), 0);
    ASSERT_EQ(EngineHost_Play(host, -1, &result), 0);
    SetFailureFlag("EDITOR_BRIDGE_FAIL_RELOAD", true);
    EXPECT_EQ(EngineHost_Reload(host, &result), -1);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SESSION_FAILED);
    SetFailureFlag("EDITOR_BRIDGE_FAIL_RELOAD", false);
    EXPECT_EQ(EngineHost_PumpOneFrame(host, 0.016f, &result), 0);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SKIPPED_NOT_PLAYING);
    RuntimeState state;
    ASSERT_EQ(ReadState(host, &state), 0);
    EXPECT_EQ(state.tickCount, 0u);
    EXPECT_EQ(EngineHost_Shutdown(host, &result), 0);
    EngineHost_Destroy(host);
}

TEST(Hrc2A4, ResourceFailureRollback) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    int32_t baselineUsers = 0;
    int32_t baselineInitCount = 0;
    int32_t baselineTerminateCount = 0;
    ASSERT_EQ(EngineHost_GetGlfwLifetimeState(&baselineUsers,
                                              &baselineInitCount,
                                              &baselineTerminateCount), 0);
    struct FailureCase {
        const char* name;
        int32_t status;
        int32_t stage;
    };
    const FailureCase cases[] = {
        {"EDITOR_BRIDGE_FAIL_FBO", ENGINE_HOST_STATUS_GL_FAILED, ENGINE_HOST_STAGE_FBO},
        {"EDITOR_BRIDGE_FAIL_APPLICATION_INIT", ENGINE_HOST_STATUS_INIT_FAILED, ENGINE_HOST_STAGE_ENGINE_SUBSYSTEM},
        {"EDITOR_BRIDGE_FAIL_SESSION_ATTACH", ENGINE_HOST_STATUS_SESSION_FAILED, ENGINE_HOST_STAGE_SESSION},
    };
    for (const auto& failure : cases) {
        SetFailureFlag(failure.name, true);
        RuntimeResult result = {};
        EXPECT_EQ(EngineHost_Create(&result), nullptr);
        EXPECT_EQ(result.status, failure.status);
        EXPECT_EQ(result.stage, failure.stage);
        SetFailureFlag(failure.name, false);
        int32_t users = 0;
        int32_t initCount = 0;
        int32_t terminateCount = 0;
        ASSERT_EQ(EngineHost_GetGlfwLifetimeState(&users,
                                                  &initCount,
                                                  &terminateCount), 0);
        EXPECT_EQ(users, baselineUsers);
    }
}

TEST(Hrc2A6, SessionProofAndExactlyOnceStop) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    RuntimeResult result = {};
    EngineHostHandle host = EngineHost_Create(&result);
    ASSERT_NE(host, nullptr);
    EditorSessionRuntimeStats stats = {};
    ASSERT_EQ(EngineHost_GetSessionProofState(host, &stats), 0);
    EXPECT_EQ(stats.session_present, 1);
    EXPECT_EQ(stats.playing, 0);
    EXPECT_EQ(stats.allocator_present, 1);
    EXPECT_EQ(stats.stop_count, 0u);

    ASSERT_EQ(EngineHost_OpenProject(host,
                                     "assets/gp01/manifest.json",
                                     "assets/gp01/Main.scene",
                                     &result), 0);
    ASSERT_EQ(EngineHost_Play(host, -1, &result), 0);
    ASSERT_EQ(EngineHost_GetSessionProofState(host, &stats), 0);
    EXPECT_EQ(stats.playing, 1);
    EXPECT_EQ(stats.runtime_scene_present, 1);
    EXPECT_EQ(stats.script_valid, 1);
    EXPECT_EQ(stats.gameplay_scene_present, 1);
    EXPECT_EQ(stats.on_create_count, 1u);
    EXPECT_EQ(stats.on_destroy_count, 0u);

    ASSERT_EQ(EngineHost_Reload(host, &result), 0);
    ASSERT_EQ(EngineHost_GetSessionProofState(host, &stats), 0);
    EXPECT_EQ(stats.on_create_count, 2u);
    EXPECT_EQ(EngineHost_Stop(host, &result), 0);
    ASSERT_EQ(EngineHost_GetSessionProofState(host, &stats), 0);
    EXPECT_EQ(stats.playing, 0);
    EXPECT_EQ(stats.runtime_scene_present, 0);
    EXPECT_EQ(stats.script_valid, 0);
    EXPECT_EQ(stats.stop_count, 1u);
    EXPECT_EQ(stats.hud_dirty, 0);

    ASSERT_EQ(EngineHost_CloseProject(host, &result), 0);
    ASSERT_EQ(EngineHost_GetSessionProofState(host, &stats), 0);
    EXPECT_EQ(stats.session_present, 0);
    EXPECT_EQ(stats.callback_attached, 0);
    EXPECT_EQ(stats.gameplay_scene_present, 0);
    EXPECT_EQ(stats.gameplay_handle_count, 0u);
    EXPECT_EQ(stats.hud_dirty, 0);
    EXPECT_EQ(EngineHost_Shutdown(host, &result), 0);
    ASSERT_EQ(EngineHost_GetSessionProofState(host, &stats), 0);
    EXPECT_EQ(stats.allocator_present, 0);
    EngineHost_Destroy(host);
}

TEST(Hrc2A6, CloseDetachesBeforeStopCallback) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    g_EventCount.store(0);
    RuntimeResult result = {};
    EngineHostHandle host = EngineHost_Create(&result);
    ASSERT_NE(host, nullptr);
    ASSERT_EQ(EngineHost_SetEventCallback(host, CountEvent, nullptr), 0);
    ASSERT_EQ(EngineHost_OpenProject(host,
                                     "assets/gp01/manifest.json",
                                     "assets/gp01/Main.scene",
                                     &result), 0);
    ASSERT_EQ(EngineHost_Play(host, -1, &result), 0);
    const int beforeClose = g_EventCount.load();
    ASSERT_EQ(EngineHost_CloseProject(host, &result), 0);
    EXPECT_EQ(g_EventCount.load(), beforeClose);
    EditorSessionRuntimeStats stats = {};
    ASSERT_EQ(EngineHost_GetSessionProofState(host, &stats), 0);
    EXPECT_EQ(stats.session_present, 0);
    EXPECT_EQ(stats.callback_attached, 0);
    EXPECT_EQ(stats.stop_count, 1u);
    EXPECT_EQ(EngineHost_Shutdown(host, &result), 0);
    EngineHost_Destroy(host);
}

TEST(Hrc2A4, GlobalStateReturnsToBaseline) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    int32_t applicationPresent = 0;
    int32_t timeInitialized = 0;
    int32_t sceneInitialized = 0;
    int32_t jobSystemPresent = 0;
    int32_t resourceManagerPresent = 0;
    int32_t fileWatcherPresent = 0;
    RuntimeResult result = {};
    EngineHostHandle host = EngineHost_Create(&result);
    ASSERT_NE(host, nullptr);
    ASSERT_EQ(EngineHost_GetGlobalProofState(host,
                                            &applicationPresent,
                                            &timeInitialized,
                                            &sceneInitialized,
                                            &jobSystemPresent,
                                            &resourceManagerPresent,
                                            &fileWatcherPresent), 0);
    EXPECT_EQ(applicationPresent, 1);
    EXPECT_EQ(timeInitialized, 1);
    EXPECT_EQ(sceneInitialized, 0);
    EXPECT_EQ(jobSystemPresent, 0);
    EXPECT_EQ(resourceManagerPresent, 0);
    EXPECT_EQ(fileWatcherPresent, 0);
    EXPECT_EQ(EngineHost_Shutdown(host, &result), 0);
    ASSERT_EQ(EngineHost_GetGlobalProofState(host,
                                            &applicationPresent,
                                            &timeInitialized,
                                            &sceneInitialized,
                                            &jobSystemPresent,
                                            &resourceManagerPresent,
                                            &fileWatcherPresent), 0);
    EXPECT_EQ(applicationPresent, 0);
    EXPECT_EQ(timeInitialized, 0);
    EXPECT_EQ(sceneInitialized, 0);
    EXPECT_EQ(jobSystemPresent, 0);
    EXPECT_EQ(resourceManagerPresent, 0);
    EXPECT_EQ(fileWatcherPresent, 0);
    EngineHost_Destroy(host);
}

// ════════════════════════════════════════════════════════════
// A7-1 native op-dispatch seam + pending-edit queue
// G1-01 routing / G1-02 diagnostic / G1-03 queue / G1-04 GL affinity
// G1-05 boundary invariant / G1-06 single pump / G1-07 failure / G1-08 A6 regression
// ════════════════════════════════════════════════════════════

namespace a7 {

struct PendingEditProof {
    int32_t accepted = 0;
    int32_t executed = 0;
    int32_t failed = 0;
    int32_t lastExecutedWithContextCurrent = 0;
    uint64_t lastExecutedFrameId = 0;
};

int32_t ReadPendingEditProof(EngineHostHandle host, PendingEditProof* proof) {
    return EngineHost_GetPendingEditProofState(
        host,
        &proof->accepted,
        &proof->executed,
        &proof->failed,
        &proof->lastExecutedWithContextCurrent,
        &proof->lastExecutedFrameId);
}

std::string FetchString(EngineHostHandle host,
                        int32_t op,
                        int32_t arg0,
                        const char* strArg,
                        const char* strArg2,
                        RuntimeResult* result) {
    char buffer[4096] = {};
    const int32_t code = EngineHost_Fetch(host,
                                          op,
                                          arg0,
                                          strArg,
                                          strArg2,
                                          nullptr,
                                          reinterpret_cast<uint8_t*>(buffer),
                                          static_cast<int32_t>(sizeof(buffer)),
                                          result);
    if (code < 0) return std::string();
    return std::string(buffer);
}

std::string ReadDiagnostic(EngineHostHandle host) {
    char buffer[512] = {};
    if (EngineHost_GetLastDiagnostic(host, buffer, static_cast<int32_t>(sizeof(buffer))) < 0) {
        return std::string();
    }
    return std::string(buffer);
}

int32_t Dispatch(EngineHostHandle host,
                 int32_t op,
                 int32_t arg0,
                 int32_t arg1,
                 const char* strArg,
                 const char* strArg2,
                 const char* strArg3,
                 RuntimeResult* result) {
    return EngineHost_Dispatch(host,
                               op,
                               arg0,
                               arg1,
                               strArg,
                               strArg2,
                               strArg3,
                               nullptr,
                               result);
}

/// 复制 GP01 到 scratch 并把 CWD 指过去：写操作（SaveProject / ScriptSave）
/// 只落在副本上，不污染 repo 资产。返回原 CWD 供还原。
/// 与 test_bridge 的 MakeScratchFacing 同一约定。
std::string MakeScratchFacing() {
    namespace fs = std::filesystem;
    const fs::path scratch = "gp01_hrc_a7_scratch";
    fs::remove_all(scratch);
    const fs::path destination = scratch / "assets" / "gp01";
    fs::create_directories(destination);
    fs::copy("assets/gp01", destination,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    const std::string previous = fs::current_path().string();
    fs::current_path(scratch);
    return previous;
}

}  // namespace a7

// G1-01：26 个 op 全部经 Host seam；legacy EditorSession 在 persistent 模式下不可创建。
TEST(Hrc2A7, OpDispatchRoutesEverySessionCall) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    // 写操作落在 scratch 副本上。
    const std::string previousCwd = a7::MakeScratchFacing();

    RuntimeResult result = {};
    EngineHostHandle host = EngineHost_Create(&result);
    ASSERT_NE(host, nullptr);

    // 未 OpenProject 时，依赖场景的 op 给出 typed 失败而不是崩溃。
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_CREATE_ENTITY, 0, 0,
                           "Ghost", nullptr, nullptr, &result), -1);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SESSION_FAILED);
    EXPECT_EQ(result.stage, ENGINE_HOST_STAGE_EDIT);
    EXPECT_NE(a7::ReadDiagnostic(host).empty(), true);
    // 未 OpenProject 时 fetch 同样被拒绝。
    EXPECT_EQ(a7::FetchString(host, ENGINE_HOST_OP_GET_WARNINGS, 0, nullptr, nullptr, &result)
                  .empty(),
              true);

    // 未知 op 被拒绝。
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_COUNT, 0, 0, nullptr, nullptr, nullptr, &result), -1);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_INVALID_STATE);
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_NONE, 0, 0, nullptr, nullptr, nullptr, &result), -1);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_INVALID_STATE);
    // fetch op 不能走 Dispatch，反之亦然。
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_GET_ENTITY_NAME, 0, 0,
                           nullptr, nullptr, nullptr, &result), -1);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_INVALID_STATE);

    ASSERT_EQ(EngineHost_OpenProject(host,
                                     "assets/gp01/manifest.json",
                                     "assets/gp01/Main.scene",
                                     &result), 0);

    // ── dispatch ops（无输出缓冲）──
    const int32_t entityCount = a7::Dispatch(host, ENGINE_HOST_OP_GET_ENTITY_COUNT, 0, 0,
                                             nullptr, nullptr, nullptr, &result);
    ASSERT_GE(entityCount, 0);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SUCCESS);
    EXPECT_EQ(result.stage, ENGINE_HOST_STAGE_EDIT);
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_GET_ASSET_COUNT, 0, 0,
                           nullptr, nullptr, nullptr, &result), 33);
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_GET_ASSET_TYPE, 0, 0,
                           nullptr, nullptr, nullptr, &result), 0);
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_IS_DIRTY, 0, 0,
                           nullptr, nullptr, nullptr, &result), 0);

    const int32_t newIndex = a7::Dispatch(host, ENGINE_HOST_OP_CREATE_ENTITY, 0, 0,
                                          "A7Probe", nullptr, nullptr, &result);
    ASSERT_GE(newIndex, 0);
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_RENAME_ENTITY, newIndex, 0,
                           "A7ProbeRenamed", nullptr, nullptr, &result), 0);
    const float position[3] = {1.0f, 2.0f, 3.0f};
    ASSERT_EQ(EngineHost_Dispatch(host,
                                  ENGINE_HOST_OP_SET_ENTITY_POSITION,
                                  newIndex,
                                  0,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  position,
                                  &result), 0);
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_HAS_COMPONENT, newIndex, 0,
                           "Camera", nullptr, nullptr, &result), 0);
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_ADD_COMPONENT, newIndex, 0,
                           "Camera", nullptr, nullptr, &result), 0);
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_HAS_COMPONENT, newIndex, 0,
                           "Camera", nullptr, nullptr, &result), 1);
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_SET_COMPONENT_PROPERTY, newIndex, 0,
                           "Camera", "enabled", "false", &result), 0);
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_ASSIGN_SCRIPT, 32, newIndex,
                           nullptr, nullptr, nullptr, &result), 0);
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_SCRIPT_SAVE, 32, 0,
                           "function OnCreate() return true end", nullptr, nullptr, &result), 0);
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_SAVE_PROJECT, 0, 0,
                           nullptr, nullptr, nullptr, &result), 0);
    // ImportAsset 幂等：已登记的 script 返回既有索引。
    EXPECT_GE(a7::Dispatch(host, ENGINE_HOST_OP_IMPORT_ASSET, 1, 0,
                           "assets/gp01/game.lua", nullptr, nullptr, &result), 0);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SUCCESS);
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_DELETE_ENTITY, newIndex, 0,
                           nullptr, nullptr, nullptr, &result), 0);
    // 负向：不存在的文件 / 非法类型被拒绝。
    EXPECT_LT(a7::Dispatch(host, ENGINE_HOST_OP_IMPORT_ASSET, 0, 0,
                           "assets/gp01/tex/no_such_file.png", nullptr, nullptr, &result), 0);
    EXPECT_LT(a7::Dispatch(host, ENGINE_HOST_OP_IMPORT_ASSET, 42, 0,
                           "assets/gp01/game.lua", nullptr, nullptr, &result), 0);

    // ── fetch ops（带输出缓冲）──
    EXPECT_EQ(a7::FetchString(host, ENGINE_HOST_OP_GET_WARNINGS, 0, nullptr, nullptr, &result)
                  .empty(),
              true);
    EXPECT_EQ(a7::FetchString(host, ENGINE_HOST_OP_SCRIPT_READ, 32, nullptr, nullptr, &result),
              "function OnCreate() return true end");
    float positionOut[3] = {};
    ASSERT_EQ(EngineHost_Fetch(host,
                               ENGINE_HOST_OP_GET_ENTITY_POSITION,
                               0,
                               nullptr,
                               nullptr,
                               nullptr,
                               reinterpret_cast<uint8_t*>(positionOut),
                               static_cast<int32_t>(sizeof(positionOut)),
                               &result),
              static_cast<int32_t>(sizeof(positionOut)));
    // 缓冲区容量不足必须被拒绝。
    EXPECT_EQ(EngineHost_Fetch(host,
                               ENGINE_HOST_OP_GET_ENTITY_POSITION,
                               0,
                               nullptr,
                               nullptr,
                               nullptr,
                               reinterpret_cast<uint8_t*>(positionOut),
                               4,
                               &result),
              -1);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_INVALID_STATE);
    // fetch op 不能走 Fetch。
    EXPECT_EQ(EngineHost_Fetch(host,
                               ENGINE_HOST_OP_CREATE_ENTITY,
                               0,
                               nullptr,
                               nullptr,
                               nullptr,
                               reinterpret_cast<uint8_t*>(positionOut),
                               static_cast<int32_t>(sizeof(positionOut)),
                               &result),
              -1);

    ASSERT_EQ(EngineHost_Shutdown(host, &result), 0);
    EngineHost_Destroy(host);
    std::filesystem::current_path(previousCwd);
}

// G1-02：GetLastError / GetRuntimeError 统一落到 EngineHost_GetLastDiagnostic。
TEST(Hrc2A7, DiagnosticUnification) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    RuntimeResult result = {};
    EngineHostHandle host = EngineHost_Create(&result);
    ASSERT_NE(host, nullptr);
    ASSERT_EQ(EngineHost_OpenProject(host,
                                     "assets/gp01/manifest.json",
                                     "assets/gp01/Main.scene",
                                     &result), 0);

    // 成功操作清空诊断。
    ASSERT_GE(a7::Dispatch(host, ENGINE_HOST_OP_GET_ENTITY_COUNT, 0, 0,
                           nullptr, nullptr, nullptr, &result), 0);
    EXPECT_EQ(a7::ReadDiagnostic(host).empty(), true);

    // 失败操作把 session 错误文本送到 host diagnostic。
    EXPECT_EQ(a7::Dispatch(host, ENGINE_HOST_OP_DELETE_ENTITY, 9999, 0,
                           nullptr, nullptr, nullptr, &result), -1);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SESSION_FAILED);
    EXPECT_EQ(result.stage, ENGINE_HOST_STAGE_EDIT);
    const std::string diagnostic = a7::ReadDiagnostic(host);
    EXPECT_FALSE(diagnostic.empty());
    EXPECT_NE(diagnostic.find("delete entity"), std::string::npos) << diagnostic;

    ASSERT_EQ(EngineHost_Shutdown(host, &result), 0);
    EngineHost_Destroy(host);
}

// G1-03 + G1-04 + G1-05：enqueue → pump → execute，且只在 context current 窗口内。
TEST(Hrc2A7, PendingEditRunsInsideFrameWithContextCurrent) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    RuntimeResult result = {};
    EngineHostHandle host = EngineHost_Create(&result);
    ASSERT_NE(host, nullptr);
    ASSERT_EQ(EngineHost_OpenProject(host,
                                     "assets/gp01/manifest.json",
                                     "assets/gp01/Main.scene",
                                     &result), 0);

    a7::PendingEditProof proof;
    ASSERT_EQ(a7::ReadPendingEditProof(host, &proof), 0);
    EXPECT_EQ(proof.accepted, 0);
    EXPECT_EQ(proof.executed, 0);
    EXPECT_EQ(proof.failed, 0);

    const int32_t entityIndex = a7::Dispatch(host, ENGINE_HOST_OP_CREATE_ENTITY, 0, 0,
                                              "A7Sprite", nullptr, nullptr, &result);
    ASSERT_GE(entityIndex, 0);

    // GL 依赖 op：Dispatch 只受理，不同步执行。
    const int32_t editId = EngineHost_Dispatch(host,
                                               ENGINE_HOST_OP_ASSIGN_SPRITE,
                                               0,
                                               entityIndex,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               &result);
    ASSERT_GT(editId, 0);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_EDIT_ACCEPTED);
    EXPECT_EQ(result.stage, ENGINE_HOST_STAGE_EDIT);
    EXPECT_EQ(result.operation_id, static_cast<uint64_t>(editId));
    EXPECT_EQ(static_cast<int32_t>(result.operation_id), editId);
    // accepted 之后、pump 之前：仍在队列中。
    EXPECT_EQ(EngineHost_PollPendingEdit(host, editId, &result), 1);
    ASSERT_EQ(a7::ReadPendingEditProof(host, &proof), 0);
    EXPECT_EQ(proof.accepted, 1);
    EXPECT_EQ(proof.executed, 0);
    // 受理不等于执行：sprite 尚未绑定。
    EXPECT_EQ(a7::FetchString(host, ENGINE_HOST_OP_GET_ENTITY_SPRITE, entityIndex,
                              nullptr, nullptr, &result)
                  .empty(),
              true);

    // 下一帧排空。
    ASSERT_EQ(EngineHost_PumpOneFrame(host, 0.016f, &result), 0);
    EXPECT_EQ(EngineHost_PollPendingEdit(host, editId, &result), 0);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SUCCESS);
    EXPECT_EQ(result.frame_id, 1u);

    ASSERT_EQ(a7::ReadPendingEditProof(host, &proof), 0);
    EXPECT_EQ(proof.accepted, 1);
    EXPECT_EQ(proof.executed, 1);
    EXPECT_EQ(proof.failed, 0);
    // G1-04：执行时 context 必须是 current。
    EXPECT_EQ(proof.lastExecutedWithContextCurrent, 1);
    // G1-05：执行发生在真实帧内。
    EXPECT_EQ(proof.lastExecutedFrameId, 1u);
    // G1-05：帧边界 context 仍不 current。
    int32_t graphicsReady = 0;
    int32_t graphicsCurrent = 1;
    ASSERT_EQ(EngineHost_GetGraphicsState(host, &graphicsReady, &graphicsCurrent), 0);
    EXPECT_EQ(graphicsCurrent, 0);

    EXPECT_NE(a7::FetchString(host, ENGINE_HOST_OP_GET_ENTITY_SPRITE, entityIndex,
                              nullptr, nullptr, &result)
                  .empty(),
              true);

    // 多帧连续：每帧边界都不 current。
    for (uint64_t frame = 2; frame <= 4; ++frame) {
        ASSERT_EQ(EngineHost_PumpOneFrame(host, 0.016f, &result), 0);
        EXPECT_EQ(result.frame_id, frame);
        ASSERT_EQ(EngineHost_GetGraphicsState(host, &graphicsReady, &graphicsCurrent), 0);
        EXPECT_EQ(graphicsCurrent, 0);
    }

    // 未知 edit id 不会被当成成功。
    EXPECT_EQ(EngineHost_PollPendingEdit(host, editId + 9999, &result), -1);
    EXPECT_EQ(EngineHost_PollPendingEdit(host, 0, &result), -1);

    ASSERT_EQ(EngineHost_Shutdown(host, &result), 0);
    EngineHost_Destroy(host);
}

// G1-07：queued edit 失败必须可观测，且不得升级为 lifecycle fault。
TEST(Hrc2A7, PendingEditFailureIsObservable) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    RuntimeResult result = {};
    EngineHostHandle host = EngineHost_Create(&result);
    ASSERT_NE(host, nullptr);
    ASSERT_EQ(EngineHost_OpenProject(host,
                                     "assets/gp01/manifest.json",
                                     "assets/gp01/Main.scene",
                                     &result), 0);

    // 越界 entity：AssignSprite 失败。
    const int32_t badEdit = EngineHost_Dispatch(host,
                                                ENGINE_HOST_OP_ASSIGN_SPRITE,
                                                0,
                                                9999,
                                                nullptr,
                                                nullptr,
                                                nullptr,
                                                nullptr,
                                                &result);
    ASSERT_GT(badEdit, 0);
    // 错误 asset index：非 Texture。
    const int32_t badTypeEdit = EngineHost_Dispatch(host,
                                                    ENGINE_HOST_OP_ASSIGN_SPRITE,
                                                    32,
                                                    0,
                                                    nullptr,
                                                    nullptr,
                                                    nullptr,
                                                    nullptr,
                                                    &result);
    ASSERT_GT(badTypeEdit, 0);

    ASSERT_EQ(EngineHost_PumpOneFrame(host, 0.016f, &result), 0);
    EXPECT_EQ(EngineHost_PollPendingEdit(host, badEdit, &result), 0);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SESSION_FAILED);
    EXPECT_EQ(result.stage, ENGINE_HOST_STAGE_EDIT);
    EXPECT_GT(result.diagnostic_length, 0u);
    EXPECT_EQ(EngineHost_PollPendingEdit(host, badTypeEdit, &result), 0);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SESSION_FAILED);

    a7::PendingEditProof proof;
    ASSERT_EQ(a7::ReadPendingEditProof(host, &proof), 0);
    EXPECT_EQ(proof.accepted, 2);
    EXPECT_EQ(proof.executed, 0);
    EXPECT_EQ(proof.failed, 2);
    EXPECT_EQ(proof.lastExecutedWithContextCurrent, 1);
    EXPECT_FALSE(a7::ReadDiagnostic(host).empty());

    // 失败不是 lifecycle fault：host 仍然存活且可继续 pump。
    int32_t emergencyRequested = 0;
    int32_t emergencyCompleted = 0;
    int32_t graphicsTeardownAttempted = 0;
    int32_t graphicsTeardownSucceeded = 0;
    int32_t lifecycleTimedOut = 0;
    ASSERT_EQ(EngineHost_GetTeardownState(host,
                                          &emergencyRequested,
                                          &emergencyCompleted,
                                          &graphicsTeardownAttempted,
                                          &graphicsTeardownSucceeded,
                                          &lifecycleTimedOut), 0);
    EXPECT_EQ(emergencyRequested, 0);
    EXPECT_EQ(lifecycleTimedOut, 0);
    ASSERT_EQ(EngineHost_PumpOneFrame(host, 0.016f, &result), 0);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SKIPPED_NOT_PLAYING);

    ASSERT_EQ(EngineHost_Shutdown(host, &result), 0);
    EngineHost_Destroy(host);
}

// G1-03 负向：未排空的 edit 在 CloseProject 后必须得到确定结论，不被静默丢弃。
TEST(Hrc2A7, UndrainedEditIsAbandonedNotDropped) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    RuntimeResult result = {};
    EngineHostHandle host = EngineHost_Create(&result);
    ASSERT_NE(host, nullptr);
    ASSERT_EQ(EngineHost_OpenProject(host,
                                     "assets/gp01/manifest.json",
                                     "assets/gp01/Main.scene",
                                     &result), 0);

    const int32_t editId = EngineHost_Dispatch(host,
                                               ENGINE_HOST_OP_ASSIGN_SPRITE,
                                               0,
                                               0,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               &result);
    ASSERT_GT(editId, 0);
    EXPECT_EQ(EngineHost_PollPendingEdit(host, editId, &result), 1);

    ASSERT_EQ(EngineHost_CloseProject(host, &result), 0);
    EXPECT_EQ(EngineHost_PollPendingEdit(host, editId, &result), 0);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SESSION_FAILED);
    EXPECT_GT(result.diagnostic_length, 0u);

    a7::PendingEditProof proof;
    ASSERT_EQ(a7::ReadPendingEditProof(host, &proof), 0);
    EXPECT_EQ(proof.accepted, 1);
    EXPECT_EQ(proof.executed, 0);
    EXPECT_EQ(proof.failed, 1);

    ASSERT_EQ(EngineHost_Shutdown(host, &result), 0);
    EngineHost_Destroy(host);
}

// G1-06 + G1-08：唯一 pump owner；A6 lifecycle 在 op-dispatch 存在时仍然成立。
TEST(Hrc2A7, SinglePumpOwnerAndLifecycleRegression) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    int32_t baselineUsers = 0;
    int32_t baselineInitCount = 0;
    int32_t baselineTerminateCount = 0;
    ASSERT_EQ(EngineHost_GetGlfwLifetimeState(&baselineUsers,
                                              &baselineInitCount,
                                              &baselineTerminateCount), 0);

    RuntimeResult result = {};
    EngineHostHandle host = EngineHost_Create(&result);
    ASSERT_NE(host, nullptr);
    const uint64_t ownerThreadId = result.owner_thread_id;

    ASSERT_EQ(EngineHost_OpenProject(host,
                                     "assets/gp01/manifest.json",
                                     "assets/gp01/Main.scene",
                                     &result), 0);
    // legacy EditorSession 与 persistent mode 互斥：不得出现第二个 owner。
    EXPECT_EQ(EditorSession_Create(), nullptr);

    // 交错 dispatch 与 pump：两者必须在同一 owner thread 上。
    for (int32_t i = 0; i < 3; ++i) {
        ASSERT_GE(a7::Dispatch(host, ENGINE_HOST_OP_GET_ENTITY_COUNT, 0, 0,
                               nullptr, nullptr, nullptr, &result), 0);
        EXPECT_EQ(result.owner_thread_id, ownerThreadId);
        ASSERT_EQ(EngineHost_PumpOneFrame(host, 0.016f, &result), 0);
        EXPECT_EQ(result.owner_thread_id, ownerThreadId);
    }

    // A6 lifecycle：Play / Reload / Stop / stats。
    ASSERT_EQ(EngineHost_Play(host, -1, &result), 0);
    ASSERT_EQ(EngineHost_Reload(host, &result), 0);
    ASSERT_EQ(EngineHost_PumpOneFrame(host, 0.016f, &result), 0);
    EditorSessionRuntimeStats stats = {};
    ASSERT_EQ(EngineHost_GetSessionProofState(host, &stats), 0);
    EXPECT_EQ(stats.struct_size, sizeof(EditorSessionRuntimeStats));
    EXPECT_EQ(stats.playing, 1);
    EXPECT_EQ(stats.stop_count, 0u);
    // Play 一次 OnCreate，Reload 再一次。
    EXPECT_EQ(stats.on_create_count, 2u);
    EXPECT_EQ(stats.on_destroy_count, 0u);
    EXPECT_EQ(stats.session_present, 1);
    EXPECT_EQ(stats.callback_attached, 1);

    ASSERT_EQ(EngineHost_Stop(host, &result), 0);
    ASSERT_EQ(EngineHost_GetSessionProofState(host, &stats), 0);
    EXPECT_EQ(stats.stop_count, 1u);
    EXPECT_EQ(stats.playing, 0);

    // 再次 CloseProject 不重复 OnDestroy。
    ASSERT_EQ(EngineHost_CloseProject(host, &result), 0);
    ASSERT_EQ(EngineHost_GetSessionProofState(host, &stats), 0);
    EXPECT_EQ(stats.callback_attached, 0);
    // on_destroy_count 取决于脚本是否定义 OnDestroy（m_HasOnDestroy），
    // 其 exactly-once 证据属于 A6 / test_bridge 范围，A7-1 只断言“不重复增长”。
    const uint32_t onDestroyAfterClose = stats.on_destroy_count;
    ASSERT_EQ(EngineHost_Shutdown(host, &result), 0);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SHUTDOWN_COMPLETE);
    // Teardown 后 proof 归零。
    ASSERT_EQ(EngineHost_GetSessionProofState(host, &stats), 0);
    EXPECT_EQ(stats.session_present, 0);
    EXPECT_EQ(stats.callback_attached, 0);
    EXPECT_EQ(stats.allocator_present, 0);
    EXPECT_LE(stats.on_destroy_count, onDestroyAfterClose);

    int32_t users = 0;
    int32_t initCount = 0;
    int32_t terminateCount = 0;
    ASSERT_EQ(EngineHost_GetGlfwLifetimeState(&users, &initCount, &terminateCount), 0);
    EXPECT_EQ(users, baselineUsers);
    EXPECT_EQ(initCount, baselineInitCount + 1);
    EXPECT_EQ(terminateCount, baselineTerminateCount + 1);
    EngineHost_Destroy(host);
}
