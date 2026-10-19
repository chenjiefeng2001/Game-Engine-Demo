#pragma once

#include "editor_bridge/capi.h"
#include "editor_bridge/PresentationTargetPolicy.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Engine {
class Application;
class OpenGLContext;
class OpenGLFramebuffer;
class OpenGLGraphicsFactory;
}

namespace editor_bridge {

/// HRC-3 P1：production render 的**测试取证 seam** 导出标记。
///
/// 这导出的是 C++ 成员函数（mangled symbol），**不是** C ABI 入口：
/// 不新增 `EngineHost_*` 导出、不改任何结构体布局、不改既有 op 语义，
/// 因此**不构成 ABI increment**（版本递增留给 P2 的 additive Fetch op）。
/// 存在的唯一理由：P1 要求"像素确定性地不同于 clear-only 帧"必须可取证，
/// 而 P1 阶段刻意不新增 C 导出（见 docs/Editor-Avalonia-HRC3-P1P2-Contract.md §4）。
#if defined(_WIN32) && defined(EDITOR_BRIDGE_EXPORTS)
#define ENGINE_HOST_P1_TEST_SEAM __declspec(dllexport)
#else
#define ENGINE_HOST_P1_TEST_SEAM
#endif

class EditorSession;

class EngineHost {
public:
    EngineHost();
    ~EngineHost() noexcept;

    int32_t Initialize(RuntimeResult* result);
    int32_t OpenProject(const char* manifestPath,
                        const char* scenePath,
                        RuntimeResult* result);
    int32_t Play(int32_t assetIndex, RuntimeResult* result);
    int32_t Reload(RuntimeResult* result);
    int32_t Stop(RuntimeResult* result);
    int32_t PumpOneFrame(float requestedDtSeconds, RuntimeResult* result);
    int32_t CloseProject(RuntimeResult* result);
    int32_t Shutdown(RuntimeResult* result);
    int32_t GetLastFrameResult(RuntimeResult* result);
    int32_t SetEventCallback(EngineHostEventCallback callback, void* userData);
    int32_t GetLastDiagnostic(char* out, int32_t cap);
    int32_t GetGraphicsState(int32_t* ready, int32_t* current);
    int32_t GetGraphicsCounters(uint64_t* makeCurrent,
                                uint64_t* uncurrent);
    int32_t GetGlobalProofState(int32_t* applicationPresent,
                                int32_t* timeInitialized,
                                int32_t* sceneInitialized,
                                int32_t* jobSystemPresent,
                                int32_t* resourceManagerPresent,
                                int32_t* fileWatcherPresent);
    int32_t GetSessionProofState(EditorSessionRuntimeStats* stats);
    int32_t GetRuntimeProofState(int32_t* applicationReady,
                                   int32_t* fboReady,
                                   int32_t* sessionAttached,
                                   int32_t* readbackVerified,
                                   uint32_t* fboId,
                                   uint32_t* fboWidth,
                                   uint32_t* fboHeight,
                                   uint64_t* frameCount,
                                   uint64_t* tickCount);
    int32_t GetTeardownState(int32_t* emergencyRequested,
                             int32_t* emergencyCompleted,
                             int32_t* graphicsTeardownAttempted,
                             int32_t* graphicsTeardownSucceeded,
                             int32_t* lifecycleTimedOut);
    int32_t Dispatch(int32_t op,
                     int32_t arg0,
                     int32_t arg1,
                     const char* strArg,
                     const char* strArg2,
                     const char* strArg3,
                     const float* floatArg,
                     RuntimeResult* result);
    int32_t Fetch(int32_t op,
                  int32_t arg0,
                  const char* strArg,
                  const char* strArg2,
                  const char* strArg3,
                  uint8_t* outBuffer,
                  int32_t capacity,
                  RuntimeResult* result);
    int32_t PollPendingEdit(int64_t editId, RuntimeResult* result);
    int32_t GetPendingEditProofState(int32_t* accepted,
                                     int32_t* executed,
                                     int32_t* failed,
                                     int32_t* lastExecutedWithContextCurrent,
                                     uint64_t* lastExecutedFrameId) const;
    bool IsOwnerThread() const;
    bool IsCreationThread() const;
    int32_t RecordFailure(RuntimeResult* result,
                          int32_t status,
                          int32_t stage,
                          const char* diagnostic);

