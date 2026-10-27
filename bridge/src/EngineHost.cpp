#include "EngineHost.h"
#include "Engine/Application.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include "Engine/OpenGL/OpenGLFramebuffer.h"
#include "Engine/OpenGL/OpenGLGraphicsFactory.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Core/Resources/FileWatcher.h"
#include "Engine/Core/Resources/ResourceManager.h"
#include "Engine/Core/Scene/SceneManager.h"
#include "Engine/Core/RHI/RenderQueue.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RHI/RenderCommand.h"
#include "Engine/Core/Renderer/SpriteBatch.h"
#include "Engine/Platform/PlatformUtils.h"
#include "Engine/Scripting/GameplayAPI.h"
#include "Engine/Scripting/ScriptAPI.h"
#include "EditorSession.h"


#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace editor_bridge {
namespace {

// A7-1 起冻结的 EngineHost ABI 版本，单一来源见 capi.h。
// HRC-2-A Readiness 之前不得再新增导出；新增必须同时给出 gate 归属与 negative evidence。
constexpr uint32_t kEngineHostAbiVersion = ENGINE_HOST_ABI_VERSION;

// v1.4 / P-1：pick packet 的布局是**冻结**的（只是把 reserved 正名为 entity_index）。
// 把它写成编译期断言：任何字段重排/增删都会在构建期失败，而不是让 managed
// 解码器在运行期读到错位数据。
static_assert(sizeof(PickResultPacket) == 32,
              "PickResultPacket must stay 32 bytes");
static_assert(offsetof(PickResultPacket, struct_size) == 0, "layout");
static_assert(offsetof(PickResultPacket, status) == 4, "layout");
static_assert(offsetof(PickResultPacket, frame_id) == 8, "layout");
static_assert(offsetof(PickResultPacket, request_id) == 16, "layout");
static_assert(offsetof(PickResultPacket, entity_id) == 24, "layout");
static_assert(offsetof(PickResultPacket, entity_index) == 28, "layout");
constexpr float kMaxEffectiveDtSeconds = 0.1f;
constexpr auto kRuntimeStartupTimeout = std::chrono::seconds(5);
constexpr auto kRuntimeShutdownTimeout = std::chrono::seconds(5);

// Slice E：viewport 重建失败后的**有界重试**上限。
// 失败时请求被重新登记并每帧重试一次；超过上限后停止重试（保持 fail-closed：
// 不发布帧、不 pick），等待新的 SET_VIEWPORT_SIZE 请求或 project 重开。
constexpr int32_t kMaxViewportResizeRetries = 8;

bool TestFailure(const char* name) {
    const char* value = std::getenv(name);
    return value && std::strcmp(value, "1") == 0;
}

int32_t StatusForStage(int32_t stage) noexcept {
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

}

EngineHost::EngineHost()
    : m_State(ENGINE_HOST_STATE_CREATED),
      m_Generation(0),
      m_OperationId(0),
      m_FrameId(0),
      m_LastDiagnostic(),
      m_LastFrameResult{},
      m_EventCallback(nullptr),
      m_EventUserData(nullptr),
      m_CreationThreadId(std::this_thread::get_id()),
      m_AcceptingCommands(false),
      m_StopRequested(false),
      m_EmergencyTeardownRequested(false),
      m_EmergencyTeardownCompleted(false),
      m_GraphicsTeardownAttempted(false),
      m_GraphicsTeardownSucceeded(false),
      m_LifecycleTimedOut(false),
      m_ShutdownComplete(false),
      m_ThreadStarted(false),
      m_ThreadExited(false) {
    ResetResult(&m_LastFrameResult);
    m_LastFrameResult.status = ENGINE_HOST_STATUS_SUCCESS;
    m_LastFrameResult.stage = ENGINE_HOST_STAGE_BOOTSTRAP;
}

void EngineHost::RequestEmergencyStop() noexcept {
    m_AcceptingCommands = false;
    m_State = ENGINE_HOST_STATE_FAULTED;
    m_EmergencyTeardownRequested = true;
    m_StopRequested = true;
    m_CommandCondition.notify_all();
}

bool EngineHost::WaitForRuntimeExit(std::chrono::milliseconds timeout) {
    if (!m_RuntimeThread.joinable()) return true;
    std::unique_lock<std::mutex> lock(m_Mutex);
    return m_LifecycleCondition.wait_for(lock, timeout, [this]() {
        return m_ThreadExited;
    });
}

bool EngineHost::JoinRuntimeIfExited() noexcept {
    if (!m_RuntimeThread.joinable()) return true;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_ThreadExited) return false;
    }
    std::lock_guard<std::mutex> joinLock(m_JoinMutex);
    if (m_RuntimeThread.joinable() &&
        m_RuntimeThread.get_id() != std::this_thread::get_id()) {
        m_RuntimeThread.join();
    }
    return true;
}

EngineHost::~EngineHost() noexcept {
    try {
        Shutdown(nullptr);
    } catch (...) {
    }

    if (m_RuntimeThread.joinable()) {
        RequestEmergencyStop();
        if (!WaitForRuntimeExit(kRuntimeShutdownTimeout) ||
            !JoinRuntimeIfExited()) {
            std::fprintf(stderr, "EngineHost runtime did not exit before destroy deadline\n");
            std::terminate();
        }
    }
}

int32_t EngineHost::RecordFailure(RuntimeResult* result,
                                   int32_t status,
                                   int32_t stage,
                                   const char* diagnostic) {
    FillRejectedResult(result, status, stage, diagnostic);
    return -1;
}

void EngineHost::SetDiagnostic(const char* diagnostic) noexcept {
    std::lock_guard<std::mutex> lock(m_Mutex);
    try {
        if (diagnostic) {
            m_LastDiagnostic = diagnostic;
        } else {
            m_LastDiagnostic.clear();
        }
    } catch (...) {
    }
}

void EngineHost::ResetResult(RuntimeResult* result) const noexcept {
    if (!result) return;
    std::thread::id ownerThreadId;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        ownerThreadId = m_OwnerThreadId;
    }
    std::memset(result, 0, sizeof(*result));
    result->struct_size = static_cast<uint32_t>(sizeof(RuntimeResult));
    result->abi_version = kEngineHostAbiVersion;
    result->state_before = m_State.load();
    result->state_after = m_State.load();
    result->generation = m_Generation.load();
    result->owner_thread_id = std::hash<std::thread::id>{}(ownerThreadId);
}

void EngineHost::FillRejectedResult(RuntimeResult* result,
                                     int32_t status,
                                     int32_t stage,
                                     const char* diagnostic) const noexcept {
    if (!result) return;
    ResetResult(result);
    result->status = status;
    result->stage = stage;
    result->error_code = 0;
    result->diagnostic_length = diagnostic
        ? static_cast<uint32_t>(std::strlen(diagnostic))
        : 0u;
    const_cast<EngineHost*>(this)->SetDiagnostic(diagnostic);
}

void EngineHost::SetResult(RuntimeResult* result,
                            int32_t status,
                            int32_t stage,
                            int32_t errorCode,
                            const char* diagnostic,
                            int32_t stateAfter) {
    if (!result) return;
    const int32_t stateBefore = m_State.load();
    ResetResult(result);
    result->status = status;
    result->stage = stage;
    result->operation_id = ++m_OperationId;
    result->frame_id = m_FrameId;
    result->state_before = stateBefore;
    result->state_after = stateAfter;
    result->error_code = errorCode;
    if (diagnostic) {
        const size_t length = std::strlen(diagnostic);
        result->diagnostic_length = length > std::numeric_limits<uint32_t>::max()
                                        ? std::numeric_limits<uint32_t>::max()
                                        : static_cast<uint32_t>(length);
        result->diagnostic_truncated = length > std::numeric_limits<uint32_t>::max()
                                           ? 1u
                                           : 0u;
    }
    SetDiagnostic(diagnostic);
}

bool EngineHost::IsOwnerThread() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_ThreadStarted && !m_ThreadExited &&
           m_OwnerThreadId == std::this_thread::get_id();
}

bool EngineHost::IsCreationThread() const {
    return m_CreationThreadId == std::this_thread::get_id();
}

int32_t EngineHost::Submit(std::function<int32_t()> operation,
                           RuntimeResult* result,
                           int32_t stage) {
    if (!operation) {
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_INVALID_STATE,
                           stage,
                           "operation is empty");
        return -1;
    }

    if (IsOwnerThread()) {
        try {
            const int32_t code = operation();
            PublishSessionProofState();
            return code;
        } catch (...) {
            PublishSessionProofState();
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_INIT_FAILED,
                               stage,
                               "owner-thread operation failed");
            return -1;
        }
    }

    auto command = std::make_shared<Command>();
    command->operation = std::move(operation);
    command->result = result;
    command->stage = stage;
    auto completion = command->completion.get_future();

    bool accepted = false;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_AcceptingCommands.load() && !m_StopRequested.load()) {
            m_Commands.push_back(std::move(command));
            accepted = true;
        }
    }
    if (!accepted) {
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_INVALID_STATE,
                           stage,
                           "host is not accepting commands");
        return -1;
    }
    m_CommandCondition.notify_one();

    try {
        return completion.get();
    } catch (...) {
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_INIT_FAILED,
                           stage,
                           "runtime command completion failed");
        return -1;
    }
}

void EngineHost::PerformEmergencyTeardown() noexcept {
    bool success = true;
    const bool contextCurrent = m_GraphicsFactory &&
                                 m_GraphicsFactory->MakePersistentContextCurrent();
    if (!contextCurrent) success = false;
    if (!TeardownRuntimeResources()) success = false;
    if (contextCurrent && m_GraphicsFactory &&
        !m_GraphicsFactory->UncurrentPersistentContext()) {
        success = false;
    }
    m_EmergencyTeardownCompleted = success;
}

int32_t EngineHost::InitializeGraphics(RuntimeResult* result) {
    try {
        if (!m_GraphicsFactory) {
            m_GraphicsFactory = std::make_unique<Engine::OpenGLGraphicsFactory>(true);
        }
        if (!m_GraphicsFactory->InitializePersistentHiddenContext(64, 64)) {
            const std::string error = m_GraphicsFactory->GetLastError();
            const int32_t stage = error.find("GLAD") != std::string::npos
                ? ENGINE_HOST_STAGE_GLAD
                : (error.find("GLFW") != std::string::npos
                       ? ENGINE_HOST_STAGE_PLATFORM
                       : ENGINE_HOST_STAGE_GL_CONTEXT);
            TeardownGraphics();
            m_State = ENGINE_HOST_STATE_FAULTED;
            FillRejectedResult(result,
                               StatusForStage(stage),
                               stage,
                               error.empty() ? "persistent GL context initialization failed"
                                             : error.c_str());
            return -1;
        }
        return 0;
    } catch (const std::exception& exception) {
        TeardownGraphics();
        m_State = ENGINE_HOST_STATE_FAULTED;
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_GL_FAILED,
                           ENGINE_HOST_STAGE_GL_CONTEXT,
                           exception.what());
        return -1;
    } catch (...) {
        TeardownGraphics();
        m_State = ENGINE_HOST_STATE_FAULTED;
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_GL_FAILED,
                           ENGINE_HOST_STAGE_GL_CONTEXT,
                           "unknown persistent GL context exception");
        return -1;
    }
}

bool EngineHost::TeardownGraphics() noexcept {
    m_GraphicsTeardownAttempted = true;
    if (!m_GraphicsFactory) {
        m_GraphicsTeardownSucceeded = true;
        return true;
    }
    bool success = false;
    try {
        success = m_GraphicsFactory->ShutdownPersistentContext();
        m_LastMakeCurrentCount = m_GraphicsFactory->GetMakeCurrentCount();
        m_LastUncurrentCount = m_GraphicsFactory->GetUncurrentCount();
    } catch (...) {
        success = false;
    }
    m_GraphicsTeardownSucceeded = success;
    if (success) m_GraphicsFactory.reset();
    return success;
}

void EngineHost::RuntimeLoop() noexcept {
    try {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_OwnerThreadId = std::this_thread::get_id();
            m_ThreadStarted = true;
        }
        m_LifecycleCondition.notify_all();

        for (;;) {
            std::shared_ptr<Command> command;
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_CommandCondition.wait(lock, [this]() {
                    return m_StopRequested.load() ||
                           m_EmergencyTeardownRequested.load() ||
                           !m_Commands.empty();
                });
                if (m_EmergencyTeardownRequested.load()) break;
                if (m_Commands.empty()) {
                    if (m_StopRequested.load()) break;
                    continue;
                }
                command = std::move(m_Commands.front());
                m_Commands.pop_front();
            }

            int32_t code = -1;
            try {
                code = command->operation();
             } catch (...) {
                 m_State = ENGINE_HOST_STATE_FAULTED;
                 m_AcceptingCommands = false;
                 m_StopRequested = true;
                 m_EmergencyTeardownRequested = true;
                 FillRejectedResult(command->result,
                                    StatusForStage(command->stage),
                                    command->stage,
                                    "runtime command threw an exception");
                 code = -1;
              }
             PublishSessionProofState();
             try {
                command->completion.set_value(code);
            } catch (...) {
            }

             if (m_StopRequested.load() ||
                 m_EmergencyTeardownRequested.load()) {
                 std::deque<std::shared_ptr<Command>> pending;
                 {
                     std::lock_guard<std::mutex> lock(m_Mutex);
                     pending.swap(m_Commands);
                 }
                 for (auto& pendingCommand : pending) {
                     FillRejectedResult(pendingCommand->result,
                                        StatusForStage(pendingCommand->stage),
                                        pendingCommand->stage,
                                        "runtime stopped before command execution");
                     try {
                         pendingCommand->completion.set_value(-1);
                     } catch (...) {
                     }
                 }
                 break;
             }
        }
    } catch (...) {
        m_State = ENGINE_HOST_STATE_FAULTED;
        std::deque<std::shared_ptr<Command>> pending;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_AcceptingCommands = false;
            m_StopRequested = true;
            m_EmergencyTeardownRequested = true;
            pending.swap(m_Commands);
        }
        m_LifecycleCondition.notify_all();
        for (auto& command : pending) {
            FillRejectedResult(command->result,
                               StatusForStage(command->stage),
                               command->stage,
                               "runtime thread stopped unexpectedly");
            try {
                command->completion.set_value(-1);
            } catch (...) {
            }
        }
    }

    if (m_EmergencyTeardownRequested.load()) {
        PerformEmergencyTeardown();
    }

    AbandonPendingEdits("runtime thread stopped before edit executed");

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_AcceptingCommands = false;
        m_ThreadExited = true;
    }
    m_LifecycleCondition.notify_all();
}