    // ── HRC-3 P1：production presentation（bridge 内部，**不是** C ABI 导出）──
    //
    // P1 阶段刻意不新增 C 导出（那是 P2 的 additive Fetch op）。为了让 P1 的
    // "真的画出了场景内容"可被 native contract test 取证，这里提供 C++ 内部读回：
    // 白色盒、仅测试使用，managed 侧通道留给 P2。
    ENGINE_HOST_P1_TEST_SEAM int32_t PresentationTargetWidth() const;
    ENGINE_HOST_P1_TEST_SEAM int32_t PresentationTargetHeight() const;
    /// 取宿主内部的 runtime session。
    ///
    /// 宿主用 EditorSession(factory, app) 构造自己的 session，不经过
    /// EditorSession_Create 这个 C ABI，所以外部拿不到。没有这个口子就无法验证
    /// "宿主上报的视口尺寸驱动了 presentation target"——而那是 P1.5 的全部内容。
    /// 沿用既有的 P1 test seam 约定，不做成 C 导出（那会要求 ABI 版本递增）。
    ENGINE_HOST_P1_TEST_SEAM EditorSession* SessionForTesting() const;
    /// 上一次 production render 绘制的 renderable 数量（0 = 没有真实内容 / 未渲染）。
    uint32_t LastProductionRenderableCount() const { return m_LastProductionRenderableCount.load(); }
    /// 读回 presentation target 的像素（RGBA8，行优先，左下角起 —— GL 布局）。
    /// GL 访问必须发生在 runtime 线程且 context current，因此走既有命令队列。
    /// 返回写入的字节数；失败返回 0。
    ENGINE_HOST_P1_TEST_SEAM int32_t ReadPresentationPixels(std::vector<uint8_t>& out);



private:
    struct Command {
        std::function<int32_t()> operation;
        std::promise<int32_t> completion;
        RuntimeResult* result = nullptr;
        int32_t stage = ENGINE_HOST_STAGE_BOOTSTRAP;
    };

    static constexpr int32_t kPendingEditPending = 0;
    static constexpr int32_t kPendingEditExecuted = 1;
    static constexpr int32_t kPendingEditFailed = 2;

    /// A7 pending-edit：只在 PumpOneFrame 的 current-context 窗口内执行。
    struct PendingEdit {
        int64_t id = 0;
        int32_t op = ENGINE_HOST_OP_NONE;
        int32_t arg0 = 0;
        int32_t arg1 = 0;
        int32_t state = kPendingEditPending;
        int32_t value = 0;
        uint64_t frameId = 0;
        int32_t contextCurrent = 0;
        /// HRC-3 D3：pick 坐标（D3-C2/D3-C3 的 canonical top-left 视口坐标）。
        float fx = 0.0f;
        float fy = 0.0f;
        std::string diagnostic;
    };

    /// HRC-3 D3：本帧待服务的 pick 请求（在 pending-edit drain 时登记，
    /// 在同一次 PumpOneFrame 的 production render **之后**完成 readback）。
    struct PickFrameRequest {
        bool active = false;
        float x = 0.0f;
        float y = 0.0f;
        uint64_t requestId = 0;
        uint64_t frameId = 0;
    };