int32_t EngineHost::MakeRuntimeContextCurrent() {
    if (!m_GraphicsFactory ||
        !m_GraphicsFactory->MakePersistentContextCurrent()) {
        RequestEmergencyStop();
        return -1;
    }
    return 0;
}

bool EngineHost::EnsureRuntimeSession() {
    if (m_Session) return true;
    if (!m_GraphicsFactory || !m_Application) return false;
    if (TestFailure("EDITOR_BRIDGE_FAIL_SESSION_ATTACH")) return false;
    try {
        m_Session = std::make_unique<EditorSession>(
            *m_GraphicsFactory, m_Application.get());
        m_Session->SetEventCallback([this](int32_t type, const char* payload) {
            ForwardEvent(type, m_Generation.load(), payload);
        });
        m_SessionAttached = m_Session->IsGraphicsReady();
        PublishSessionProofState();
        return m_SessionAttached.load();
    } catch (...) {
        m_Session.reset();
        m_SessionAttached = false;
        PublishSessionProofState();
        return false;
    }
}

int32_t EngineHost::InitializeRuntimeResources(RuntimeResult* result) {
    int32_t failureStage = ENGINE_HOST_STAGE_FBO;
    int32_t failureStatus = ENGINE_HOST_STATUS_GL_FAILED;
    try {
        if (!m_GraphicsFactory ||
            !m_GraphicsFactory->GetPersistentWindow()) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_GL_CONTEXT,
                               "persistent graphics owner is unavailable");
            return -1;
        }
        m_RenderContext = std::make_unique<Engine::OpenGLContext>(
            m_GraphicsFactory->GetPersistentWindow(),
            m_GraphicsFactory->GetGLContext());
        if (TestFailure("EDITOR_BRIDGE_FAIL_FBO")) {
            throw std::runtime_error("injected FBO initialization failure");
        }
        m_OffscreenFbo = std::make_unique<Engine::OpenGLFramebuffer>(
            m_GraphicsFactory->GetGLContext());
        m_OffscreenFbo->Resize(64, 64);
        if (!m_OffscreenFbo->IsValid()) {
            throw std::runtime_error("offscreen FBO is incomplete");
        }
        m_FboId = m_OffscreenFbo->GetFramebufferID();
        m_FboWidth = m_OffscreenFbo->GetWidth();
        m_FboHeight = m_OffscreenFbo->GetHeight();
        m_FboReady = true;

        failureStage = ENGINE_HOST_STAGE_ENGINE_SUBSYSTEM;
        failureStatus = ENGINE_HOST_STATUS_INIT_FAILED;
        if (TestFailure("EDITOR_BRIDGE_FAIL_APPLICATION_INIT")) {
            throw std::runtime_error("injected Application initialization failure");
        }
        m_Application = std::make_unique<Engine::Application>(
            *m_GraphicsFactory, true);
        if (!m_Application->InitializeHeadless()) {
            throw std::runtime_error("headless Application initialization failure");
        }
        m_ApplicationReady = true;

        // ── HRC-3 P1：production presentation target + render 资源 ──
        //
        // 与上面的 64×64 探针 FBO **完全分离**：探针 FBO 保持原样（G2P-01 的既有证据
        // 一个字节都不改），presentation FBO 才是"真的画场景"的宿主目标（D4 已后置，
        // 尺寸固定，取自 capi.h 的单一来源常量）。
        failureStage = ENGINE_HOST_STAGE_FBO;
        failureStatus = ENGINE_HOST_STATUS_GL_FAILED;
        m_PresentationFbo = std::make_unique<Engine::OpenGLFramebuffer>(
            m_GraphicsFactory->GetGLContext());
        m_PresentationFbo->Resize(ENGINE_HOST_PRESENTATION_DEFAULT_WIDTH,
                                   ENGINE_HOST_PRESENTATION_DEFAULT_HEIGHT);
        if (!m_PresentationFbo->IsValid()) {
            throw std::runtime_error("presentation FBO is incomplete");
        }
        // Slice E：默认尺寸的 target 有效；后续 resize 失败才会把它置为无效。
        m_PresentationTargetValid = true;
        // ⚠ P1.1 BLOCKED：production render 的 GL 资源（sprite-batch shader + VAO/VBO/EBO）
        // **暂不在 bootstrap 创建**。原因（实测，非推测）：这些对象的析构会无条件删除 GL name，
        // 而 host 拆除时未必持有 current context / 未必在创建线程上 —— 实测导致后续 host 的
        // GLFW 初始化失败（test_bridge 从 41/41 退化）。sprite 管线的片元级根因未定位之前，
        // 不引入这些跨线程 GL 资源。渲染入口、presentation target、测试 seam 仍保留，
        // 待 P1.1 修好管线后在**runtime 线程上**创建（见 Application 侧注释）。


        failureStage = ENGINE_HOST_STAGE_SESSION;
        failureStatus = ENGINE_HOST_STATUS_SESSION_FAILED;
        if (!EnsureRuntimeSession()) {
            throw std::runtime_error("runtime session attach failed");
        }
        UpdateGlobalProofState();
        PublishSessionProofState();
        m_ReadbackVerified = false;
        return 0;
    } catch (const std::exception& exception) {
        TeardownRuntimeResources();
        m_State = ENGINE_HOST_STATE_FAULTED;
        FillRejectedResult(result,
                           failureStatus,
                           failureStage,
                           exception.what());
        return -1;
    } catch (...) {
        TeardownRuntimeResources();
        m_State = ENGINE_HOST_STATE_FAULTED;
        FillRejectedResult(result,
                           failureStatus,
                           failureStage,
                           "unknown runtime resource initialization failure");
        return -1;
    }
}

bool EngineHost::TeardownRuntimeResources() noexcept {
    bool success = true;
    if (m_Session) {
        try {
            m_Session->SetEventCallback(nullptr);
        } catch (...) {
            success = false;
        }
        try {
            m_Session->Stop();
        } catch (...) {
            success = false;
        }
        PublishSessionProofState();
        try {
            m_Session.reset();
        } catch (...) {
            success = false;
        }
    }
    m_SessionAttached = false;
    PublishSessionProofState();
    if (m_OffscreenFbo) {
    try {
    m_OffscreenFbo.reset();
    } catch (...) {
    success = false;
    }
    }
    // HRC-3 P1：presentation target 与探针 FBO 同序释放（都在 GL 资源销毁之前）。
    if (m_PresentationFbo) {
    try {
    m_PresentationFbo.reset();
    } catch (...) {
    success = false;
    }
    m_LastProductionRenderableCount = 0;
    }

    m_FboReady = false;
    m_FboId = 0;
    m_FboWidth = 0;
    m_FboHeight = 0;
    if (m_Application) {
        try {
            m_Application->Shutdown();
            m_Application.reset();
        } catch (...) {
            success = false;
        }
    }
    m_ApplicationReady = false;
    m_ReadbackVerified = false;
    if (m_RenderContext) {
        try {
            m_RenderContext.reset();
        } catch (...) {
            success = false;
        }
    }
    UpdateGlobalProofState();
    PublishSessionProofState();
    return success;
}

void EngineHost::UpdateGlobalProofState() noexcept {
    m_GlobalApplicationPresent.store(Engine::Application::GetInstance() != nullptr);
    m_GlobalTimeInitialized.store(Engine::Time::IsInitialized());
    m_GlobalSceneInitialized.store(Engine::SceneManager::IsInitialized());
    m_GlobalJobSystemPresent.store(Engine::JobSystem::IsInitialized());
    m_GlobalResourceManagerPresent.store(Engine::ResourceManager::Get() != nullptr);
    m_GlobalFileWatcherPresent.store(Engine::FileWatcher::Get() != nullptr);
}

void EngineHost::PublishSessionProofState() noexcept {
    std::lock_guard<std::mutex> proofLock(m_SessionProofMutex);
    m_SessionProofPresent.store(m_SessionAttached.load() ? 1 : 0);
    if (m_SessionAttached.load() && m_Session) {
        EditorSession::RuntimeStats stats;
        if (m_Session->GetRuntimeStats(&stats)) {
            m_SessionStopCount.store(stats.stopCount);
            m_SessionOnCreateCount.store(stats.onCreateCount);
            m_SessionOnDestroyCount.store(stats.onDestroyCount);
            m_SessionGameplayHandleCount.store(stats.gameplayHandleCount);
            m_SessionPlaying.store(stats.playing ? 1 : 0);
            m_SessionRuntimeScenePresent.store(stats.runtimeScenePresent ? 1 : 0);
            m_SessionScriptValid.store(stats.scriptValid ? 1 : 0);
            m_SessionCallbackAttached.store(stats.callbackAttached ? 1 : 0);
            m_SessionGameplayScenePresent.store(stats.gameplayScenePresent ? 1 : 0);
            m_SessionHudDirty.store(stats.hudDirty ? 1 : 0);
        }
    } else {
        m_SessionGameplayHandleCount.store(0);
        m_SessionPlaying.store(0);
        m_SessionRuntimeScenePresent.store(0);
        m_SessionScriptValid.store(0);
        m_SessionCallbackAttached.store(0);
    }
    m_SessionGameplayScenePresent.store(
        Engine::Scripting::GameplayAPI::HasScene() ? 1 : 0);
    m_SessionGameplayHandleCount.store(
        Engine::Scripting::GameplayAPI::HandleCount());
    m_SessionHudDirty.store(Engine::Scripting::ScriptAPI::HasHudText() ? 1 : 0);
    m_SessionAllocatorPresent.store(
        Engine::Application::IsSubsystemAllocatorPresent() ? 1 : 0);
}

int32_t EngineHost::Initialize(RuntimeResult* result) {
    std::lock_guard<std::mutex> lifecycleLock(m_LifecycleMutex);
    if (m_State.load() != ENGINE_HOST_STATE_CREATED) {
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_INVALID_STATE,
                           ENGINE_HOST_STAGE_BOOTSTRAP,
                           "host is not in the created state");
        return -1;
    }

    m_State = ENGINE_HOST_STATE_BOOTSTRAPPING;
    m_Generation = 1;
    m_AcceptingCommands = true;
    m_StopRequested = false;
    m_ThreadStarted = false;
    m_ThreadExited = false;
    m_EmergencyTeardownRequested = false;
    m_EmergencyTeardownCompleted = false;
    m_GraphicsTeardownAttempted = false;
    m_GraphicsTeardownSucceeded = false;
    m_LifecycleTimedOut = false;
    m_ShutdownComplete = false;

    if (InitializeGraphics(result) != 0) {
        m_AcceptingCommands = false;
        return -1;
    }
    if (!m_GraphicsFactory->UncurrentPersistentContext()) {
        TeardownGraphics();
        m_State = ENGINE_HOST_STATE_FAULTED;
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_GL_FAILED,
                           ENGINE_HOST_STAGE_GL_CONTEXT,
                           "failed to un-current persistent context before runtime start");
        m_AcceptingCommands = false;
        return -1;
    }

    try {
        m_RuntimeThread = std::jthread(&EngineHost::RuntimeLoop, this);
        bool startupReady = false;
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            startupReady = m_LifecycleCondition.wait_for(
                lock,
                kRuntimeStartupTimeout,
                [this]() {
                    return m_ThreadStarted || m_ThreadExited;
                });
        }
        if (!startupReady) {
            m_LifecycleTimedOut = true;
            RequestEmergencyStop();
            WaitForRuntimeExit(kRuntimeShutdownTimeout);
            throw std::runtime_error("runtime thread startup timed out");
        }
        if (!m_ThreadStarted) {
            throw std::runtime_error("runtime thread exited before startup");
        }
    } catch (const std::exception& exception) {
        m_AcceptingCommands = false;
        m_StopRequested = true;
        m_State = ENGINE_HOST_STATE_FAULTED;
        SetResult(result,
                  ENGINE_HOST_STATUS_INIT_FAILED,
                  ENGINE_HOST_STAGE_BOOTSTRAP,
                  0,
                  exception.what(),
                  ENGINE_HOST_STATE_FAULTED);
        return -1;
    } catch (...) {
        m_AcceptingCommands = false;
        m_StopRequested = true;
        m_State = ENGINE_HOST_STATE_FAULTED;
        SetResult(result,
                  ENGINE_HOST_STATUS_INIT_FAILED,
                  ENGINE_HOST_STAGE_BOOTSTRAP,
                  0,
                  "runtime thread creation failed",
                  ENGINE_HOST_STATE_FAULTED);
        return -1;
    }

    std::shared_ptr<Command> bootstrap;
    std::future<int32_t> bootstrapCompletion;
    try {
        m_BootstrapResult = {};
        bootstrap = std::make_shared<Command>();
        bootstrap->result = &m_BootstrapResult;
        bootstrap->stage = ENGINE_HOST_STAGE_BOOTSTRAP;
        bootstrap->operation = [this]() {
            if (!m_GraphicsFactory ||
                !m_GraphicsFactory->MakePersistentContextCurrent()) {
                const std::string error = m_GraphicsFactory
                    ? m_GraphicsFactory->GetLastError()
                    : "persistent graphics owner is unavailable";
                if (m_GraphicsFactory) {
                    m_GraphicsFactory->UncurrentPersistentContext();
                }
                m_State = ENGINE_HOST_STATE_FAULTED;
                m_AcceptingCommands = false;
                m_EmergencyTeardownRequested = true;
                m_StopRequested = true;
                FillRejectedResult(&m_BootstrapResult,
                                   ENGINE_HOST_STATUS_GL_FAILED,
                                   ENGINE_HOST_STAGE_GL_CONTEXT,
                                   error.empty() ? "failed to activate persistent context"
                                                 : error.c_str());
                return -1;
            }
            if (InitializeRuntimeResources(&m_BootstrapResult) != 0) {
                m_State = ENGINE_HOST_STATE_FAULTED;
                m_AcceptingCommands = false;
                m_EmergencyTeardownRequested = true;
                m_StopRequested = true;
                return -1;
            }
            SetResult(&m_BootstrapResult,
                      ENGINE_HOST_STATUS_SUCCESS,
                      ENGINE_HOST_STAGE_BOOTSTRAP,
                      0,
                      nullptr,
                      ENGINE_HOST_STATE_READY);
            m_State = ENGINE_HOST_STATE_READY;
            return 0;
        };
        bootstrapCompletion = bootstrap->completion.get_future();
        bool queued = false;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_AcceptingCommands.load() && !m_StopRequested.load() &&
                !m_ThreadExited) {
                m_Commands.push_back(std::move(bootstrap));
                queued = true;
            }
        }
        if (!queued) {
            throw std::runtime_error("runtime thread exited before bootstrap");
        }
    } catch (const std::exception& exception) {
        m_AcceptingCommands = false;
        m_StopRequested = true;
        m_State = ENGINE_HOST_STATE_FAULTED;
        SetResult(result,
                  ENGINE_HOST_STATUS_INIT_FAILED,
                  ENGINE_HOST_STAGE_BOOTSTRAP,
                  0,
                  exception.what(),
                  ENGINE_HOST_STATE_FAULTED);
        return -1;
    } catch (...) {
        m_AcceptingCommands = false;
        m_StopRequested = true;
        m_State = ENGINE_HOST_STATE_FAULTED;
        SetResult(result,
                  ENGINE_HOST_STATUS_INIT_FAILED,
                  ENGINE_HOST_STAGE_BOOTSTRAP,
                  0,
                  "failed to enqueue bootstrap",
                  ENGINE_HOST_STATE_FAULTED);
        return -1;
    }

    m_CommandCondition.notify_one();
    int32_t code = -1;
    bool bootstrapTimedOut = false;
    try {
        if (bootstrapCompletion.wait_for(kRuntimeStartupTimeout) !=
            std::future_status::ready) {
            bootstrapTimedOut = true;
            m_LifecycleTimedOut = true;
            RequestEmergencyStop();
            if (!WaitForRuntimeExit(kRuntimeShutdownTimeout) ||
                !JoinRuntimeIfExited()) {
                FillRejectedResult(result,
                                   ENGINE_HOST_STATUS_INIT_FAILED,
                                   ENGINE_HOST_STAGE_BOOTSTRAP,
                                   "runtime bootstrap did not exit before deadline");
                return -1;
            }
        }
        code = bootstrapCompletion.get();
    } catch (...) {
        code = -1;
    }
    if (bootstrapTimedOut) code = -1;
    if (code == 0) {
        if (result) *result = m_BootstrapResult;
    } else {
        m_State = ENGINE_HOST_STATE_FAULTED;
        if (result && m_BootstrapResult.struct_size != 0) {
            *result = m_BootstrapResult;
        } else {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_INIT_FAILED,
                               ENGINE_HOST_STAGE_BOOTSTRAP,
                               "runtime bootstrap failed");
        }
        RequestEmergencyStop();
        if (!WaitForRuntimeExit(kRuntimeShutdownTimeout) ||
            !JoinRuntimeIfExited()) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_INIT_FAILED,
                               ENGINE_HOST_STAGE_BOOTSTRAP,
                               "runtime bootstrap failure did not exit before deadline");
            return -1;
        }
        TeardownGraphics();
    }
    return code;
}