    void RuntimeLoop() noexcept;
    void PerformEmergencyTeardown() noexcept;
    void RequestEmergencyStop() noexcept;
    bool WaitForRuntimeExit(std::chrono::milliseconds timeout);
    bool JoinRuntimeIfExited() noexcept;
    int32_t InitializeGraphics(RuntimeResult* result);
    bool TeardownGraphics() noexcept;
    int32_t InitializeRuntimeResources(RuntimeResult* result);
    bool TeardownRuntimeResources() noexcept;
    void UpdateGlobalProofState() noexcept;
    void PublishSessionProofState() noexcept;
    bool EnsureRuntimeSession();
    int32_t MakeRuntimeContextCurrent();
    int32_t Submit(std::function<int32_t()> operation,
                   RuntimeResult* result,
                   int32_t stage);
    void FillRejectedResult(RuntimeResult* result,
                            int32_t status,
                            int32_t stage,
                            const char* diagnostic) const noexcept;
    void SetDiagnostic(const char* diagnostic) noexcept;
    void ForwardEvent(int32_t type,
                      uint64_t generation,
                      const char* payload);
    void ResetResult(RuntimeResult* result) const noexcept;
    void SetResult(RuntimeResult* result,
                   int32_t status,
                   int32_t stage,
                   int32_t errorCode,
                   const char* diagnostic,
                   int32_t stateAfter);
    int32_t RejectUnimplemented(int32_t stage, RuntimeResult* result);

    // ── A7 op-dispatch seam ────────────────────────────────
    static bool IsKnownOp(int32_t op) noexcept;
    static bool RequiresGraphicsContext(int32_t op) noexcept;
    /// HRC-3 P2：只能经 `EngineHost_Fetch` 使用、不得经 `EngineHost_Dispatch` 的 op。
    static bool IsFetchOnlyOp(int32_t op) noexcept;
    static int32_t MinFetchCapacityForOp(int32_t op) noexcept;

    /// HRC-3 P2：取整个 presentation 帧（Fetch-only）。必须在 runtime 线程执行
    /// （由 Fetch 的 Submit 保证），GL 读回在该线程内 make-current 后完成。
    /// frame_id 与像素在**同一次 Submit** 内取得，因此原子一致。
    /// HRC-3 D3：Fetch-only 的 typed pick 结果读取（绝不可经 Dispatch 到达）。
    int32_t FetchPickResult(uint8_t* outBuffer, int32_t capacity,
                            RuntimeResult* result);
    int32_t FetchPresentationFrame(uint8_t* outBuffer, int32_t capacity,
                                   RuntimeResult* result);
    EditorSession* RequireSessionForEdit(RuntimeResult* result) noexcept;
    int32_t ExecuteSessionOp(int32_t op,
                             int32_t arg0,
                             int32_t arg1,
                             const char* strArg,
                             const char* strArg2,
                             const char* strArg3,
                             const float* floatArg,
                             RuntimeResult* result);
    int32_t ExecuteFetchOp(int32_t op,
                           int32_t arg0,
                           const char* strArg,
                           const char* strArg2,
                           const char* strArg3,
                           uint8_t* outBuffer,
                           int32_t capacity,
                           RuntimeResult* result);
    int32_t EnqueuePendingEdit(int32_t op,
    int32_t arg0,
    int32_t arg1,
    RuntimeResult* result);
    int32_t EnqueuePendingEdit(int32_t op,
                               int32_t arg0,
                               int32_t arg1,
                               float fx,
                               float fy,
                               RuntimeResult* result);
    /// HRC-3 D3：提交 pick 请求。capacity = 1：已有 outstanding（未取走）时返回
    /// `PICK_BUSY`，且**不消费、不替换** outstanding request。
    int32_t EnqueuePickRequest(float x, float y, RuntimeResult* result);
    /// HRC-3 D3：在 production render 之后对本帧请求做 readback，产出 typed 结果。
    void ServicePickRequestForFrame() noexcept;
    /// HRC-3 D3：作废 pending / 已产出但未取走的 pick 结果。
    /// 任何 Stop / Close / terminal teardown 都必须调用；之后 Fetch 只返回
    /// `INVALID_STATE` + `STAGE_SHUTDOWN` 且**不产生 packet**。
    void InvalidatePickResult(const char* diagnostic) noexcept;
    /// HRC-3 D3：是否存在"已受理但尚未被取走"的 pick 结果（capacity=1 判定）。
    bool HasOutstandingPickResult() const noexcept;