int32_t EngineHost::RejectUnimplemented(int32_t stage, RuntimeResult* result) {
    SetResult(result,
              ENGINE_HOST_STATUS_INVALID_STATE,
              stage,
              0,
              "operation is not available in the ABI skeleton",
              m_State.load());
    return -1;
}

int32_t EngineHost::OpenProject(const char* manifestPath,
                                 const char* scenePath,
                                 RuntimeResult* result) {
    if (!result) return -1;
    const std::string manifest = manifestPath ? manifestPath : "";
    const std::string scene = scenePath ? scenePath : "";
    return Submit([this, manifest, scene, result]() {
        if (MakeRuntimeContextCurrent() != 0) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_GL_CONTEXT,
                               "failed to make context current before opening project");
            return -1;
        }
        if (!EnsureRuntimeSession() || !m_Session) {
            if (m_GraphicsFactory) m_GraphicsFactory->UncurrentPersistentContext();
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_SESSION_FAILED,
                               ENGINE_HOST_STAGE_OPEN_PROJECT,
                               "runtime session is unavailable");
            return -1;
        }
        if (!m_Session->OpenProject(manifest, scene)) {
            const std::string error = m_Session->GetLastError();
            if (m_GraphicsFactory) m_GraphicsFactory->UncurrentPersistentContext();
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_SESSION_FAILED,
                               ENGINE_HOST_STAGE_OPEN_PROJECT,
                               error.c_str());
            return -1;
        }
        if (!m_GraphicsFactory->UncurrentPersistentContext()) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_GL_CONTEXT,
                               "failed to un-current context after opening project");
            return -1;
        }
        SetResult(result,
                  ENGINE_HOST_STATUS_SUCCESS,
                  ENGINE_HOST_STAGE_SESSION,
                  0,
                  nullptr,
                  m_State.load());
        return 0;
    }, result, ENGINE_HOST_STAGE_OPEN_PROJECT);
}

int32_t EngineHost::Play(int32_t assetIndex, RuntimeResult* result) {
    if (!result) return -1;
    return Submit([this, assetIndex, result]() {
        if (MakeRuntimeContextCurrent() != 0) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_GL_CONTEXT,
                               "failed to make context current before play");
            return -1;
        }
        if (!m_Session) {
            if (m_GraphicsFactory) m_GraphicsFactory->UncurrentPersistentContext();
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_SESSION_FAILED,
                               ENGINE_HOST_STAGE_PLAY,
                               "runtime session is unavailable");
            return -1;
        }
        if (!m_Session->Play(assetIndex)) {
            const std::string error = m_Session->GetLastError();
            if (m_GraphicsFactory) m_GraphicsFactory->UncurrentPersistentContext();
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_SESSION_FAILED,
                               ENGINE_HOST_STAGE_PLAY,
                               error.c_str());
            return -1;
        }
        if (!m_GraphicsFactory->UncurrentPersistentContext()) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_GL_CONTEXT,
                               "failed to un-current context after play");
            return -1;
        }
        SetResult(result,
                  ENGINE_HOST_STATUS_SUCCESS,
                  ENGINE_HOST_STAGE_PLAY,
                  0,
                  nullptr,
                  m_State.load());
        return 0;
    }, result, ENGINE_HOST_STAGE_PLAY);
}

int32_t EngineHost::Reload(RuntimeResult* result) {
    if (!result) return -1;
    return Submit([this, result]() {
        if (MakeRuntimeContextCurrent() != 0) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_GL_CONTEXT,
                               "failed to make context current before reload");
            return -1;
        }
        if (!m_Session) {
            if (m_GraphicsFactory) m_GraphicsFactory->UncurrentPersistentContext();
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_SESSION_FAILED,
                               ENGINE_HOST_STAGE_PLAY,
                               "runtime session is unavailable");
            return -1;
        }
        if (TestFailure("EDITOR_BRIDGE_FAIL_RELOAD")) {
            m_Session->Stop();
            if (m_GraphicsFactory) m_GraphicsFactory->UncurrentPersistentContext();
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_SESSION_FAILED,
                               ENGINE_HOST_STAGE_PLAY,
                               "injected reload failure");
            return -1;
        }
        if (!m_Session->Reload()) {
            const std::string error = m_Session->GetRuntimeError();
            if (m_GraphicsFactory) m_GraphicsFactory->UncurrentPersistentContext();
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_SESSION_FAILED,
                               ENGINE_HOST_STAGE_PLAY,
                               error.c_str());
            return -1;
        }
        if (!m_GraphicsFactory->UncurrentPersistentContext()) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_GL_CONTEXT,
                               "failed to un-current context after reload");
            return -1;
        }
        SetResult(result,
                  ENGINE_HOST_STATUS_SUCCESS,
                  ENGINE_HOST_STAGE_PLAY,
                  0,
                  nullptr,
                  m_State.load());
        return 0;
    }, result, ENGINE_HOST_STAGE_PLAY);
}

int32_t EngineHost::Stop(RuntimeResult* result) {
    if (!result) return -1;
    // D3-H8-2：Stop 是 terminal teardown 的一种，必须作废 outstanding pick
    // （含「已受理但尚未 dispatch」的那个），否则 Stop 之后 Fetch 仍会成功
    // 返回旧结果，违反已冻结的 D3 teardown 语义。
    InvalidatePickResult("host stopped");
    if (m_State.load() == ENGINE_HOST_STATE_FAULTED) {
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_SHUTDOWN_FAILED,
                           ENGINE_HOST_STAGE_SHUTDOWN,
                           "host is faulted");
        return -1;
    }
    if (m_ShutdownComplete.load() ||
        (!m_RuntimeThread.joinable() && m_State.load() == ENGINE_HOST_STATE_STOPPED)) {
        SetResult(result,
                  ENGINE_HOST_STATUS_SUCCESS,
                  ENGINE_HOST_STAGE_SHUTDOWN,
                  0,
                  nullptr,
                  ENGINE_HOST_STATE_STOPPED);
        return 0;
    }
    return Submit([this, result]() {
        if (!m_Session || !m_Session->IsPlaying()) {
            SetResult(result,
                      ENGINE_HOST_STATUS_SKIPPED_NOT_PLAYING,
                      ENGINE_HOST_STAGE_SHUTDOWN,
                      0,
                      nullptr,
                      m_State.load());
            return 0;
        }
        if (MakeRuntimeContextCurrent() != 0) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_GL_CONTEXT,
                               "failed to make context current before stop");
            return -1;
        }
        m_Session->Stop();
        if (!m_GraphicsFactory->UncurrentPersistentContext()) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_GL_CONTEXT,
                               "failed to un-current context after stop");
            return -1;
        }
        SetResult(result,
                  ENGINE_HOST_STATUS_SUCCESS,
                  ENGINE_HOST_STAGE_SHUTDOWN,
                  0,
                  nullptr,
                  m_State.load());
        return 0;
    }, result, ENGINE_HOST_STAGE_SHUTDOWN);
}

int32_t EngineHost::PumpOneFrame(float requestedDtSeconds, RuntimeResult* result) {
    if (!result) return -1;
    return Submit([this, requestedDtSeconds, result]() {
        if (TestFailure("EDITOR_BRIDGE_FAIL_RUNTIME_COMMAND") ||
            TestFailure("EDITOR_BRIDGE_FAIL_FRAME")) {
            throw std::runtime_error("injected runtime command failure");
        }
        if (!std::isfinite(requestedDtSeconds) || requestedDtSeconds < 0.0f) {
            SetResult(result,
                      ENGINE_HOST_STATUS_TICK_FAULT,
                      ENGINE_HOST_STAGE_TICK,
                      0,
                      "dt must be finite and non-negative",
                      m_State.load());
            return -1;
        }
        if (MakeRuntimeContextCurrent() != 0) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_TICK,
                               "runtime resources are not ready");
            return -1;
        }
        // ── Slice E：在 current-context 窗口内、渲染**之前**应用 viewport 尺寸 ──
        // 顺序保证：FBO 尺寸 → 渲染 viewport → 读回 → frame header 全部同帧一致，
        // 因此不存在"新元数据 + 旧像素"。失败时本帧不发布（fail-closed）。
        const int32_t viewportApply = ApplyPendingViewportSize(result);
        if (viewportApply != 0) {
            // 重建失败 ⇒ 本帧**不发布任何帧**：不渲染、不推进 frame id。
            // host 保持存活（不 teardown）；pending pick 在此结掉（NOT_READY）。
            // 结果用 SUCCESS + 诊断说明"这一帧没有发布"，这样 managed 侧不会把
            // 它当成 runtime fault 而停泵（停泵会让"保留最后一张 bitmap"失效）。
            m_Application->SetProductionPickPassEnabled(false);
            ServicePickRequestForFrame();
            if (m_GraphicsFactory) m_GraphicsFactory->UncurrentPersistentContext();
            SetResult(result,
                      ENGINE_HOST_STATUS_SUCCESS,
                      ENGINE_HOST_STAGE_FBO,
                      0,
                      "viewport resize failed; no frame was published (fail-closed)",
                      m_State.load());
            return 0;
        }
        // A7 pending-edit drain：仅在 context 已 current 的窗口内执行，
        // 且位于 Engine update 之前，使 edit 对本帧可见。
        DrainPendingEdits();
        if (!m_Application || !m_OffscreenFbo) {
            RequestEmergencyStop();
            if (m_GraphicsFactory) m_GraphicsFactory->UncurrentPersistentContext();
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_TICK,
                               "runtime resources are not ready");
            return -1;
        }
        const float effectiveDt = std::min(requestedDtSeconds,
                                             kMaxEffectiveDtSeconds);
        m_ReadbackVerified = false;
        if (!m_Application->PumpOneFrame(effectiveDt)) {
            RequestEmergencyStop();
            if (m_GraphicsFactory) m_GraphicsFactory->UncurrentPersistentContext();
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_TICK_FAULT,
                               ENGINE_HOST_STAGE_ENGINE_SUBSYSTEM,
                               "Application frame pump failed");
            return -1;
        }
        if (m_Session && m_Session->IsPlaying()) {
            const bool tickInjected = TestFailure("EDITOR_BRIDGE_FAIL_RUNTIME_TICK");
            if (tickInjected || !m_Session->RuntimeTick(effectiveDt)) {
                const std::string error = tickInjected
                    ? "injected runtime tick failure"
                    : (m_Session->GetRuntimeError().empty()
                           ? "runtime script tick failed"
                           : m_Session->GetRuntimeError());
                RequestEmergencyStop();
                if (m_GraphicsFactory) m_GraphicsFactory->UncurrentPersistentContext();
                FillRejectedResult(result,
                                   ENGINE_HOST_STATUS_TICK_FAULT,
                                   ENGINE_HOST_STAGE_TICK,
                                   error.c_str());
                return -1;
            }
            ++m_TickCount;
        }
        m_OffscreenFbo->Clear(0.1f, 0.2f, 0.3f, 1.0f);
        uint8_t pixel[4] = {};
        if (!m_OffscreenFbo->ReadColorPixel(32, 32, pixel)) {
            RequestEmergencyStop();
            if (m_GraphicsFactory) m_GraphicsFactory->UncurrentPersistentContext();
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_FBO,
                               "offscreen FBO readback failed");
            return -1;
        }
        const bool pixelMatches =
            std::abs(static_cast<int>(pixel[0]) - 26) <= 1 &&
            std::abs(static_cast<int>(pixel[1]) - 51) <= 1 &&
            std::abs(static_cast<int>(pixel[2]) - 77) <= 1 &&
            std::abs(static_cast<int>(pixel[3]) - 255) <= 1;
        if (!pixelMatches) {
            RequestEmergencyStop();
            if (m_GraphicsFactory) m_GraphicsFactory->UncurrentPersistentContext();
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_FBO,
                               "offscreen FBO readback color mismatch");
            return -1;
        }
        m_ReadbackVerified = true;

        // ── HRC-3 P1：production scene render → presentation target ──
        //
        // 编排者职责（P1-H3）：由**既有** frame 驱动，render 本身归 Application 所有（P1-a）。
        //
        // ⚠ P1.1 BLOCKED（2026-09-28）：这条渲染路径目前**必须保持关闭**，原因有两条，
        // 都是实测结论而不是推测（完整分层证据见 docs/Editor-Avalonia-HRC3-P1-Blocked-Log.md）：
        //   B1 内容层：合法 draw（4 次 batch 提交、glError=0、常量色片元着色器）**不产生任何片元**，
        //      attachment 0/1 均为空 —— sprite 渲染管线本身是死代码，从未执行过。
        //   B2 状态层：一旦在每帧里真的跑这条路径，后续 host 的 GLFW 初始化会失败
        //      （test_bridge 由 41/41 退化为多项失败），即污染 HRC-2 冻结证据。
        // 因此在 P1.1 定位并修复根因之前，这里显式关闭；**不允许**为了让测试变绿而
        // 打开它，也不允许用 clear-only / default quad 伪造 scene proof。
        constexpr bool kEnableProductionSceneRender = true;
        // HRC-3 D3：只有本帧确实登记了 pick 请求才打开 pick pass（D3-C1 的同帧约束）。
        // 关闭状态是默认值，render 之后立刻复位（见下方 ServicePickRequestForFrame）。
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_PickFrameRequest.active) {
                m_Application->SetProductionPickPassEnabled(true);
            }
        }
        if (kEnableProductionSceneRender && m_Session && m_Session->GetEditScene()) {
            // 场景反序列化只保留 sprite GUID，不绑纹理 ⇒ RenderCommand::texture 全是 null
            // ⇒ SceneRenderer 会跳过全部 sprite。先做一次幂等绑定（纯读 GUID，不改语义）。
            m_Session->BindEditSceneSpriteTextures();
            // ── HRC-3 P1.1 根因修复（B1）────────────────────────────────────────
            // B1 根因：`OpenGLFramebuffer::Clear()` 的实现是 Bind → Clear → **Unbind**，
            // 因此旧顺序 `Bind(); Clear();` 在 Clear() 返回时已经把 target 换回默认帧缓冲，
            // 随后的 RenderProductionFrame() 就把片元画进了默认帧缓冲，presentation FBO
            // 只剩 clear 色 —— 而读回路径显式 Bind() 后读的是 presentation FBO，
            // 于是"draw target 与 read target 不一致"，表现为：合法 draw、glError 全 0、
            // renderables=9、batch 收数正常，但 presentation FBO 480,000/480,000 全 clear。
            //
            // 修法：**先 Clear()、再 Bind()**，让"清理 → 绑定 → viewport → 渲染 → 解绑"
            // 全程使用同一个 target。
            m_PresentationFbo->Clear(0.05f, 0.05f, 0.07f, 1.0f);
            m_PresentationFbo->Bind();
            m_GraphicsFactory->GetGLContext().Viewport(
                0, 0, m_PresentationFbo->GetWidth(), m_PresentationFbo->GetHeight());

            // ── 回归断言（B1 的可执行守卫）────────────────────────────────────────
            // 就在渲染边界上验证"即将被写入的 draw target 确实是 presentation FBO"。
            // 这不是修复手段（修复是上面的顺序更正），而是把当初失败的那条不变量
            // 变成可执行检查，使同类顺序回归立刻失败而不是静默产出 clear-only 帧。
            // 只读检查，不改任何 GL 状态、不引入新 ABI。
            {
                GLint currentDrawFbo = 0;
                m_GraphicsFactory->GetGLContext().GetIntegerv(
                    GL_DRAW_FRAMEBUFFER_BINDING, &currentDrawFbo);
                const GLint expectedFbo =
                    static_cast<GLint>(m_PresentationFbo->GetFramebufferID());
                if (currentDrawFbo != expectedFbo || expectedFbo == 0) {
                    RequestEmergencyStop();
                    if (m_GraphicsFactory) m_GraphicsFactory->UncurrentPersistentContext();
                    FillRejectedResult(result,
                                       ENGINE_HOST_STATUS_GL_FAILED,
                                       ENGINE_HOST_STAGE_FBO,
                                       "production render draw target is not the presentation FBO");
                    return -1;
                }
            }

            const bool rendered = m_Application->RenderProductionFrame(
                *m_RenderContext,
                *m_Session->GetEditScene(),
                m_PresentationFbo->GetWidth(),
                m_PresentationFbo->GetHeight());
            m_PresentationFbo->Unbind();
            if (!rendered) {
                RequestEmergencyStop();
                if (m_GraphicsFactory) m_GraphicsFactory->UncurrentPersistentContext();
                FillRejectedResult(result,
                                   ENGINE_HOST_STATUS_GL_FAILED,
                                   ENGINE_HOST_STAGE_FBO,
                                   "production scene render produced no renderable frame");
                return -1;
            }
            m_LastProductionRenderableCount = m_Application->LastProductionRenderableCount();
        }

        // ── HRC-3 D3：本帧 pick 的第二阶段 ──
        // readback 必须发生在 production render **之后**、context 仍 current 时，
        // 这样 result 才真正绑定到渲染了 ID 的那一帧（D3-C1/D3-C4）。
        // 随后立刻关闭 pick pass：它只在"确实有请求"的那一帧运行（U2 冻结语义），
        // 不会变成每帧无条件开销。
        m_Application->SetProductionPickPassEnabled(false);
        ServicePickRequestForFrame();


        if (!m_GraphicsFactory->UncurrentPersistentContext()) {
            RequestEmergencyStop();
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_GL_CONTEXT,
                               "failed to un-current context after frame");
            return -1;
        }
        ++m_FrameId;
        SetResult(result,
                  m_Session && m_Session->IsPlaying()
                      ? ENGINE_HOST_STATUS_SUCCESS
                      : ENGINE_HOST_STATUS_SKIPPED_NOT_PLAYING,
                  ENGINE_HOST_STAGE_TICK,
                  0,
                  nullptr,
                  m_State.load());
        result->raw_dt_seconds = requestedDtSeconds;
        result->effective_dt_seconds = effectiveDt;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_LastFrameResult = *result;
        }
        return 0;
    }, result, ENGINE_HOST_STAGE_TICK);
}

int32_t EngineHost::CloseProject(RuntimeResult* result) {
    if (!result) return -1;
    return Submit([this, result]() {
        // 未执行的 GL edit 必须得到确定结论，不能静默丢弃。
        AbandonPendingEdits("project closed before edit executed");
        {
            // Slice E：未应用的 viewport 尺寸随 project 一起丢弃（下次 bootstrap
            // 会按默认尺寸重建 presentation target）。
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_PendingViewportSize = PendingViewportSize{};
            m_ViewportResizeRetries = 0;
        }
        if (MakeRuntimeContextCurrent() != 0) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_GL_CONTEXT,
                               "failed to make context current before close");
            return -1;
        }
        // Slice E：重建失败会让 presentation target 处于无效状态，且**无法**自动恢复
        // （旧资源已被销毁）。关 project 是明确的恢复点：把 target 重建回默认尺寸。
        if (m_PresentationFbo && !m_PresentationTargetValid.load()) {
            if (m_PresentationFbo->Resize(ENGINE_HOST_PRESENTATION_DEFAULT_WIDTH,
                                          ENGINE_HOST_PRESENTATION_DEFAULT_HEIGHT) &&
                m_PresentationFbo->IsValid()) {
                m_PresentationTargetValid = true;
                m_ReadbackVerified = false;
            }
        }
        if (m_Session) {
            m_Session->SetEventCallback(nullptr);
            m_Session->Stop();
            PublishSessionProofState();
            m_Session.reset();
        }
        m_SessionAttached = false;
        PublishSessionProofState();
        m_TickCount = 0;
        if (!m_GraphicsFactory->UncurrentPersistentContext()) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_GL_CONTEXT,
                               "failed to un-current context after close");
            return -1;
        }
        SetResult(result,
                  ENGINE_HOST_STATUS_SUCCESS,
                  ENGINE_HOST_STAGE_SESSION,
                  0,
                  nullptr,
                  m_State.load());
        return 0;
    }, result, ENGINE_HOST_STAGE_OPEN_PROJECT);
}

int32_t EngineHost::Shutdown(RuntimeResult* result) {
    if (!IsCreationThread()) {
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_WRONG_THREAD,
                           ENGINE_HOST_STAGE_SHUTDOWN,
                           "shutdown must run on the creation thread");
        return -1;
    }
    if (IsOwnerThread()) {
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_WRONG_THREAD,
                           ENGINE_HOST_STAGE_SHUTDOWN,
                           "shutdown cannot run on the runtime thread");
        return -1;
    }

    std::lock_guard<std::mutex> lifecycleLock(m_LifecycleMutex);
    auto waitAndJoinRuntime = [this]() -> bool {
        if (!m_RuntimeThread.joinable()) return true;
        if (!WaitForRuntimeExit(kRuntimeShutdownTimeout)) {
            m_LifecycleTimedOut = true;
            RequestEmergencyStop();
            return false;
        }
        return JoinRuntimeIfExited();
    };

    if (m_State.load() == ENGINE_HOST_STATE_FAULTED) {
        RequestEmergencyStop();
        if (!waitAndJoinRuntime()) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_SHUTDOWN_FAILED,
                               ENGINE_HOST_STAGE_SHUTDOWN,
                               "runtime thread did not exit before shutdown deadline");
            return -1;
        }
        // HRC-3 P1.1 B2 修复 (a)：FAULTED 路径原本**只**调 TeardownGraphics()，
        // 于是 GL context / GLFW 先被销毁，Application 随后靠**隐式成员析构**释放，
        // 而成员声明顺序是 m_Application(254) < m_RenderContext(255) ⇒ 逆序析构让
        // **context 先死、Application 后死** ⇒ ~OpenGLSpriteBatch / ~OpenGLShader
        // 在无 current context 的情况下发出 glDelete*，污染进程，使同进程后续 host
        // 的 glfwInit() 失败。
        //
        // 修法：FAULTED 路径必须**先**做 runtime 资源拆除，**再**做 graphics/context 拆除。
        // 关键条件：不能假设 join 完成后 context 仍然 current —— 这里显式把它设为 current。
        bool runtimeTeardown = true;
        bool contextCurrent = m_GraphicsFactory &&
                              m_GraphicsFactory->MakePersistentContextCurrent();
        if (!contextCurrent) {
            runtimeTeardown = false;
        } else {
            runtimeTeardown = TeardownRuntimeResources();
            if (!m_GraphicsFactory->UncurrentPersistentContext()) {
                runtimeTeardown = false;
            }
        }
        const bool graphicsTeardown = TeardownGraphics();
        m_ShutdownComplete = false;
        m_TickCount = 0;
        m_FboId = 0;
        m_FboWidth = 0;
        m_FboHeight = 0;
        m_ApplicationReady = false;
        m_FboReady = false;
        m_SessionAttached = false;
        PublishSessionProofState();
        m_ReadbackVerified = false;
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_SHUTDOWN_FAILED,
                           ENGINE_HOST_STAGE_SHUTDOWN,
                           (!runtimeTeardown && !graphicsTeardown)
                               ? "host is faulted; runtime and graphics teardown both failed"
                               : (runtimeTeardown
                                      ? "host is already faulted"
                                      : "host is faulted and runtime resource teardown failed"));
        return -1;
    }

    if (!m_RuntimeThread.joinable()) {
        m_AcceptingCommands = false;
        const bool graphicsTeardown = TeardownGraphics();
        if (!graphicsTeardown) {
            m_State = ENGINE_HOST_STATE_FAULTED;
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_SHUTDOWN_FAILED,
                               ENGINE_HOST_STAGE_SHUTDOWN,
                               "persistent graphics carrier teardown failed");
            return -1;
        }
        m_State = ENGINE_HOST_STATE_STOPPED;
        m_ShutdownComplete = true;
        SetResult(result,
                  ENGINE_HOST_STATUS_SHUTDOWN_COMPLETE,
                  ENGINE_HOST_STAGE_SHUTDOWN,
                  0,
                  nullptr,
                  ENGINE_HOST_STATE_STOPPED);
        return 0;
    }

    bool threadExited = false;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        threadExited = m_ThreadExited;
    }
    if (threadExited) {
        if (!waitAndJoinRuntime()) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_SHUTDOWN_FAILED,
                               ENGINE_HOST_STAGE_SHUTDOWN,
                               "runtime thread did not exit before shutdown deadline");
            return -1;
        }
        if (!m_ShutdownComplete.load()) {
            m_State = ENGINE_HOST_STATE_FAULTED;
            const bool graphicsTeardown = TeardownGraphics();
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_SHUTDOWN_FAILED,
                               ENGINE_HOST_STAGE_SHUTDOWN,
                               graphicsTeardown
                                   ? "runtime thread exited before shutdown completed"
                                   : "runtime exited and graphics teardown failed");
            return -1;
        }
        SetResult(result,
                  ENGINE_HOST_STATUS_SHUTDOWN_COMPLETE,
                  ENGINE_HOST_STAGE_SHUTDOWN,
                  0,
                  nullptr,
                  ENGINE_HOST_STATE_STOPPED);
        return 0;
    }

    m_AcceptingCommands = false;
    std::shared_ptr<Command> command;
    std::future<int32_t> completion;
    try {
        command = std::make_shared<Command>();
        command->result = nullptr;
        command->stage = ENGINE_HOST_STAGE_SHUTDOWN;
        command->operation = [this]() {
            if (MakeRuntimeContextCurrent() != 0 ||
                !TeardownRuntimeResources() ||
                !m_GraphicsFactory ||
                !m_GraphicsFactory->UncurrentPersistentContext()) {
                m_State = ENGINE_HOST_STATE_FAULTED;
                m_AcceptingCommands = false;
                m_EmergencyTeardownRequested = true;
                SetDiagnostic("failed to tear down runtime resources");
                m_StopRequested = true;
                return -1;
            }
            SetDiagnostic(nullptr);
            m_State = ENGINE_HOST_STATE_STOPPED;
            m_StopRequested = true;
            return 0;
        };
        completion = command->completion.get_future();
        bool queued = false;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (!m_ThreadExited) {
                m_Commands.push_back(std::move(command));
                queued = true;
            }
        }
        if (!queued) {
            RequestEmergencyStop();
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_SHUTDOWN_FAILED,
                               ENGINE_HOST_STAGE_SHUTDOWN,
                               "runtime thread exited before shutdown");
            waitAndJoinRuntime();
            return -1;
        }
    } catch (...) {
        RequestEmergencyStop();
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_SHUTDOWN_FAILED,
                           ENGINE_HOST_STAGE_SHUTDOWN,
                           "failed to enqueue shutdown");
        waitAndJoinRuntime();
        return -1;
    }

    m_CommandCondition.notify_one();

    int32_t code = -1;
    bool completionTimedOut = false;
    try {
        if (completion.wait_for(kRuntimeShutdownTimeout) !=
            std::future_status::ready) {
            completionTimedOut = true;
            m_LifecycleTimedOut = true;
            RequestEmergencyStop();
            if (!waitAndJoinRuntime()) {
                FillRejectedResult(result,
                                   ENGINE_HOST_STATUS_SHUTDOWN_FAILED,
                                   ENGINE_HOST_STAGE_SHUTDOWN,
                                   "runtime thread did not exit before shutdown deadline");
                return -1;
            }
        }
        code = completion.get();
    } catch (...) {
        code = -1;
    }
    if (completionTimedOut) code = -1;

    if (code != 0) RequestEmergencyStop();
    if (!waitAndJoinRuntime()) {
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_SHUTDOWN_FAILED,
                           ENGINE_HOST_STAGE_SHUTDOWN,
                           "runtime thread did not exit before shutdown deadline");
        return -1;
    }

    const bool graphicsTeardown = TeardownGraphics();
    if (code == 0) {
        if (!graphicsTeardown) {
            m_State = ENGINE_HOST_STATE_FAULTED;
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_SHUTDOWN_FAILED,
                               ENGINE_HOST_STAGE_SHUTDOWN,
                               "persistent graphics carrier teardown failed");
            return -1;
        }
        m_State = ENGINE_HOST_STATE_STOPPED;
        m_ShutdownComplete = true;
        SetResult(result,
                  ENGINE_HOST_STATUS_SHUTDOWN_COMPLETE,
                  ENGINE_HOST_STAGE_SHUTDOWN,
                  0,
                  nullptr,
                  ENGINE_HOST_STATE_STOPPED);
        return 0;
    }

    m_ShutdownComplete = false;
    if (!graphicsTeardown) {
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_SHUTDOWN_FAILED,
                           ENGINE_HOST_STAGE_SHUTDOWN,
                           "persistent graphics carrier teardown failed after runtime failure");
    } else {
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_SHUTDOWN_FAILED,
                           ENGINE_HOST_STAGE_SHUTDOWN,
                           "shutdown command failed");
    }
    return -1;
}