    int32_t ExecutePendingEdit(PendingEdit& edit);
    void DrainPendingEdits() noexcept;
    void AbandonPendingEdits(const char* diagnostic) noexcept;
    void RecordEditFailure(const std::string& diagnostic);

    std::atomic<int32_t> m_State;
    std::atomic<uint64_t> m_Generation;
    uint64_t m_OperationId;
    std::atomic<uint64_t> m_FrameId;
    /// HRC-3 P2：最近一次**成功**交出 presentation frame 的 frame_id。
    /// 用于"没有新帧"的判定；0 表示尚未交出过一帧。
    std::atomic<uint64_t> m_LastFetchedFrameId{0};
    mutable std::string m_LastDiagnostic;
    RuntimeResult m_LastFrameResult;
    RuntimeResult m_BootstrapResult{};
    EngineHostEventCallback m_EventCallback;
    void* m_EventUserData;
    std::thread::id m_CreationThreadId;
    std::unique_ptr<Engine::OpenGLGraphicsFactory> m_GraphicsFactory;
    std::unique_ptr<Engine::Application> m_Application;
    std::unique_ptr<Engine::OpenGLContext> m_RenderContext;
    std::unique_ptr<Engine::OpenGLFramebuffer> m_OffscreenFbo;
    /// HRC-3 P1：production presentation target（与上面的 64×64 探针 FBO 相互独立）。
    std::unique_ptr<Engine::OpenGLFramebuffer> m_PresentationFbo;
    /// 呈现目标的尺寸策略。bootstrap 时以编译期常量初始化，之后由宿主上报的
    /// 视口尺寸驱动。放在这里而不是每次重算，是因为"要不要重建"的判断必须跨帧
    /// 保持状态（去重、零尺寸期间保留上一有效值），否则每次 PumpOneFrame 都会
    /// 重建一次 FBO。
    editor_bridge::PresentationTargetPolicy m_PresentationSizePolicy;
    /// 在 current-context 窗口内把策略的决策落到 FBO 上。返回 true 表示本帧
    /// 发生了重建（调用方据此知道后续的 viewport/scissor 需要重新设置）。
    bool ApplyPresentationTargetSize();
    std::atomic<uint32_t> m_LastProductionRenderableCount{0};
    std::unique_ptr<EditorSession> m_Session;

    mutable std::mutex m_Mutex;
    mutable std::recursive_mutex m_CallbackMutex;
    mutable std::mutex m_SessionProofMutex;
    std::mutex m_LifecycleMutex;
    std::mutex m_JoinMutex;
    std::condition_variable m_CommandCondition;
    std::condition_variable m_LifecycleCondition;
    std::deque<std::shared_ptr<Command>> m_Commands;
    std::deque<PendingEdit> m_PendingEdits;
    std::map<int64_t, PendingEdit> m_CompletedEdits;
    int64_t m_NextEditId{1};

    // ── HRC-3 D3：pick 状态（capacity = 1）──
    //
    // m_PickResultPresent / m_PickResultFetched 共同表达"outstanding"：
    // 产出后未被 Fetch 取走即为 outstanding，此时新请求必须 PICK_BUSY。
    // m_PickResultInvalidated 表达"曾有 pending/request，但被 teardown 作废"，
    // 此时 Fetch 返回 INVALID_STATE + STAGE_SHUTDOWN 且**不产生 packet**。
    PickFrameRequest m_PickFrameRequest;
    PickResultPacket m_PickResult{};
    bool m_PickResultPresent = false;
    bool m_PickResultFetched = false;
    bool m_PickResultInvalidated = false;