int32_t EngineHost::GetLastFrameResult(RuntimeResult* result) {
    if (!result) return -1;
    std::lock_guard<std::mutex> lock(m_Mutex);
    *result = m_LastFrameResult;
    return 0;
}

void EngineHost::ForwardEvent(int32_t type,
                              uint64_t generation,
                              const char* payload) {
    std::lock_guard<std::recursive_mutex> callbackLock(m_CallbackMutex);
    EngineHostEventCallback callback = nullptr;
    void* userData = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        callback = m_EventCallback;
        userData = m_EventUserData;
    }
    if (callback) callback(type, generation, payload, userData);
}

int32_t EngineHost::SetEventCallback(EngineHostEventCallback callback,
                                      void* userData) {
    std::lock_guard<std::recursive_mutex> callbackLock(m_CallbackMutex);
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_EventCallback = callback;
    m_EventUserData = userData;
    return 0;
}

int32_t EngineHost::GetGraphicsState(int32_t* ready, int32_t* current) {
    if (!ready || !current) return -1;
    std::lock_guard<std::mutex> lifecycleLock(m_LifecycleMutex);
    *ready = m_GraphicsFactory &&
             m_GraphicsFactory->IsPersistentContextReady() ? 1 : 0;
    *current = m_GraphicsFactory &&
               m_GraphicsFactory->IsPersistentContextCurrent() ? 1 : 0;
    if (m_State.load() == ENGINE_HOST_STATE_FAULTED ||
        (m_GraphicsTeardownAttempted.load() &&
         !m_GraphicsTeardownSucceeded.load()) ||
        (m_EmergencyTeardownRequested.load() &&
         !m_EmergencyTeardownCompleted.load())) {
        return -1;
    }
    return 0;
}

int32_t EngineHost::GetGraphicsCounters(uint64_t* makeCurrent,
                                          uint64_t* uncurrent) {
    if (!makeCurrent || !uncurrent) return -1;
    std::lock_guard<std::mutex> lifecycleLock(m_LifecycleMutex);
    if (m_GraphicsFactory) {
        *makeCurrent = m_GraphicsFactory->GetMakeCurrentCount();
        *uncurrent = m_GraphicsFactory->GetUncurrentCount();
    } else {
        *makeCurrent = m_LastMakeCurrentCount.load();
        *uncurrent = m_LastUncurrentCount.load();
    }
    return 0;
}

int32_t EngineHost::GetGlobalProofState(
    int32_t* applicationPresent,
    int32_t* timeInitialized,
    int32_t* sceneInitialized,
    int32_t* jobSystemPresent,
    int32_t* resourceManagerPresent,
    int32_t* fileWatcherPresent) {
    if (!applicationPresent || !timeInitialized || !sceneInitialized ||
        !jobSystemPresent || !resourceManagerPresent ||
        !fileWatcherPresent) return -1;
    *applicationPresent = m_GlobalApplicationPresent.load() ? 1 : 0;
    *timeInitialized = m_GlobalTimeInitialized.load() ? 1 : 0;
    *sceneInitialized = m_GlobalSceneInitialized.load() ? 1 : 0;
    *jobSystemPresent = m_GlobalJobSystemPresent.load() ? 1 : 0;
    *resourceManagerPresent = m_GlobalResourceManagerPresent.load() ? 1 : 0;
    *fileWatcherPresent = m_GlobalFileWatcherPresent.load() ? 1 : 0;
    return 0;
}

int32_t EngineHost::GetSessionProofState(EditorSessionRuntimeStats* stats) {
    if (!stats) return -1;
    std::lock_guard<std::mutex> proofLock(m_SessionProofMutex);
    stats->struct_size = sizeof(*stats);
    stats->stop_count = m_SessionStopCount.load();
    stats->on_create_count = m_SessionOnCreateCount.load();
    stats->on_destroy_count = m_SessionOnDestroyCount.load();
    stats->gameplay_handle_count = m_SessionGameplayHandleCount.load();
    stats->playing = m_SessionPlaying.load();
    stats->runtime_scene_present = m_SessionRuntimeScenePresent.load();
    stats->script_valid = m_SessionScriptValid.load();
    stats->callback_attached = m_SessionCallbackAttached.load();
    stats->gameplay_scene_present = m_SessionGameplayScenePresent.load();
    stats->hud_dirty = m_SessionHudDirty.load();
    stats->session_present = m_SessionProofPresent.load();
    stats->allocator_present = m_SessionAllocatorPresent.load();
    return 0;
}

int32_t EngineHost::GetRuntimeProofState(
    int32_t* applicationReady,
    int32_t* fboReady,
    int32_t* sessionAttached,
    int32_t* readbackVerified,
    uint32_t* fboId,
    uint32_t* fboWidth,
    uint32_t* fboHeight,
    uint64_t* frameCount,
    uint64_t* tickCount) {
    if (!applicationReady || !fboReady || !sessionAttached ||
        !readbackVerified || !fboId || !fboWidth || !fboHeight ||
        !frameCount || !tickCount) return -1;
    std::lock_guard<std::mutex> lifecycleLock(m_LifecycleMutex);
    *applicationReady = m_ApplicationReady.load() ? 1 : 0;
    *fboReady = m_FboReady.load() ? 1 : 0;
    *sessionAttached = m_SessionAttached.load() ? 1 : 0;
    *readbackVerified = m_ReadbackVerified.load() ? 1 : 0;
    *fboId = m_FboId.load();
    *fboWidth = m_FboWidth.load();
    *fboHeight = m_FboHeight.load();
    *frameCount = m_FrameId;
    *tickCount = m_TickCount.load();
    return 0;
}

// ================================================================
// HRC-3 P1：presentation target 读回（**C++ 内部测试 seam，不是 C ABI 导出**）
//
// 为什么是白色盒：P1 阶段刻意不新增 C 导出（那是 P2 的 additive Fetch op）。
// 但 P1 的验收要求"像素确实不同于 clear-only 帧"，没有读回就无法取证，
// 因此这里提供仅供 native contract test 使用的内部读回。managed 侧通道留给 P2。
//
// 线程纪律与 P2-H4 同源：GL 访问只在 runtime 线程、context current 时发生。
// ================================================================
int32_t EngineHost::PresentationTargetWidth() const {
    return m_PresentationFbo ? static_cast<int32_t>(m_PresentationFbo->GetWidth()) : 0;
}

int32_t EngineHost::PresentationTargetHeight() const {
    return m_PresentationFbo ? static_cast<int32_t>(m_PresentationFbo->GetHeight()) : 0;
}

int32_t EngineHost::ReadPresentationPixels(std::vector<uint8_t>& out) {
    out.clear();
    if (!m_PresentationFbo || !m_GraphicsFactory || !m_RenderContext) return 0;
    const int32_t width = static_cast<int32_t>(m_PresentationFbo->GetWidth());
    const int32_t height = static_cast<int32_t>(m_PresentationFbo->GetHeight());
    if (width <= 0 || height <= 0) return 0;

    auto result = RuntimeResult{};
    const int32_t code = Submit(
        [this, width, height, &out]() -> int32_t {
            if (MakeRuntimeContextCurrent() != 0) return -1;
            auto& gl = m_GraphicsFactory->GetGLContext();
            m_PresentationFbo->Bind();
            gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
            out.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);
            gl.ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
            m_PresentationFbo->Unbind();
            m_GraphicsFactory->UncurrentPersistentContext();
            return 0;
        },
        &result,
        ENGINE_HOST_STAGE_FBO);
    if (code != 0) {
        out.clear();
        return 0;
    }
    return static_cast<int32_t>(out.size());
}

int32_t EngineHost::GetTeardownState(int32_t* emergencyRequested,
                                     int32_t* emergencyCompleted,
                                     int32_t* graphicsTeardownAttempted,
                                     int32_t* graphicsTeardownSucceeded,
                                     int32_t* lifecycleTimedOut) {
    if (!emergencyRequested || !emergencyCompleted ||
        !graphicsTeardownAttempted || !graphicsTeardownSucceeded ||
        !lifecycleTimedOut) return -1;
    *emergencyRequested = m_EmergencyTeardownRequested.load() ? 1 : 0;
    *emergencyCompleted = m_EmergencyTeardownCompleted.load() ? 1 : 0;
    *graphicsTeardownAttempted = m_GraphicsTeardownAttempted.load() ? 1 : 0;
    *graphicsTeardownSucceeded = m_GraphicsTeardownSucceeded.load() ? 1 : 0;
    *lifecycleTimedOut = m_LifecycleTimedOut.load() ? 1 : 0;
    return 0;
}

int32_t EngineHost::GetLastDiagnostic(char* out, int32_t cap) {
    if (cap < 0 || (!out && cap != 0)) return -1;
    std::lock_guard<std::mutex> lifecycleLock(m_LifecycleMutex);
    std::lock_guard<std::mutex> lock(m_Mutex);
    const int32_t length = static_cast<int32_t>(m_LastDiagnostic.size());
    if (cap == 0) return length;
    const int32_t copy = std::min(length, cap - 1);
    if (copy > 0) {
        std::memcpy(out, m_LastDiagnostic.data(), static_cast<size_t>(copy));
    }
    out[copy] = '\0';
    return length;
}

// ════════════════════════════════════════════════════════════
// A7 op-dispatch seam
//
// 不新增第二个 executor：所有 SESSION op 复用既有 Submit()，
// 因此仍只有 native runtime thread 一个执行者（A7-C03 / G1-06）。
// GL 依赖 op 走 pending-edit queue，只在 PumpOneFrame 已持有
// context 的窗口内执行（A7-C04 / G1-04），frame 边界仍
// graphicsCurrent == 0（A4 invariant / G1-05）。
// ════════════════════════════════════════════════════════════

bool EngineHost::IsKnownOp(int32_t op) noexcept {
    return op > ENGINE_HOST_OP_NONE && op < ENGINE_HOST_OP_COUNT;
}

bool EngineHost::RequiresGraphicsContext(int32_t op) noexcept {
    // HRC-3 P2：`GET_PRESENTATION_FRAME` 确实需要 GL context（读回在 runtime 线程内
    // make-current 后完成），因此这里**必须**正确标记为 true。
    // 注意：这会让 Dispatch 把它路由进 pending-edit 队列，所以 Dispatch 侧另有
    // IsFetchOnlyOp 显式拦截（见下），保证它只能经 EngineHost_Fetch 使用。
    return op == ENGINE_HOST_OP_ASSIGN_SPRITE ||
           op == ENGINE_HOST_OP_GET_PRESENTATION_FRAME ||
           // HRC-3 D3：pick 需要 GL context，且 readback 必须在 runtime 线程的
           // make-current 窗口内完成，因此路由进 pending-edit 队列。
           op == ENGINE_HOST_OP_PICK;
}

bool EngineHost::IsFetchOnlyOp(int32_t op) noexcept {
    // HRC-3 P2：presentation frame 只能通过 EngineHost_Fetch 取。
    // HRC-3 D3：`PICK` 的**结果读取**同样只能经 EngineHost_Fetch。
    // 这里**不**把 PICK 列为 fetch-only：同一个 op 值在 Dispatch 上表示
    // "提交请求"（RequiresGraphicsContext → pending edit），在 Fetch 上才表示
    // "取回结果"。两个入口语义分离，结果读取因此无法误走普通 Dispatch 执行路径。
    return op == ENGINE_HOST_OP_GET_PRESENTATION_FRAME;
}

int32_t EngineHost::FetchCapacityForOp(int32_t op) noexcept {
    switch (op) {
    case ENGINE_HOST_OP_PICK:
        // HRC-3 D3：定长 32B —— 只搬一个像素的 readback，ID 表面不跨 ABI。
        return ENGINE_HOST_PICK_RESULT_PACKET_BYTES;
    case ENGINE_HOST_OP_GET_PRESENTATION_FRAME:
        // HRC-3 P2：静态容量，无协商、无分配。尺寸在编译期固定。
        return ENGINE_HOST_FRAME_PACKET_BYTES;
    case ENGINE_HOST_OP_GET_ENTITY_NAME:
    case ENGINE_HOST_OP_GET_ASSET_PATH:
    case ENGINE_HOST_OP_GET_ASSET_GUID:
    case ENGINE_HOST_OP_GET_ENTITY_SPRITE:
    case ENGINE_HOST_OP_GET_ENTITY_SCRIPT:
    case ENGINE_HOST_OP_SCRIPT_READ:
    case ENGINE_HOST_OP_GET_WARNINGS:
        return 4096;
    case ENGINE_HOST_OP_GET_COMPONENT_PROPERTY:
        return 512;
    case ENGINE_HOST_OP_GET_ENTITY_POSITION:
        return static_cast<int32_t>(sizeof(float) * 3);
    default:
        return 0;
    }
}

EditorSession* EngineHost::RequireSessionForEdit(RuntimeResult* result) noexcept {
    if (m_Session) return m_Session.get();
    RecordEditFailure("editor session is not attached; open a project first");
    if (result) {
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_INVALID_STATE,
                           ENGINE_HOST_STAGE_EDIT,
                           "editor session is not attached; open a project first");
    }
    return nullptr;
}

void EngineHost::RecordEditFailure(const std::string& diagnostic) {
    SetDiagnostic(diagnostic.c_str());
}

int32_t EngineHost::ExecuteSessionOp(int32_t op,
                                     int32_t arg0,
                                     int32_t arg1,
                                     const char* strArg,
                                     const char* strArg2,
                                     const char* strArg3,
                                     const float* floatArg,
                                     RuntimeResult* result) {
    const std::string text = strArg ? strArg : "";

    auto fail = [&](const char* fallback) {
        const std::string& last = m_Session ? m_Session->GetLastError()
                                            : std::string();
        const std::string diagnostic = last.empty() ? std::string(fallback) : last;
        RecordEditFailure(diagnostic);
        if (result) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_SESSION_FAILED,
                               ENGINE_HOST_STAGE_EDIT,
                               diagnostic.c_str());
        }
        return -1;
    };

    switch (op) {
    case ENGINE_HOST_OP_CREATE_ENTITY: {
        const int32_t index = m_Session->CreateEntity(strArg);
        if (index < 0) return fail("create entity failed");
        return index;
    }
    case ENGINE_HOST_OP_DELETE_ENTITY:
        return m_Session->DeleteEntity(arg0) ? 0 : fail("delete entity failed");
    case ENGINE_HOST_OP_RENAME_ENTITY:
        return m_Session->RenameEntity(arg0, text) ? 0
                                                   : fail("rename entity failed");
    case ENGINE_HOST_OP_SET_ENTITY_POSITION: {
        if (!floatArg) return fail("set entity position: missing position");
        return m_Session->SetEntityPosition(arg0, floatArg) ? 0
                                                            : fail("set entity position failed");
    }
    case ENGINE_HOST_OP_GET_ENTITY_COUNT:
        return m_Session->GetEntityCount();
    case ENGINE_HOST_OP_GET_ASSET_COUNT:
        return m_Session->GetAssetCount();
    case ENGINE_HOST_OP_GET_ASSET_TYPE:
        return m_Session->GetAssetType(arg0);
    case ENGINE_HOST_OP_IMPORT_ASSET: {
        const int32_t index = m_Session->ImportAsset(text, arg0);
        if (index < 0) return fail("import asset failed");
        return index;
    }
    case ENGINE_HOST_OP_RENAME_ASSET:
        return m_Session->RenameAsset(arg0, text) ? 0 : fail("rename asset failed");
    case ENGINE_HOST_OP_ASSIGN_SCRIPT:
        return m_Session->AssignScript(arg0, arg1) ? 0
                                                   : fail("assign script failed");
    case ENGINE_HOST_OP_SCRIPT_SAVE:
        return m_Session->ScriptSave(arg0, text) ? 0 : fail("script save failed");
    case ENGINE_HOST_OP_SAVE_PROJECT:
        return m_Session->SaveProject() ? 0 : fail("save project failed");
    case ENGINE_HOST_OP_ADD_COMPONENT:
        return m_Session->AddComponent(arg0, text) ? 0
                                                   : fail("add component failed");
    case ENGINE_HOST_OP_REMOVE_COMPONENT:
        return m_Session->RemoveComponent(arg0, text) ? 0
                                                      : fail("remove component failed");
    case ENGINE_HOST_OP_HAS_COMPONENT:
        return m_Session->HasComponent(arg0, text) ? 1 : 0;
    case ENGINE_HOST_OP_SET_COMPONENT_PROPERTY:
        return m_Session->SetComponentProperty(arg0,
                                               text,
                                               strArg2 ? strArg2 : "",
                                               strArg3 ? strArg3 : "")
            ? 0
            : fail("set component property failed");
    case ENGINE_HOST_OP_IS_DIRTY:
        return m_Session->IsDirty() ? 1 : 0;
    default:
        if (result) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_INVALID_STATE,
                               ENGINE_HOST_STAGE_EDIT,
                               "operation is not a dispatch op");
        }
        return -1;
    }
}

int32_t EngineHost::ExecuteFetchOp(int32_t op,
                                   int32_t arg0,
                                   const char* strArg,
                                   const char* strArg2,
                                   const char* strArg3,
                                   uint8_t* outBuffer,
                                   int32_t capacity,
                                   RuntimeResult* result) {
    if (!outBuffer || capacity <= 0) {
        if (result) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_INVALID_STATE,
                               ENGINE_HOST_STAGE_EDIT,
                               "fetch op requires a non-empty output buffer");
        }
        return -1;
    }

    auto copyString = [&](const std::string& value) {
        const int32_t length = static_cast<int32_t>(value.size());
        const int32_t copy = length < capacity - 1 ? length : capacity - 1;
        if (copy > 0) {
            std::memcpy(outBuffer, value.data(), static_cast<size_t>(copy));
        }
        outBuffer[copy] = 0;
        return length;
    };

    auto fail = [&](const char* fallback) {
        const std::string& last = m_Session ? m_Session->GetLastError()
                                            : std::string();
        const std::string diagnostic = last.empty() ? std::string(fallback) : last;
        RecordEditFailure(diagnostic);
        if (result) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_SESSION_FAILED,
                               ENGINE_HOST_STAGE_EDIT,
                               diagnostic.c_str());
        }
        return -1;
    };

    std::string value;
    switch (op) {
    case ENGINE_HOST_OP_GET_PRESENTATION_FRAME:
        return FetchPresentationFrame(outBuffer, capacity, result);
    case ENGINE_HOST_OP_PICK:
        // HRC-3 D3：typed pick 结果**只**能经 EngineHost_Fetch 取回。
        // 同一个 op 值在 Dispatch 上是"提交请求"，在这里是"取回结果" ——
        // 因此结果读取不可能误走普通 Dispatch 执行路径。
        return FetchPickResult(outBuffer, capacity, result);
    case ENGINE_HOST_OP_GET_ENTITY_NAME:
        if (!m_Session->GetEntityName(arg0, &value)) return fail("get entity name failed");
        return copyString(value);
    case ENGINE_HOST_OP_GET_ASSET_PATH:
        if (!m_Session->GetAssetPath(arg0, &value)) return fail("get asset path failed");
        return copyString(value);
    case ENGINE_HOST_OP_GET_ASSET_GUID:
        if (!m_Session->GetAssetGuid(arg0, &value)) return fail("get asset guid failed");
        return copyString(value);
    case ENGINE_HOST_OP_GET_ENTITY_SPRITE:
        if (!m_Session->GetEntitySprite(arg0, &value)) return fail("get entity sprite failed");
        return copyString(value);
    case ENGINE_HOST_OP_GET_ENTITY_SCRIPT:
        if (!m_Session->GetEntityScript(arg0, &value)) return fail("get entity script failed");
        return copyString(value);
    case ENGINE_HOST_OP_GET_COMPONENT_PROPERTY: {
        if (!m_Session->GetComponentProperty(arg0,
                                             strArg ? strArg : "",
                                             strArg2 ? strArg2 : "",
                                             &value)) {
            return fail("get component property failed");
        }
        return copyString(value);
    }
    case ENGINE_HOST_OP_SCRIPT_READ:
        if (!m_Session->ScriptRead(arg0, &value)) return fail("script read failed");
        return copyString(value);
    case ENGINE_HOST_OP_GET_WARNINGS:
        return copyString(m_Session->GetWarnings());
    case ENGINE_HOST_OP_GET_ENTITY_POSITION: {
        if (capacity < static_cast<int32_t>(sizeof(float) * 3)) {
            if (result) {
                FillRejectedResult(result,
                                   ENGINE_HOST_STATUS_INVALID_STATE,
                                   ENGINE_HOST_STAGE_EDIT,
                                   "get entity position needs a 12-byte buffer");
            }
            return -1;
        }
        float position[3] = {0.0f, 0.0f, 0.0f};
        if (!m_Session->GetEntityPosition(arg0, position)) {
            return fail("get entity position failed");
        }
        std::memcpy(outBuffer, position, sizeof(position));
        return static_cast<int32_t>(sizeof(position));
    }
    default:
        if (result) {
            FillRejectedResult(result,
                               ENGINE_HOST_STATUS_INVALID_STATE,
                               ENGINE_HOST_STAGE_EDIT,
                               "operation is not a fetch op");
        }
        return -1;
    }
}

int32_t EngineHost::Dispatch(int32_t op,
                             int32_t arg0,
                             int32_t arg1,
                             const char* strArg,
                             const char* strArg2,
                             const char* strArg3,
                             const float* floatArg,
                             RuntimeResult* result) {
    if (!result) return -1;
      if (!IsKnownOp(op)) {
          FillRejectedResult(result,
                             ENGINE_HOST_STATUS_INVALID_STATE,
                             ENGINE_HOST_STAGE_EDIT,
                             "unknown editor op");
          return -1;
      }
      // HRC-3 P2：Fetch-only op 不得经 Dispatch 进入 pending-edit 队列。
      if (IsFetchOnlyOp(op)) {
          FillRejectedResult(result,
                             ENGINE_HOST_STATUS_INVALID_STATE,
                             ENGINE_HOST_STAGE_EDIT,
                             "operation is fetch-only; use EngineHost_Fetch");
          return -1;
      }
      // Slice E：viewport 尺寸只登记，不进 pending-edit 队列、不做 GL。
      // 位置必须在这里：RequiresGraphicsContext 之后会把 op 路由进 pending-edit。
      if (op == ENGINE_HOST_OP_SET_VIEWPORT_SIZE) {
          return EnqueueViewportResize(floatArg, result);
      }
      if (RequiresGraphicsContext(op)) {
        // HRC-3 D3：pick 走专用入口，capacity=1 的 busy 判定必须发生在入队**之前**。
        if (op == ENGINE_HOST_OP_PICK) {
            const float* xy = floatArg;
            if (!xy) {
                FillRejectedResult(result,
                                   ENGINE_HOST_STATUS_INVALID_STATE,
                                   ENGINE_HOST_STAGE_EDIT,
                                   "pick requires floatArg = [x, y]");
                return -1;
            }
            return EnqueuePickRequest(xy[0], xy[1], result);
        }
        return EnqueuePendingEdit(op, arg0, arg1, result);
    }
    return Submit([this, op, arg0, arg1, strArg, strArg2, strArg3, floatArg,
                   result]() {
        if (!RequireSessionForEdit(result)) return -1;
        const int32_t code = ExecuteSessionOp(op,
                                              arg0,
                                              arg1,
                                              strArg,
                                              strArg2,
                                              strArg3,
                                              floatArg,
                                              result);
        if (result && code >= 0) {
            SetResult(result,
                      ENGINE_HOST_STATUS_SUCCESS,
                      ENGINE_HOST_STAGE_EDIT,
                      0,
                      nullptr,
                      m_State.load());
            result->state_after = code;
        }
        return code;
    }, result, ENGINE_HOST_STAGE_EDIT);
}

// ================================================================
// HRC-3 P2：presentation frame 的原子 fetch。
//
// 由 Fetch() 的 Submit 调用 ⇒ **必定在 runtime 线程**执行。
// frame_id 与像素在**同一次 Submit** 内取得（帧只在同一 runtime 线程的 frame 命令里
// 前进，命令队列串行化 ⇒ 两者不可能来自不同帧）。
// GL 读回在该线程内显式 make-current / uncurrent，帧边界 `graphicsCurrent==0` 不变量保持。
// 静态容量：尺寸编译期固定，不协商、不分配。
// 无新帧：返回 0、状态 = NO_NEW_FRAME，且**一个字节都不写**（不静默重发旧像素）。
// ================================================================
int32_t EngineHost::FetchPickResult(uint8_t* outBuffer, int32_t capacity,
                                    RuntimeResult* result) {
    auto rejectAt = [&](int32_t status, int32_t stage,
                        const char* diagnostic) -> int32_t {
        if (result) {
            FillRejectedResult(result, status, stage, diagnostic);
        }
        return -1;
    };

    if (capacity < ENGINE_HOST_PICK_RESULT_PACKET_BYTES) {
        return rejectAt(ENGINE_HOST_STATUS_INVALID_STATE,
                        ENGINE_HOST_STAGE_EDIT,
                        "output buffer is smaller than the pick packet requires");
    }

    PickResultPacket packet{};
    // D3-H8：state decision 在锁内完成，rejection mutation 一律在**解锁之后**。
    //
    // 原因（D3-H7 定位）：`FillRejectedResult` 会经 `ResetResult` / `SetDiagnostic`
    // 再次 acquire `m_Mutex`。若在持有 `m_Mutex` 时调用它，就是对非递归
    // `std::mutex` 的同线程重入（UB / 自死锁）—— pre-service `Fetch(PICK)`
    // 正是因此在 servicing frame 之前就崩掉，frame 2 永远到不了
    // `DrainPendingEdits`。
    //
    // 不变量：**持有 m_Mutex 的函数不得调用任何可能 acquire 同一 mutex 的函数。**
    // 绝不用 recursive_mutex 掩盖该重叠所有权边界。
    int32_t rejectStatus = 0;
    int32_t rejectStage = 0;
    const char* rejectDiagnostic = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        // D3-H8-2：invalidated 必须**先于** present 判定。
        // InvalidatePickResult 刻意保留 m_PickResultPresent（用它区分
        // 「被 teardown 作废」与「本来就没有」），若先查 present，
        // 已产出但未取走的旧结果会抢先命中，invalidated 分支**永远不可达** ——
        // 表现为 Stop 之后 Fetch 仍成功返回旧结果。
        if (m_PickResultInvalidated) {
            rejectStatus = ENGINE_HOST_STATUS_INVALID_STATE;
            rejectStage = ENGINE_HOST_STAGE_SHUTDOWN;
            rejectDiagnostic = "pick result was invalidated by teardown";
        } else if (m_PickResultPresent && !m_PickResultFetched) {
            packet = m_PickResult;
            m_PickResultFetched = true;   // 消费后不再 outstanding
            // D3-H8-1：消费即释放 capacity，使下一个 request 可以被受理。
            m_PickOutstandingRequestId = 0;
        } else {
            // 请求已受理但**尚未产出结果**：只观察、不消费、不作废 outstanding。
            rejectStatus = ENGINE_HOST_STATUS_INVALID_STATE;
            rejectStage = ENGINE_HOST_STAGE_EDIT;
            rejectDiagnostic = "no outstanding pick result to fetch";
        }
    }

    if (rejectStatus != 0) {
        return rejectAt(rejectStatus, rejectStage, rejectDiagnostic);
    }

    if (outBuffer) {
        std::memcpy(outBuffer, &packet, sizeof(packet));
    }
    if (result) {
        SetResult(result, ENGINE_HOST_STATUS_SUCCESS, ENGINE_HOST_STAGE_EDIT, 0,
                  nullptr, m_State.load());
        result->frame_id = packet.frame_id;
        result->operation_id = packet.request_id;
    }
    return 0;
}