    // ── D3-H8-1：outstanding request（capacity = 1 的真正判据）──
    //
    // 此前只跟踪 `m_PickFrameRequest.active`（已 dispatch）与
    // `m_PickResultPresent`（已产出），**漏掉「已受理、仍在 pending 队列、
    // 尚未 dispatch」这一态**，导致 A 还在队列里时 B 被错误受理。
    // 现在以 request id 作为唯一 outstanding 标志，覆盖三态：
    //   Idle                          → 0
    //   Queued(A) / Dispatched(A) / Produced-not-yet-fetched(A) → A 的 id
    // Fetch 消费或 teardown 作废时清零。
    uint64_t m_PickOutstandingRequestId = 0;

    // D3-H8-3：outstanding request 绑定受理时的 runtime generation；
    // 旧 generation 的 request 永远不能在新一代里重新出现。
    uint64_t m_PickOutstandingGeneration = 0;
    std::atomic<int32_t> m_PendingEditAccepted{0};
    std::atomic<int32_t> m_PendingEditExecuted{0};
    std::atomic<int32_t> m_PendingEditFailed{0};
    std::atomic<int32_t> m_PendingEditLastContextCurrent{0};
    std::atomic<uint64_t> m_PendingEditLastFrameId{0};
    std::jthread m_RuntimeThread;
    std::thread::id m_OwnerThreadId;
    std::atomic<bool> m_AcceptingCommands;
    std::atomic<bool> m_StopRequested;
    std::atomic<bool> m_EmergencyTeardownRequested;
    std::atomic<bool> m_EmergencyTeardownCompleted;
    std::atomic<bool> m_GraphicsTeardownAttempted;
    std::atomic<bool> m_GraphicsTeardownSucceeded;
    std::atomic<bool> m_LifecycleTimedOut;
    std::atomic<bool> m_ShutdownComplete;
    std::atomic<uint64_t> m_LastMakeCurrentCount{0};
    std::atomic<uint64_t> m_LastUncurrentCount{0};
    std::atomic<uint64_t> m_TickCount{0};
    std::atomic<uint32_t> m_FboId{0};
    std::atomic<uint32_t> m_FboWidth{0};
    std::atomic<uint32_t> m_FboHeight{0};
    std::atomic<bool> m_ApplicationReady{false};
    std::atomic<bool> m_FboReady{false};
    std::atomic<bool> m_SessionAttached{false};
    std::atomic<bool> m_ReadbackVerified{false};
    std::atomic<uint32_t> m_SessionStopCount{0};
    std::atomic<uint32_t> m_SessionOnCreateCount{0};
    std::atomic<uint32_t> m_SessionOnDestroyCount{0};
    std::atomic<uint32_t> m_SessionGameplayHandleCount{0};
    std::atomic<int32_t> m_SessionPlaying{0};
    std::atomic<int32_t> m_SessionRuntimeScenePresent{0};
    std::atomic<int32_t> m_SessionScriptValid{0};
    std::atomic<int32_t> m_SessionCallbackAttached{0};
    std::atomic<int32_t> m_SessionGameplayScenePresent{0};
    std::atomic<int32_t> m_SessionHudDirty{0};
    std::atomic<int32_t> m_SessionAllocatorPresent{0};
    std::atomic<int32_t> m_SessionProofPresent{0};
    std::atomic<bool> m_GlobalApplicationPresent{false};
    std::atomic<bool> m_GlobalTimeInitialized{false};
    std::atomic<bool> m_GlobalSceneInitialized{false};
    std::atomic<bool> m_GlobalJobSystemPresent{false};
    std::atomic<bool> m_GlobalResourceManagerPresent{false};
    std::atomic<bool> m_GlobalFileWatcherPresent{false};
    bool m_ThreadStarted;
    bool m_ThreadExited;
};

}