int32_t EngineHost::FetchPresentationFrame(uint8_t* outBuffer, int32_t capacity,
                                           RuntimeResult* result) {
    auto reject = [&](const char* diagnostic) -> int32_t {
        if (result) {
            FillRejectedResult(result, ENGINE_HOST_STATUS_INVALID_STATE,
                               ENGINE_HOST_STAGE_EDIT, diagnostic);
        }
        return -1;
    };

    if (!m_PresentationFbo || !m_GraphicsFactory || !m_RenderContext) {
        return reject("presentation target is not available; open a project first");
    }
    // Slice E：重建失败后 target 无效 ⇒ **不发布任何帧**。
    // 这里返回"没有新帧"（而不是错误），让 UI 安静地保留最后一张 bitmap
    // （E-C6d），而不是每帧抛一次异常。
    if (!m_PresentationTargetValid.load() || !m_PresentationFbo->IsValid()) {
        if (result) {
            SetResult(result, ENGINE_HOST_STATUS_NO_NEW_FRAME, ENGINE_HOST_STAGE_EDIT,
                      0, nullptr, m_State.load());
        }
        return 0;   // 明确"没有新帧"；不写任何字节
    }
    const int32_t width = static_cast<int32_t>(m_PresentationFbo->GetWidth());
    const int32_t height = static_cast<int32_t>(m_PresentationFbo->GetHeight());
    // Slice E：尺寸校验从"必须等于编译期常量"改为**范围**校验。
    // 帧的真实尺寸由 header 携带，容量上限由最大尺寸常量决定。
    if (width <= 0 || height <= 0 ||
        width > ENGINE_HOST_PRESENTATION_MAX_WIDTH ||
        height > ENGINE_HOST_PRESENTATION_MAX_HEIGHT) {
        return reject("presentation target size is outside the supported range");
    }
    // 乘法安全：width/height 已被上界约束（<= 1920/1080），int32 不会溢出。
    const int32_t required =
        ENGINE_HOST_FRAME_PACKET_HEADER_BYTES + width * height * 4;
    if (capacity < required) {
        return reject("output buffer is smaller than the frame packet requires");
    }

    // ── 无新帧判定（与读回在同一 runtime 线程内）──
    const uint64_t currentFrame = m_FrameId.load();
    if (currentFrame == 0 || currentFrame == m_LastFetchedFrameId.load()) {
        if (result) {
            SetResult(result, ENGINE_HOST_STATUS_NO_NEW_FRAME, ENGINE_HOST_STAGE_EDIT,
                      0, nullptr, m_State.load());
        }
        return 0;   // 明确"没有新帧"；不写任何字节
    }

    if (MakeRuntimeContextCurrent() != 0) {
        return reject("failed to make the runtime GL context current for frame readback");
    }
    auto& gl = m_GraphicsFactory->GetGLContext();
    m_PresentationFbo->Bind();
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl.ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                  outBuffer + ENGINE_HOST_FRAME_PACKET_HEADER_BYTES);
    const GLenum glError = gl.GetError();
    m_PresentationFbo->Unbind();
    m_GraphicsFactory->UncurrentPersistentContext();
    if (glError != GL_NO_ERROR) {
        return reject("presentation frame readback produced a GL error");
    }

    // header 与像素同属这一次 fetch
    std::memcpy(outBuffer + 0, &width, sizeof(int32_t));
    std::memcpy(outBuffer + 4, &height, sizeof(int32_t));
    std::memcpy(outBuffer + 8, &currentFrame, sizeof(uint64_t));
    m_LastFetchedFrameId.store(currentFrame);
    return required;
}

int32_t EngineHost::Fetch(int32_t op,
                          int32_t arg0,
                          const char* strArg,
                          const char* strArg2,
                          const char* strArg3,
                          uint8_t* outBuffer,
                          int32_t capacity,
                          RuntimeResult* result) {
    if (!result) return -1;
    if (!IsKnownOp(op) || FetchCapacityForOp(op) == 0) {
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_INVALID_STATE,
                           ENGINE_HOST_STAGE_EDIT,
                           "unknown or non-fetch editor op");
        return -1;
    }
    const int32_t required = FetchCapacityForOp(op);
    if (capacity < required) {
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_INVALID_STATE,
                           ENGINE_HOST_STAGE_EDIT,
                           "output buffer is smaller than the op requires");
        return -1;
    }
    return Submit([this, op, arg0, strArg, strArg2, strArg3,
                   outBuffer, capacity, result]() {
        if (!RequireSessionForEdit(result)) return -1;
          const int32_t code = ExecuteFetchOp(op,
                                              arg0,
                                              strArg,
                                              strArg2,
                                              strArg3,
                                              outBuffer,
                                              capacity,
                                              result);
          // HRC-3 P2：`NO_NEW_FRAME` 是**成功语义的"没有新帧"**，不是失败，
          // 也不是"成功交出一帧"。绝不能被下面的 SUCCESS 覆盖掉。
          if (result && code >= 0 &&
              result->status != ENGINE_HOST_STATUS_NO_NEW_FRAME) {
              SetResult(result,
                        ENGINE_HOST_STATUS_SUCCESS,
                        ENGINE_HOST_STAGE_EDIT,
                        0,
                        nullptr,
                        m_State.load());
          }
          return code;
    }, result, ENGINE_HOST_STAGE_EDIT);
}

int32_t EngineHost::EnqueuePendingEdit(int32_t op,
                                       int32_t arg0,
                                       int32_t arg1,
                                       RuntimeResult* result) {
    return EnqueuePendingEdit(op, arg0, arg1, 0.0f, 0.0f, result);
}

int32_t EngineHost::EnqueuePendingEdit(int32_t op,
                                       int32_t arg0,
                                       int32_t arg1,
                                       float fx,
                                       float fy,
                                       RuntimeResult* result) {
    PendingEdit edit;
    // D3-H8：拒绝决定在锁内记录，mutation 在解锁后进行（同 FetchPickResult）。
    // 这条是**既有**的潜在缺陷（host 不接受命令时的拒绝路径），与 D3 无关，
    // 但属于同一个所有权违规，必须一并修掉。
    bool notAccepting = false;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_AcceptingCommands.load() || m_StopRequested.load()) {
            notAccepting = true;
        } else {
            edit.id = m_NextEditId++;
            edit.op = op;
            edit.arg0 = arg0;
            edit.arg1 = arg1;
            edit.fx = fx;
            edit.fy = fy;
            m_PendingEdits.push_back(edit);
            // D3-H8-1：PICK 一旦**受理**就占据 capacity=1，
            // 此刻 request 仍在 pending 队列里、尚未 dispatch。
            if (op == ENGINE_HOST_OP_PICK) {
                m_PickOutstandingRequestId = static_cast<uint64_t>(edit.id);
                m_PickOutstandingGeneration = m_Generation.load();
            }
        }
    }
    if (notAccepting) {
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_INVALID_STATE,
                           ENGINE_HOST_STAGE_EDIT,
                           "host is not accepting commands");
        return -1;
    }
    ++m_PendingEditAccepted;
    // accepted != executed：执行结果只能由下一次 PumpOneFrame 产生。
    SetResult(result,
              ENGINE_HOST_STATUS_EDIT_ACCEPTED,
              ENGINE_HOST_STAGE_EDIT,
              0,
              "edit accepted; execution is deferred to the next frame",
              m_State.load());
    result->operation_id = static_cast<uint64_t>(edit.id);
    return static_cast<int32_t>(edit.id);
}

bool EngineHost::HasOutstandingPickResult() const noexcept {
    std::lock_guard<std::mutex> lock(m_Mutex);
    // outstanding = 已登记的本帧请求，或已产出但尚未被 Fetch 取走的结果。
    return m_PickFrameRequest.active ||
           (m_PickResultPresent && !m_PickResultFetched);
}

void EngineHost::InvalidatePickResult(const char* diagnostic) noexcept {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_PickFrameRequest = PickFrameRequest{};
    // D3-H8-2 / H8-3：teardown 必须作废**全部** pick 状态 ——
    // 包括「已受理但仍在 pending 队列、尚未 dispatch」的那一个。
    // 此前只看 m_PickResultPresent，因此 Stop() 之后 Fetch 仍能成功返回旧结果。
    if (m_PickOutstandingRequestId != 0 ||
        (m_PickResultPresent && !m_PickResultFetched)) {
        m_PickResultInvalidated = true;
    }
    m_PickOutstandingRequestId = 0;
    m_PickOutstandingGeneration = 0;
    // 刻意**不**清 m_PickResultPresent：Fetch 需要据此区分
    // "被 teardown 作废"（INVALID_STATE + STAGE_SHUTDOWN）与"本来就没有"。
    (void)diagnostic;
}

int32_t EngineHost::EnqueuePickRequest(float x, float y, RuntimeResult* result) {
    // D3-H8：busy 判定在锁内，rejection mutation 在解锁后（同 FetchPickResult）。
    bool busy = false;
    bool staleGeneration = false;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        // D3-H8-3：旧 generation 的 outstanding 视为不存在
        // （Stop/Close 之后新一代不得重新看到旧 request）。
        //
        // 同样地，**已被 teardown 作废**的结果不再是 outstanding：它永远不会被
        // 交付，若仍把它算进 busy，新一代的第一个 request 就会被错误地
        // 判为 PICK_BUSY 而无法发起。
        if (m_PickResultInvalidated ||
            (m_PickOutstandingRequestId != 0 &&
             m_PickOutstandingGeneration != m_Generation.load())) {
            m_PickOutstandingRequestId = 0;
            m_PickOutstandingGeneration = 0;
            m_PickResultPresent = false;
            m_PickResultFetched = false;
        }
        // D3-H8-1：以 request id 判定 outstanding，覆盖
        // Queued / Dispatched / Produced-not-yet-fetched 三态。
        busy = m_PickOutstandingRequestId != 0 ||
               m_PickFrameRequest.active ||
               (m_PickResultPresent && !m_PickResultFetched);
        staleGeneration = m_PickResultInvalidated;
        if (!busy) {
            // 受理新请求：清掉上一轮的 invalidation 标记。
            m_PickResultInvalidated = false;
            m_PickResultPresent = false;
            m_PickResultFetched = false;
        }
    }
    (void)staleGeneration;
    if (busy) {
        // D3 contract：第二个请求必须拿到明确的 busy，**不消费、不替换**
        // outstanding request。否则 Fetch 返回的 A 会被误当成 B 的结果 ——
        // 那正是"靠时序猜测"，D3 要排除的正是这一类。
        FillRejectedResult(result,
                           ENGINE_HOST_STATUS_PICK_BUSY,
                           ENGINE_HOST_STAGE_EDIT,
                           "a pick result is still outstanding; fetch it first");
        return -1;
    }
    return EnqueuePendingEdit(ENGINE_HOST_OP_PICK, 0, 0, x, y, result);
}

/// v1.4 / P-1：命中实体 ID → **当前场景快照**中的索引。
///
/// 语义要点（三条都刻意不做"尽力而为"）：
///   1. 存在性判定走 `Scene::FindByID`（canonical 身份 API），**不**假设 ID 与下标有算术关系；
///      找到之后再定位下标，因此删除/重排都不会把结果错位到邻居实体上。
///   2. 场景未附着、场景为空、ID 不在场景中 —— 一律返回 `ENGINE_HOST_PICK_INDEX_NONE`。
///   3. 索引是**该次调用时**的快照位置；跨帧稳定性由 `frame_id` 承担（调用方负责
///      stale 判定），本函数不缓存、不承诺跨帧一致。
uint32_t EngineHost::ResolveSceneIndexForEntityId(uint32_t entityId) noexcept {
    if (entityId == 0 || !m_Session) return ENGINE_HOST_PICK_INDEX_NONE;
    Engine::Scene* scene = m_Session->GetEditScene();
    if (!scene) return ENGINE_HOST_PICK_INDEX_NONE;
    // 1) 身份判定：实体是否仍存在于场景中。
    if (scene->FindByID(entityId) == nullptr) return ENGINE_HOST_PICK_INDEX_NONE;
    // 2) 定位下标：与 `EditorSession` 的 entityIndex 使用同一个容器与同一顺序。
    const auto& objects = scene->GetObjects();
    for (size_t i = 0; i < objects.size(); ++i) {
        if (objects[i] && objects[i]->GetID() == entityId) {
            return static_cast<uint32_t>(i);
        }
    }
    // FindByID 命中但遍历未命中：只可能来自并发修改（场景在两次读取之间变化）。
    // fail-closed，绝不退化成"就用 0"。
    return ENGINE_HOST_PICK_INDEX_NONE;
}

int32_t EngineHost::EnqueueViewportResize(const float* widthHeight,
                                          RuntimeResult* result) {
    auto reject = [&](const char* diagnostic) -> int32_t {
        if (result) {
            FillRejectedResult(result, ENGINE_HOST_STATUS_INVALID_STATE,
                               ENGINE_HOST_STAGE_EDIT, diagnostic);
        }
        return -1;
    };
    if (!widthHeight) {
        return reject("set viewport size requires floatArg = [width, height]");
    }
    const float rawWidth = widthHeight[0];
    const float rawHeight = widthHeight[1];
    if (!std::isfinite(rawWidth) || !std::isfinite(rawHeight)) {
        return reject("viewport size must be finite");
    }
    // 零/负尺寸显式拒绝：**不** clamp 到 1（那会把"最小化"伪装成 1x1 的正常帧）。
    if (rawWidth <= 0.0f || rawHeight <= 0.0f) {
        return reject("viewport size must be positive; zero/negative is rejected, not clamped");
    }
    if (rawWidth > static_cast<float>(ENGINE_HOST_PRESENTATION_MAX_WIDTH) ||
        rawHeight > static_cast<float>(ENGINE_HOST_PRESENTATION_MAX_HEIGHT)) {
        return reject("viewport size exceeds the maximum presentation size");
    }
    // 四舍五入到整像素：DIP -> 像素只做一次收敛，避免 1px 抖动跨帧累积。
    const long w = std::lround(rawWidth);
    const long h = std::lround(rawHeight);
    if (w <= 0 || h <= 0) {
        return reject("viewport size rounds to zero pixels");
    }
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_PresentationFbo) {
            return reject("presentation target is not available; open a project first");
        }
        // last-wins：直接覆盖，不排队。
        m_PendingViewportSize.active = true;
        m_PendingViewportSize.width = static_cast<uint32_t>(w);
        m_PendingViewportSize.height = static_cast<uint32_t>(h);
    }
    if (result) {
        SetResult(result, ENGINE_HOST_STATUS_SUCCESS, ENGINE_HOST_STAGE_EDIT, 0,
                  nullptr, m_State.load());
    }
    return 0;
}

int32_t EngineHost::ApplyPendingViewportSize(RuntimeResult* result) {
    PendingViewportSize pending;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_PendingViewportSize.active) return 0;
        pending = m_PendingViewportSize;
        m_PendingViewportSize = PendingViewportSize{};
    }
    if (!m_PresentationFbo) return 0;

    const uint32_t oldWidth = m_PresentationFbo->GetWidth();
    const uint32_t oldHeight = m_PresentationFbo->GetHeight();
    if (pending.width == oldWidth && pending.height == oldHeight &&
        m_PresentationTargetValid.load()) {
        return 0;   // 尺寸未变：Resize 本身也会 early-out，这里避免无谓的状态改动。
    }

    // 调用方保证：runtime 线程 + context current（PumpOneFrame 已 make-current）。
    // 故障注入：让"重建失败"这条路径成为**可执行**的（而不是只能靠猜）。
    const bool injectedFailure = TestFailure("EDITOR_BRIDGE_FAIL_VIEWPORT_RESIZE");
    const bool resized = !injectedFailure &&
                         m_PresentationFbo->Resize(pending.width, pending.height);
    const bool applied = resized &&
                         m_PresentationFbo->IsValid() &&
                         m_PresentationFbo->GetWidth() == pending.width &&
                         m_PresentationFbo->GetHeight() == pending.height;

    // Slice E：本帧的 pending pick 坐标属于**旧**尺寸空间 ⇒ 必须由 native 作废。
    // managed 的 `frame_id < presented` 判定无法覆盖它（结果带的是**新** frame_id）。
    // 失败时同样作废：失败后 host 不再发布任何帧，旧坐标没有任何可命中的帧。
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_PickFrameRequest.active) {
            m_PickFrameRequest.invalidatedByResize = true;
        }
    }

    if (!applied) {
        // 重建失败 ⇒ **fail-closed**（E-C6d）：
        //   - 本帧**不发布**任何帧（不推进 frame id ⇒ fetch 报"没有新帧"）；
        //   - host **保持存活**（不 teardown、不 emergency stop）；
        //   - UI 保留最后一张 bitmap，但 pick 一律 NOT_READY；
        //   - 请求被**重新登记**（有界重试）：故障排除后下一帧即可自愈。
        m_ReadbackVerified = false;
        m_PresentationTargetValid = false;
        bool rearmed = false;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_ViewportResizeRetries < kMaxViewportResizeRetries) {
                ++m_ViewportResizeRetries;
                // 只在没有更新的请求时重试（last-wins：新的 Dispatch 覆盖重试）。
                if (!m_PendingViewportSize.active) {
                    m_PendingViewportSize = pending;
                    rearmed = true;
                }
            } else {
                m_ViewportResizeRetries = 0;
            }
        }
        if (result) {
            FillRejectedResult(result, ENGINE_HOST_STATUS_GL_FAILED,
                               ENGINE_HOST_STAGE_FBO,
                               injectedFailure
                                   ? "viewport resize failed (injected); no frame is published"
                                   : "viewport resize failed; the presentation target is invalid");
        }
        (void)rearmed;
        return 1;
    }

    m_ReadbackVerified = false;
    m_PresentationTargetValid = true;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_ViewportResizeRetries = 0;
        // 本帧应用了 resize：DrainPendingEdits 里登记的 pick 必须作废。
        m_InvalidatePickForThisFrame = true;
    }
    return 0;
}

void EngineHost::ServicePickRequestForFrame() noexcept {
    // Slice E：清除"本帧 resize"标记（无论是否有 pending pick）。
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_InvalidatePickForThisFrame = false;
    }
    PickFrameRequest request;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_PickFrameRequest.active) return;
        request = m_PickFrameRequest;
        m_PickFrameRequest = PickFrameRequest{};
    }

    PickResultPacket packet{};
    packet.struct_size = static_cast<uint32_t>(sizeof(PickResultPacket));
    packet.frame_id = request.frameId;
    packet.request_id = request.requestId;
    packet.entity_id = 0;
    // v1.4：非 HIT 一律写 NONE —— "没有索引"必须是显式状态，
    // 不能被读成"索引 0"（0 是合法的第一个实体）。
    packet.entity_index = ENGINE_HOST_PICK_INDEX_NONE;

    if (request.invalidatedByResize) {
        // Slice E：该请求的坐标属于旧尺寸空间 —— 查询无效（不是 no-hit，也不是 fault）。
        packet.status = ENGINE_HOST_PICK_NOT_READY;
    } else if (!m_PresentationFbo ||
               !m_PresentationTargetValid.load() ||
               !m_PresentationFbo->IsValid()) {
        // target 无效（重建失败）⇒ 一律 NOT_READY，直到新有效帧到达（E-C6d）。
        packet.status = ENGINE_HOST_PICK_NOT_READY;
    } else {
        // D3-C3：canonical top-left 视口坐标；越界由 readback 显式拒绝，绝不 clamp。
        const Engine::OpenGLFramebuffer::PickReadback readback =
            m_PresentationFbo->ReadPickPixel(request.x, request.y, 0.0f, 0.0f);
        switch (readback.status) {
        case Engine::OpenGLFramebuffer::PickReadbackStatus::Hit:
            packet.status = ENGINE_HOST_PICK_HIT;
            packet.entity_id = readback.entityId;   // D3-C2：恒 >= 1
            // v1.4 / P-1：把 GameObject::ID 解析为**本帧**的场景索引。
            // 解析失败（实体已不在场景快照中）保持 NONE —— 不猜测、不夹取。
            packet.entity_index = ResolveSceneIndexForEntityId(readback.entityId);
            break;
        case Engine::OpenGLFramebuffer::PickReadbackStatus::NoHit:
            packet.status = ENGINE_HOST_PICK_NO_HIT;  // 正常未命中，不是失败
            break;
        case Engine::OpenGLFramebuffer::PickReadbackStatus::OutsideViewport:
            packet.status = ENGINE_HOST_PICK_OUTSIDE_VIEWPORT;
            break;
        case Engine::OpenGLFramebuffer::PickReadbackStatus::InvalidCoordinate:
            packet.status = ENGINE_HOST_PICK_INVALID_COORDINATE;
            break;
        case Engine::OpenGLFramebuffer::PickReadbackStatus::NotReady:
        default:
            packet.status = ENGINE_HOST_PICK_NOT_READY;
            break;
        }
    }

    std::lock_guard<std::mutex> lock(m_Mutex);
    m_PickResult = packet;
    m_PickResultPresent = true;
    m_PickResultFetched = false;   // 产出即 outstanding，直到被 Fetch 取走
    m_PickResultInvalidated = false;
}

int32_t EngineHost::ExecutePendingEdit(PendingEdit& edit) {
    if (!m_Session) {
        edit.state = kPendingEditFailed;
        edit.diagnostic =
            "editor session is not attached; open a project first";
        return -1;
    }
    if (edit.op == ENGINE_HOST_OP_ASSIGN_SPRITE) {
        if (m_Session->AssignSprite(edit.arg0, edit.arg1)) {
            edit.state = kPendingEditExecuted;
            edit.value = 0;
            return 0;
        }
        const std::string& last = m_Session->GetLastError();
        edit.state = kPendingEditFailed;
        edit.diagnostic = last.empty() ? "assign sprite failed" : last;
        return -1;
    }
    if (edit.op == ENGINE_HOST_OP_PICK) {
        // D3-U2：pending-edit drain 发生在 production render **之前**
        // （PumpOneFrame: DrainPendingEdits -> render）。因此这里只登记
        // "本帧要 pick"，真正的 readback 必须在同一次 PumpOneFrame 的 render
        // 之后由 ServicePickRequestForFrame() 完成 —— 这样 result 才真正
        // 绑定到渲染了 ID 的那一帧（D3-C1/D3-C4）。
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_PickFrameRequest.active = true;
        m_PickFrameRequest.x = edit.fx;
        m_PickFrameRequest.y = edit.fy;
        m_PickFrameRequest.requestId = static_cast<uint64_t>(edit.id);
        m_PickFrameRequest.frameId = edit.frameId;
        // Slice E：本帧应用了 resize ⇒ 该请求坐标属于旧尺寸空间，必须作废。
        if (m_InvalidatePickForThisFrame) {
            m_PickFrameRequest.invalidatedByResize = true;
        }
        edit.state = kPendingEditExecuted;
        edit.value = 0;
        return 0;
    }
    edit.state = kPendingEditFailed;
    edit.diagnostic = "pending edit op is not a graphics-context op";
    return -1;
}

void EngineHost::DrainPendingEdits() noexcept {
    std::deque<PendingEdit> batch;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_PendingEdits.empty()) return;
        batch.swap(m_PendingEdits);
    }

    const bool contextCurrent =
        m_GraphicsFactory && m_GraphicsFactory->IsPersistentContextCurrent();

    for (PendingEdit& edit : batch) {
        // 记录“本帧”编号：与 PumpOneFrame 返回的 result.frame_id 对齐。
        edit.frameId = m_FrameId.load() + 1;
        edit.contextCurrent = contextCurrent ? 1 : 0;
        try {
            ExecutePendingEdit(edit);
        } catch (const std::exception& exception) {
            edit.state = kPendingEditFailed;
            edit.diagnostic = exception.what();
        } catch (...) {
            edit.state = kPendingEditFailed;
            edit.diagnostic = "pending edit threw an unknown exception";
        }

        if (edit.state == kPendingEditExecuted) {
            ++m_PendingEditExecuted;
        } else {
            ++m_PendingEditFailed;
            // queued edit 失败不得升级为 lifecycle fault，但必须可观测。
            RecordEditFailure(edit.diagnostic);
        }
        m_PendingEditLastContextCurrent = edit.contextCurrent;
        m_PendingEditLastFrameId = edit.frameId;

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_CompletedEdits[edit.id] = edit;
        }
    }
}

void EngineHost::AbandonPendingEdits(const char* diagnostic) noexcept {
    // HRC-3 D3：任何 teardown 都要作废 pick —— 必须放在下面的 early-return **之前**，
    // 否则"队列恰好为空"就会漏掉作废，让旧 request 在 reopen 后重新出现。
    InvalidatePickResult(diagnostic);
    std::deque<PendingEdit> batch;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_PendingEdits.empty()) return;
        batch.swap(m_PendingEdits);
    }
    const char* reason = diagnostic ? diagnostic : "runtime stopped before edit executed";
    for (PendingEdit& edit : batch) {
        edit.state = kPendingEditFailed;
        edit.diagnostic = reason;
        ++m_PendingEditFailed;
        RecordEditFailure(edit.diagnostic);
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_CompletedEdits[edit.id] = edit;
        }
    }
}

int32_t EngineHost::PollPendingEdit(int64_t editId, RuntimeResult* result) {
    if (!result || editId <= 0) return -1;
    PendingEdit edit;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto it = m_CompletedEdits.find(editId);
        if (it == m_CompletedEdits.end()) {
            for (const PendingEdit& pending : m_PendingEdits) {
                if (pending.id == editId) return 1;
            }
            return -1;
        }
        edit = it->second;
    }

    const bool ok = edit.state == kPendingEditExecuted;
    SetResult(result,
              ok ? ENGINE_HOST_STATUS_SUCCESS : ENGINE_HOST_STATUS_SESSION_FAILED,
              ENGINE_HOST_STAGE_EDIT,
              0,
              ok ? nullptr : edit.diagnostic.c_str(),
              m_State.load());
    result->operation_id = static_cast<uint64_t>(edit.id);
    result->frame_id = edit.frameId;
    return 0;
}

int32_t EngineHost::GetPendingEditProofState(int32_t* accepted,
                                             int32_t* executed,
                                             int32_t* failed,
                                             int32_t* lastExecutedWithContextCurrent,
                                             uint64_t* lastExecutedFrameId) const {
    if (!accepted || !executed || !failed ||
        !lastExecutedWithContextCurrent || !lastExecutedFrameId) return -1;
    *accepted = m_PendingEditAccepted.load();
    *executed = m_PendingEditExecuted.load();
    *failed = m_PendingEditFailed.load();
    *lastExecutedWithContextCurrent = m_PendingEditLastContextCurrent.load();
    *lastExecutedFrameId = m_PendingEditLastFrameId.load();
    return 0;
}

}
