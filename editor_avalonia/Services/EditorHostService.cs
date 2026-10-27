using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Avalonia.Threading;
using AvaloniaEditor.Native;

namespace AvaloniaEditor.Services;

/// <summary>Host backend 模式。进程启动时选定一次，运行期不可切换、无回退。</summary>
public enum HostBackend
{
    /// <summary>A7 production 路径：唯一 native owner 为 EngineHost。</summary>
    EngineHost,
    /// <summary>仅供 HRC-1 gate 注入链：legacy EditorSession。</summary>
    LegacySession,
}

/// <summary>A7-C03 runtime 三态（A2/F2-05 增加 Faulted：runtime 已不可用但尚未终态释放）。</summary>
public enum HostRuntimeState
{
    /// <summary>Host 存活但无可用 runtime（未 bootstrap / project 未打开）。</summary>
    Starting,
    /// <summary>runtime 可用。</summary>
    Ready,
    /// <summary>
    /// A2/F2-05：runtime 已 fault（pump / play 命令失败并触发 emergency teardown）。
    /// pump 已停止、runtime 不可用，但句柄仍归本 Host 所有，必须走 <c>Dispose</c> 收尾。
    /// </summary>
    Faulted,
    /// <summary>Dispose 进行中或已释放。</summary>
    Stopping,
}

/// <summary>Starting 态下请求 runtime 依赖操作。</summary>
public sealed class HostRuntimeNotReadyException : InvalidOperationException
{
    public HostRuntimeNotReadyException(string message) : base(message) { }
}

/// <summary>Stopping/Disposed 态下请求 runtime 依赖操作。</summary>
public sealed class HostRuntimeUnavailableException : ObjectDisposedException
{
    public HostRuntimeUnavailableException(string message) : base(nameof(EditorHostService), message) { }
}

/// <summary>
/// A2/F2-05：native typed failure（<c>RuntimeResult.status/stage</c>）的结构化搬运。
/// 这是唯一允许从 Host 生命周期操作抛出的"host 侧失败"类型：ViewModel 负责把它转成
/// 可观测 UI 状态（Console / 状态栏 / ScriptError），View 边界再兜一层，
/// 因此 native 诊断不会再以反射派发异常的形式逃出 XAML handler。
/// </summary>
public sealed class HostOperationException : InvalidOperationException
{
    /// <summary>失败的操作名（bootstrap / open project / play / reload / stop / pump frame）。</summary>
    public string Operation { get; }

    /// <summary>native <c>RuntimeResult.status</c>（原样保留，不做重映射）。</summary>
    public int Status { get; }

    /// <summary>native <c>RuntimeResult.stage</c>（原样保留）。</summary>
    public int Stage { get; }

    /// <summary>native diagnostic 文本；bootstrap 失败时句柄已被销毁，只能给出结构化原因（见构造函数说明）。</summary>
    public string Diagnostic { get; }

    public HostOperationException(string operation, int status, int stage, string diagnostic)
        : base($"{operation} failed: {StatusText(status)} / {StageText(stage)}"
               + (string.IsNullOrEmpty(diagnostic) ? string.Empty : $" — {diagnostic}"))
    {
        Operation = operation;
        Status = status;
        Stage = stage;
        Diagnostic = diagnostic ?? string.Empty;
    }

    public static string StatusText(int status) => status switch
    {
        EditorBridgeApi.EngineHostStatusSuccess => "Success",
        EditorBridgeApi.EngineHostStatusSkippedNotPlaying => "SkippedNotPlaying",
        EditorBridgeApi.EngineHostStatusInvalidState => "InvalidState",
        EditorBridgeApi.EngineHostStatusWrongThread => "WrongThread",
        EditorBridgeApi.EngineHostStatusInitFailed => "InitFailed",
        EditorBridgeApi.EngineHostStatusGlFailed => "GLFailed",
        EditorBridgeApi.EngineHostStatusSessionFailed => "SessionFailed",
        EditorBridgeApi.EngineHostStatusTickFault => "TickFault",
        EditorBridgeApi.EngineHostStatusShutdownComplete => "ShutdownComplete",
        EditorBridgeApi.EngineHostStatusShutdownFailed => "ShutdownFailed",
        EditorBridgeApi.EngineHostStatusEditAccepted => "EditAccepted",
        _ => $"Status({status})",
    };

    public static string StageText(int stage) => stage switch
    {
        EditorBridgeApi.EngineHostStageBootstrap => "Bootstrap",
        EditorBridgeApi.EngineHostStagePlatform => "Platform",
        EditorBridgeApi.EngineHostStageGlContext => "GLContext",
        EditorBridgeApi.EngineHostStageGlad => "Glad",
        EditorBridgeApi.EngineHostStageEngineSubsystem => "EngineSubsystem",
        EditorBridgeApi.EngineHostStageFbo => "FBO",
        EditorBridgeApi.EngineHostStageSession => "Session",
        EditorBridgeApi.EngineHostStageOpenProject => "OpenProject",
        EditorBridgeApi.EngineHostStagePlay => "Play",
        EditorBridgeApi.EngineHostStageTick => "Tick",
        EditorBridgeApi.EngineHostStageShutdown => "Shutdown",
        EditorBridgeApi.EngineHostStageEdit => "Edit",
        _ => $"Stage({stage})",
    };
}

/// <summary>当前 backend 不提供该能力（如 EngineHost 模式下的 Viewport/Gizmo）。</summary>
public sealed class HostCapabilityNotAvailableException : NotSupportedException
{
    public HostCapabilityNotAvailableException(string message) : base(message) { }
}

/// <summary>
/// A7-2 editor 操作面。生产路径只由 <see cref="EngineHostChannel"/> 实现，
/// legacy Session ABI 只在 <see cref="LegacySessionChannel"/> 中出现，
/// 因此 production 不存在 legacy fallback。
/// </summary>
internal interface IEditorOps
{
    bool OpenProject(string manifestPath, string scenePath);
    bool SaveProject();
    (int entities, int assets) GetCounts();
    int CreateEntity(string? name);
    string? GetEntityName(int index);
    (float x, float y, float z) GetEntityPosition(int index);
    bool SetEntityPosition(int index, float x, float y, float z);
    string? GetAssetPath(int index);
    int GetAssetType(int index);
    string GetAssetGuid(int index);
    int ImportAsset(string path, int type);
    bool RenameAsset(int assetIndex, string newName);
    bool DeleteEntity(int index);
    bool RenameEntity(int index, string newName);
    string GetEntitySprite(int index);
    bool AssignSprite(int assetIndex, int entityIndex);
    string GetEntityScript(int index);

    bool AssignScript(int assetIndex, int entityIndex);
    string ScriptRead(int assetIndex);
    bool ScriptSave(int assetIndex, string text);
    bool Play(int assetIndex);
    bool Reload();
    void Stop();
    void RuntimeTick(float dt);
    string GetRuntimeError();
    bool IsDirty();
    string GetLastError();
    string GetWarnings();
    bool AddComponent(int entityIndex, string typeName);
    bool RemoveComponent(int entityIndex, string typeName);
    bool HasComponent(int entityIndex, string typeName);
    string GetComponentProperty(int entityIndex, string typeName, string propName);
    bool SetComponentProperty(int entityIndex, string typeName, string propName, string value);
}

/// <summary>
/// A7 唯一 managed Host（Program.Main 构造一次，App/MainWindow/MainViewModel 共享同一引用）。
/// </summary>
/// <summary>
/// 纯诊断：native runtime thread 最近发布的一帧结果（Stage A/A1 生产路径 evidence）。
/// 读取无副作用；Host 未 bootstrap 时返回 <c>null</c>，不抛异常。
/// </summary>
public readonly record struct HostFrameProof(
    ulong OperationId, ulong FrameId, int Status, int Stage,
    float RawDtSeconds, float EffectiveDtSeconds);

public sealed class EditorHostService : IDisposable
{
    internal static void VerifyEngineHostAbiVersion(int reportedVersion)
        => EngineHostChannel.VerifyEngineHostAbiVersion(reportedVersion);
    /// <summary>
    /// pump 时间步长上限。UI 线程被阻塞（调试器、模态框、断点）后，恢复的第一帧
    /// 不得把整段停顿灌进模拟；超出部分一律丢弃。
    /// </summary>
    public const double MaxPumpDeltaSeconds = 0.1;

    /// <summary>把原始帧间隔收敛到 [0, <see cref="MaxPumpDeltaSeconds"/>]，并拒绝 NaN/Inf。</summary>
    internal static double ClampPumpDeltaSeconds(double raw)
    {
        if (!double.IsFinite(raw) || raw < 0.0) return 0.0;
        return raw > MaxPumpDeltaSeconds ? MaxPumpDeltaSeconds : raw;
    }

    private readonly EngineHostChannel? _engineHost;
    private readonly LegacySessionChannel? _legacy;
    private bool _disposed;

    public EditorHostService(HostBackend backend = HostBackend.EngineHost)
    {
        Backend = backend;
        if (backend == HostBackend.EngineHost)
        {
            _engineHost = new EngineHostChannel
            {
                NativeEventHook = RaiseNativeEvent,
                PendingEditCompletedHook = RaisePendingEditCompleted,
                RuntimeFaultedHook = RaiseRuntimeFault,
                FrameAdvancedHook = RaiseFrameAdvanced,
            };
        }
        else
        {
            _legacy = new LegacySessionChannel
            {
                NativeEventHook = RaiseNativeEvent,
            };
        }
    }

    public HostBackend Backend { get; }
    public bool IsEngineHost => _engineHost is not null;
    public bool IsDisposed => _disposed;
    public bool HasSession => IsEngineHost ? _engineHost!.IsCreated : _legacy!.IsCreated;
    public bool IsOpen { get; private set; }
    public HostRuntimeState State { get; private set; } = HostRuntimeState.Starting;

    /// <summary>Viewport/Gizmo/Picking 属 A7 范围外，EngineHost 模式不提供。</summary>
    public bool SupportsViewport => !IsEngineHost;

    /// <summary>被 generation guard 丢弃的过期事件数（G2-06 evidence）。</summary>
    public int DroppedLateEventCount => _engineHost?.DroppedLateEventCount ?? 0;
    internal int FrameBufferAllocations => _engineHost?.FrameBufferAllocations ?? 0;
    internal byte[]? FrameFetchBuffer => _engineHost?.FrameFetchBuffer;

    /// <summary>通过 generation guard 并投递给 UI 的事件数（G2-06 evidence）。</summary>
    public int AcceptedEventCount => _engineHost?.AcceptedEventCount ?? 0;

    /// <summary>pending edit 统计（G2-07 evidence）。</summary>
    public (int accepted, int executed, int failed) PendingEditCounts
        => _engineHost?.PendingEditCounts ?? (0, 0, 0);

    /// <summary>最近一次 pending edit 是否在 context current 的帧内执行（G2-07 evidence）。</summary>
    public bool LastPendingEditExecutedWithContextCurrent
        => _engineHost?.LastPendingEditExecutedWithContextCurrent ?? false;

    /// <summary>engine host / legacy session 的当前 managed generation（G2-06 evidence）。</summary>
    public long Generation => _engineHost?.Generation ?? _legacy!.Generation;

    /// <summary>
    /// Stage A/A1：最近一帧的 typed result。只读诊断，不受三态 gate 约束
    /// （evidence probe 不得成为 runtime 依赖操作）；未 bootstrap 时为 <c>null</c>。
    /// </summary>
    public HostFrameProof? LastFrameProof => _engineHost?.ReadLastFrameProof();

    /// <summary>
    /// 累计 pump 调用次数（service 定时泵 + 显式 <see cref="RuntimeTick"/>）。
    /// 包含失败调用：成功帧数 = PumpInvocations - PumpFailures。
    /// </summary>
    public long PumpInvocations => _engineHost?.PumpInvocations ?? 0;

    /// <summary>累计失败的 pump 调用次数（native 未推进帧）。</summary>
    public long PumpFailures => _engineHost?.PumpFailures ?? 0;

    /// <summary>最近一次 pump 使用的（已 clamp 的）时间步长，只读诊断。</summary>
    public double LastPumpDeltaSeconds => _engineHost?.LastPumpDeltaSeconds ?? 0.0;

    /// <summary>Stage A/A1：native runtime 的累计帧数 / RuntimeTick 次数（未 bootstrap 时为 0）。</summary>
    public (ulong frames, ulong ticks) RuntimeCounts
    {
        get
        {
            if (_engineHost is null) return (0, 0);
            return _engineHost.ReadRuntimeCounts();
        }
    }

    /// <summary>
    /// HRC-3 P2：原子取整帧的 managed 入口（转发到 EngineHost 通道，**不缓存**、不生成 frame id）。
    /// 这一层只是镜像/解码 native packet；UI 呈现（WriteableBitmap/ViewportImage）属于 P2 UI proof 层。
    /// </summary>
    public FrameFetchResult TryFetchPresentationFrame()
    {
        ThrowIfDisposed();
        if (!IsEngineHost)
            throw new HostCapabilityNotAvailableException(
                "presentation frame fetch is an EngineHost capability; the legacy backend has no presentation target");
        return _engineHost!.TryFetchPresentationFrame();
    }

    /// <summary>
    /// HRC-3 D3：提交 pick 请求（Dispatch(PICK)）。结果需在**既有** pump 之后
    /// 由 <see cref="TryFetchPickResult"/> 取回 —— request 与 result 天然跨帧。
    /// 本层不新建 pump、不调 GL。
    /// </summary>
    public PickRequestResult RequestPick(float x, float y)
    {
        ThrowIfDisposed();
        if (!IsEngineHost)
            throw new HostCapabilityNotAvailableException(
                "pick request is an EngineHost capability; the legacy backend has no pick pass");
        return _engineHost!.RequestPick(x, y);
    }

    /// <summary>
    /// HRC-3 D3：取回 pick 结果（Fetch(PICK_RESULT)）。不缓存 latest result；
    /// 被 teardown 作废时不产生 packet。
    /// </summary>
    public PickFetchResult TryFetchPickResult()
    {
        ThrowIfDisposed();
        if (!IsEngineHost)
            throw new HostCapabilityNotAvailableException(
                "pick result fetch is an EngineHost capability; the legacy backend has no pick pass");
        return _engineHost!.TryFetchPickResult();
    }

    /// <summary>
    /// A2/F2-07 evidence：native GLFW 生命周期计数（users / init / terminate）。
    /// 只读全局诊断，不需要句柄；"init 次数不随 CloseProject 增长"就是"CloseProject 不重建 GL"的直接证据。
    /// </summary>
    public (int users, int initCount, int terminateCount) GlfwLifetime
    {
        get
        {
            if (!IsEngineHost) return (0, 0, 0);
            return EditorBridgeApi.EngineHost_GetGlfwLifetimeState(out int users, out int init, out int terminate) == 0
                ? (users, init, terminate)
                : (0, 0, 0);
        }
    }

    /// <summary>G2-06 gate hook：以指定 generation 走一遍真实 marshal 路径。</summary>
    internal void PostEventWithGeneration(long capturedGeneration, int eventType, string payload)
        => _engineHost?.MarshalEvent(capturedGeneration, eventType, payload);

    public event Action<int, string>? NativeEvent;
    /// <summary>pending edit 在帧内完成后于 UI 线程触发（accepted ≠ executed 的可观测出口）。</summary>
    public event Action<bool, string>? PendingEditCompleted;

    /// <summary>
    /// 唯一 runtime pump 完成一帧后于 UI 线程触发。UI 只消费该帧做呈现，
    /// **不得**在 handler 里再调用 <see cref="RuntimeTick"/>（否则重新引入第二个驱动）。
    /// 没有新帧时 handler 内的 fetch 会返回 no-new-frame，presenter 不触碰控件。
    /// </summary>
    public event Action? FrameAdvanced;

    /// <summary>
    /// A2/F2-05：pump 检测到 native typed failure 时在 UI 线程触发一次。
    /// 语义 = "runtime 已不可用"：pump 已停止、State 已转 Faulted，UI 必须显式呈现，不得静默继续 tick。
    /// </summary>
    public event Action<HostOperationException>? RuntimeFaulted;

    /// <summary>最近一次 pump 侧 native failure（无故障时为 null），纯诊断、只读。</summary>
    public HostOperationException? LastRuntimeFailure { get; private set; }

    // ── 生命周期 ────────────────────────────────────────────

    /// <summary>
    /// F2-02 / F2-04：bootstrap 的**唯一**命名触发点。
    /// 幂等：已有 runtime 时直接返回。失败抛 <see cref="HostOperationException"/>（带 native status/stage），
    /// 且失败后 host 保持 Starting、无句柄、无 pump（F2-05 / F2-10）。
    /// </summary>
    public void EnsureRuntime()
    {
        ThrowIfDisposed();
        if (State == HostRuntimeState.Stopping)
            throw new HostRuntimeUnavailableException("host runtime is unavailable");
        if (State == HostRuntimeState.Faulted)
            throw new HostRuntimeUnavailableException(
                "host runtime is faulted; terminal Dispose is required before any new runtime");
        if (HasSession) return;
        if (IsEngineHost) _engineHost!.Create();
        else _legacy!.Create();
    }

    /// <summary>Starting → Ready 的唯一入口；Stopping/Faulted 态明确拒绝。</summary>
    public bool OpenProject(string manifestPath, string scenePath)
    {
        ThrowIfDisposed();
        if (State == HostRuntimeState.Stopping)
            throw new HostRuntimeUnavailableException("host runtime is unavailable");
        if (State == HostRuntimeState.Faulted)
            throw new HostRuntimeUnavailableException(
                "host runtime is faulted; terminal Dispose is required before reopening a project");
        // F2-04：OpenProject 保留"隐含 bootstrap"的调用时机，但实现收敛到唯一命名操作。
        EnsureRuntime();
        var opened = Ops().OpenProject(manifestPath, scenePath);
        IsOpen = opened;
        if (opened)
        {
            State = HostRuntimeState.Ready;
            if (IsEngineHost) _engineHost!.BeginRuntime();
        }
        return opened;
    }

    public void CloseProject()
    {
        if (_disposed) return;
        if (IsEngineHost) _engineHost!.CloseProject(faulted: State == HostRuntimeState.Faulted);
        else _legacy!.DestroySession();
        IsOpen = false;
        // F2-07：CloseProject 把可用性退回 Starting，但**不**销毁 EngineHost runtime；
        // 若 runtime 已 fault，则保持 Faulted（不得让 UI 误以为可以重新使用）。
        if (State != HostRuntimeState.Faulted) State = HostRuntimeState.Starting;
    }

    public void Dispose()
    {
        if (_disposed) return;
        State = HostRuntimeState.Stopping;
        try
        {
            if (IsEngineHost) _engineHost!.Dispose();
            else _legacy!.DestroySession();
        }
        finally
        {
            _disposed = true;
            IsOpen = false;
            NativeEvent = null;
            PendingEditCompleted = null;
            FrameAdvanced = null;
            RuntimeFaulted = null;
        }
    }

    // ── 编辑操作（28 SESSION 调用）──────────────────────────

    public bool SaveProject() => Guarded(ops => ops.SaveProject());
    public (int entities, int assets) GetCounts()
    {
        ThrowIfDisposed();
        RequireRuntimeAvailable();
        return Ops().GetCounts();
    }
    public int CreateEntity(string? name)
    {
        ThrowIfDisposed();
        RequireRuntimeAvailable();
        return Ops().CreateEntity(name);
    }
    public string? GetEntityName(int index) => Guarded(ops => ops.GetEntityName(index));
    public (float x, float y, float z) GetEntityPosition(int index) => Guarded(ops => ops.GetEntityPosition(index));
    public bool SetEntityPosition(int index, float x, float y, float z) => Guarded(ops => ops.SetEntityPosition(index, x, y, z));
    public string? GetAssetPath(int index) => Guarded(ops => ops.GetAssetPath(index));
    public int GetAssetType(int index) => Guarded(ops => ops.GetAssetType(index));
    public string GetAssetGuid(int index) => Guarded(ops => ops.GetAssetGuid(index));
    public int ImportAsset(string path, int type) => Guarded(ops => ops.ImportAsset(path, type));
    public bool RenameAsset(int assetIndex, string newName) => Guarded(ops => ops.RenameAsset(assetIndex, newName));
    public bool DeleteEntity(int index) => Guarded(ops => ops.DeleteEntity(index));
    public bool RenameEntity(int index, string newName) => Guarded(ops => ops.RenameEntity(index, newName));
    public string GetEntitySprite(int index) => Guarded(ops => ops.GetEntitySprite(index));
    public string GetEntityScript(int index) => Guarded(ops => ops.GetEntityScript(index));

    public bool AssignScript(int assetIndex, int entityIndex) => Guarded(ops => ops.AssignScript(assetIndex, entityIndex));
    public string ScriptRead(int assetIndex) => Guarded(ops => ops.ScriptRead(assetIndex));
    public bool ScriptSave(int assetIndex, string text) => Guarded(ops => ops.ScriptSave(assetIndex, text));
    public bool Play(int assetIndex) => Guarded(ops => ops.Play(assetIndex));
    public bool Reload() => Guarded(ops => ops.Reload());
    public void Stop() => Guarded(ops => ops.Stop());
    public void RuntimeTick(float dt) => Guarded(ops => ops.RuntimeTick(dt));
    public string GetRuntimeError() => Guarded(ops => ops.GetRuntimeError());
    public bool IsDirty() => Guarded(ops => ops.IsDirty());
    public string GetLastError() => Guarded(ops => ops.GetLastError());
    public string GetWarnings() => Guarded(ops => ops.GetWarnings());
    public bool AddComponent(int entityIndex, string typeName) => Guarded(ops => ops.AddComponent(entityIndex, typeName));
    public bool RemoveComponent(int entityIndex, string typeName) => Guarded(ops => ops.RemoveComponent(entityIndex, typeName));
    public bool HasComponent(int entityIndex, string typeName) => Guarded(ops => ops.HasComponent(entityIndex, typeName));
    public string GetComponentProperty(int entityIndex, string typeName, string propName)
        => Guarded(ops => ops.GetComponentProperty(entityIndex, typeName, propName));
    public bool SetComponentProperty(int entityIndex, string typeName, string propName, string value)
        => Guarded(ops => ops.SetComponentProperty(entityIndex, typeName, propName, value));

    /// <summary>
    /// GL 依赖 edit。EngineHost 模式返回 <c>accepted</c>（已进入 pending-edit queue），
    /// 不代表已执行；执行结果经 <see cref="PendingEditCompleted"/> 在下一帧后给出。
    /// </summary>
    public bool AssignSprite(int assetIndex, int entityIndex)
        => Guarded(ops => ops.AssignSprite(assetIndex, entityIndex));

    /// <summary>A7-0 DEFERRED：运行态持久化探针仅为 gate 用途，未迁移到 Host seam。</summary>
    public int GetRuntimePersistInt(string key, int defaultVal)
    {
        ThrowIfDisposed();
        if (_legacy is null)
            throw new HostCapabilityNotAvailableException(
                "RuntimePersistInt is DEFERRED in A7-0 and has no EngineHost equivalent");
        return _legacy.RuntimePersistInt(key, defaultVal);
    }

    // ── Viewport / Gizmo / Picking（A7 范围外，EngineHost 模式明确拒绝）────

    public uint GetViewportTextureHandle() => LegacyOnly(ops => ops.GetViewportTextureHandle());
    public (int width, int height) GetViewportSize() => LegacyOnly(ops => ops.GetViewportSize());
    /// <summary>
    /// Slice E：设置 presentation viewport 尺寸。
    /// EngineHost 走 append-only op `SET_VIEWPORT_SIZE`（只登记，下一帧生效）；
    /// legacy 走既有 `EditorSession_SetViewportSize`。
    /// 返回 <c>false</c> 表示被拒绝（零/负/超限/无 presentation target）——
    /// 拒绝**不会**改变当前 FBO 尺寸。
    /// </summary>
    public bool SetViewportSize(int width, int height)
    {
        ThrowIfDisposed();
        if (IsEngineHost) return _engineHost!.SetViewportSize(width, height);
        return LegacyOnly(ops => { ops.SetViewportSize(width, height); return true; });
    }
    public void ViewportTick(float dt) => LegacyOnly(ops => ops.ViewportTick(dt));
    public void SetViewportCameraPosition(float x, float y, float z) => LegacyOnly(ops => ops.SetViewportCameraPosition(x, y, z));
    public (float x, float y, float z) GetViewportCameraPosition() => LegacyOnly(ops => ops.GetViewportCameraPosition());
    public void SetViewportCameraProjection(int type) => LegacyOnly(ops => ops.SetViewportCameraProjection(type));
    public void SetViewportCameraZoom(float zoom) => LegacyOnly(ops => ops.SetViewportCameraZoom(zoom));
    public void ViewportMouseMove(float x, float y) => LegacyOnly(ops => ops.ViewportMouseMove(x, y));
    public void ViewportMouseButton(int button, bool pressed) => LegacyOnly(ops => ops.ViewportMouseButton(button, pressed));
    public void ViewportScroll(float delta) => LegacyOnly(ops => ops.ViewportScroll(delta));
    public void ViewportKeyDown(int key) => LegacyOnly(ops => ops.ViewportKeyDown(key));
    public void ViewportKeyUp(int key) => LegacyOnly(ops => ops.ViewportKeyUp(key));
    public int ViewportPick(float ndcX, float ndcY) => LegacyOnly(ops => ops.ViewportPick(ndcX, ndcY));
    public void SetGizmoMode(int mode) => LegacyOnly(ops => ops.SetGizmoMode(mode));
    public void SetGizmoSpace(int space) => LegacyOnly(ops => ops.SetGizmoSpace(space));
    public void SetGizmoSnap(bool enabled, float value) => LegacyOnly(ops => ops.SetGizmoSnap(enabled, value));
    public int GetGizmoMode() => LegacyOnly(ops => ops.GetGizmoMode());
    public int GetGizmoSpace() => LegacyOnly(ops => ops.GetGizmoSpace());
    public bool GetGizmoSnapEnabled() => LegacyOnly(ops => ops.GetGizmoSnapEnabled());
    public float GetGizmoSnapValue() => LegacyOnly(ops => ops.GetGizmoSnapValue());
    public void SetViewportGridVisible(bool visible) => LegacyOnly(ops => ops.SetViewportGridVisible(visible));
    public bool GetViewportGridVisible() => LegacyOnly(ops => ops.GetViewportGridVisible());
    public (int width, int height, byte[]? pixels) GetViewportPixels() => LegacyOnly(ops => ops.GetViewportPixels());
    public void SetCameraExternalInput(bool enabled) => LegacyOnly(ops => ops.SetCameraExternalInput(enabled));
    public bool GetCameraExternalInput() => LegacyOnly(ops => ops.GetCameraExternalInput());

    // ── 内部 ────────────────────────────────────────────────

    private IEditorOps Ops() => _engineHost ?? (IEditorOps)_legacy!;

    private T Guarded<T>(Func<IEditorOps, T> call)
    {
        ThrowIfDisposed();
        RequireRuntimeAvailable();
        return call(Ops());
    }

    private void Guarded(Action<IEditorOps> call)
    {
        ThrowIfDisposed();
        RequireRuntimeAvailable();
        call(Ops());
    }

    /// <summary>A7-C03 三态 gate：Starting / Faulted / Stopping 明确拒绝，绝不静默继续或回退。</summary>
    private void RequireRuntimeAvailable()
    {
        if (_disposed || State == HostRuntimeState.Stopping)
            throw new HostRuntimeUnavailableException("host runtime is unavailable");
        if (State == HostRuntimeState.Faulted)
            throw new HostRuntimeUnavailableException(
                $"host runtime is faulted ({HostOperationException.StatusText(LastRuntimeFailure?.Status ?? 0)})");
        if (State != HostRuntimeState.Ready)
            throw new HostRuntimeNotReadyException(
                $"host runtime is not ready (state={State}); open a project first");
    }

    private T LegacyOnly<T>(Func<LegacySessionChannel, T> call)
    {
        ThrowIfDisposed();
        if (_legacy is null)
            throw new HostCapabilityNotAvailableException(
                "Viewport/Gizmo/Picking is out of A7 scope and has no EngineHost equivalent");
        return call(_legacy);
    }

    private void LegacyOnly(Action<LegacySessionChannel> call)
    {
        ThrowIfDisposed();
        if (_legacy is null)
            throw new HostCapabilityNotAvailableException(
                "Viewport/Gizmo/Picking is out of A7 scope and has no EngineHost equivalent");
        call(_legacy);
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(EditorHostService));
    }

    internal void RaiseNativeEvent(int eventType, string payload)
        => NativeEvent?.Invoke(eventType, payload);

    internal void RaiseFrameAdvanced()
        => FrameAdvanced?.Invoke();

    internal void RaisePendingEditCompleted(bool executed, string diagnostic)
        => PendingEditCompleted?.Invoke(executed, diagnostic);

    /// <summary>A2/F2-05：pump 侧 typed failure → 置 Faulted（停泵已由 channel 完成）→ 通知 UI 一次。</summary>
    internal void RaiseRuntimeFault(HostOperationException failure)
    {
        if (State == HostRuntimeState.Faulted) return;
        LastRuntimeFailure = failure;
        State = HostRuntimeState.Faulted;
        // runtime 已死（emergency teardown 已销毁 project session）：project 不再算打开。
        IsOpen = false;
        RuntimeFaulted?.Invoke(failure);
    }
}

/// <summary>
/// A7-2 production channel：唯一 native owner 为 EngineHost。
/// 本类型内不出现任何 legacy <c>EditorSession_*</c> 调用（G2-03 结构保证）。
/// </summary>
internal sealed class EngineHostChannel : IEditorOps, IDisposable
{
    private const int FetchStringCapacity = 4096;
    private static readonly TimeSpan PumpInterval = TimeSpan.FromMilliseconds(33);

    private IntPtr _host = IntPtr.Zero;
    private EditorBridgeApi.EngineHostEventCallback? _pinnedCallback;
    /// <summary>Slice E：presentation fetch 的复用缓冲（按最大容量分配一次）。</summary>
    private byte[]? _frameBuffer;
    internal byte[]? FrameFetchBuffer => _frameBuffer;
    private DispatcherTimer? _pump;
    private Stopwatch _clock = new();
    private double _lastPumpSeconds;
    private long _pumpInvocations;
    private long _pumpFailures;
    private double _lastPumpDeltaSeconds;
    private bool _pumpActive;
    private long _generation = 1;
    private int _droppedLateEvents;
    private int _acceptedEvents;
    private int _suppressEvents;
    private long _pendingEditId = -1;
    private (int accepted, int executed, int failed) _pendingCounts;
    private bool _lastEditExecutedWithContextCurrent;
    private bool _disposed;
    private bool _runtimeFaultReported;

    /// <summary>
    /// Slice E：frame fetch 缓冲区的**分配次数**。
    /// 缓冲按最大容量分配**一次**并逐帧复用；这个计数是"复用"的直接证据
    /// （跨不同帧尺寸仍然是 1）。
    /// </summary>
    internal int FrameBufferAllocations => _frameBufferAllocations;
    private int _frameBufferAllocations;

    public bool IsCreated => _host != IntPtr.Zero;
    public long Generation => Interlocked.Read(ref _generation);
    public long PumpInvocations => Interlocked.Read(ref _pumpInvocations);
    public long PumpFailures => Interlocked.Read(ref _pumpFailures);
    public double LastPumpDeltaSeconds => _lastPumpDeltaSeconds;
    public int DroppedLateEventCount => Volatile.Read(ref _droppedLateEvents);
    public int AcceptedEventCount => Volatile.Read(ref _acceptedEvents);
    public (int accepted, int executed, int failed) PendingEditCounts => _pendingCounts;
    public bool LastPendingEditExecutedWithContextCurrent => _lastEditExecutedWithContextCurrent;

    /// <summary>
    /// ABI lockstep 闸（v1.5）：native 报告的版本必须与 managed 期望**完全一致**。
    ///
    /// 抽成独立方法（而不是内联在 <see cref="Create"/> 里）是为了让
    /// "v1.4 native + v1.5 managed" 这条混搭路径**可执行地被断言** ——
    /// gate 直接用 v1.4 的版本号调用它，并要求它抛出。
    /// 它**不是**测试专用的旁路：<see cref="Create"/> 走的是同一个方法。
    /// </summary>
    internal static void VerifyEngineHostAbiVersion(int reportedVersion)
    {
        if (reportedVersion == EditorBridgeApi.EngineHostExpectedVersion) return;
        throw new HostOperationException("bootstrap",
            EditorBridgeApi.EngineHostStatusInitFailed,
            EditorBridgeApi.EngineHostStageBootstrap,
            $"EngineHost ABI version mismatch: got 0x{reportedVersion:X8}, expected 0x{EditorBridgeApi.EngineHostExpectedVersion:X8}");
    }

    public void Create()
    {
        if (_host != IntPtr.Zero) return;
        if (EditorBridgeApi.EditorBridge_RegisterMainThread() != 1)
            throw new HostOperationException("bootstrap",
                EditorBridgeApi.EngineHostStatusWrongThread,
                EditorBridgeApi.EngineHostStagePlatform,
                "EditorBridge main-thread registration failed");
        int version = EditorBridgeApi.EngineHost_GetApiVersion();
        VerifyEngineHostAbiVersion(version);
        var result = default(EditorBridgeApi.RuntimeResult);
        var host = EditorBridgeApi.EngineHost_Create(ref result);
        if (host == IntPtr.Zero)
        {
            // F2-05：句柄已被 native 删除（无半初始化），因此 diagnostic 文本无法回读；
            // 结构化信息（status/stage）必须原样上抛，不能退化成反射派发异常。
            throw new HostOperationException("bootstrap", result.Status, result.Stage,
                "EngineHost_Create returned null (native diagnostic text is unavailable: "
                + "the failed host was destroyed, no half-initialized handle exists)");
        }
        try
        {
            _pinnedCallback = OnNativeEvent;
            if (EditorBridgeApi.EngineHost_SetEventCallback(host, _pinnedCallback, IntPtr.Zero) != 0)
                throw new HostOperationException("bootstrap",
                    EditorBridgeApi.EngineHostStatusInitFailed,
                    EditorBridgeApi.EngineHostStageBootstrap,
                    "EngineHost_SetEventCallback failed");
            _host = host;
        }
        catch
        {
            _pinnedCallback = null;
            EditorBridgeApi.EngineHost_Destroy(host);
            throw;
        }
    }

    /// <summary>project 打开后启动唯一 pump（A7-C03），并重新允许事件 marshal。</summary>
    public void BeginRuntime()
    {
        Interlocked.Exchange(ref _suppressEvents, 0);
        // 幂等：已有运行中的 pump 时不得重置时钟（否则会把正在运行的泵重新锚定）。
        if (_pump is not null) return;

        // 时钟从 runtime 启动时刻起算：首帧只包含启动后的间隔，不含启动前的墙钟时间。
        _clock.Restart();
        _lastPumpSeconds = 0.0;
        _lastPumpDeltaSeconds = 0.0;
        _pumpActive = true;
        // 每个 timer 只允许驱动自己：Close/Reopen 之后残留的旧 tick 不得泵新 runtime。
        var timer = new DispatcherTimer(DispatcherPriority.Normal) { Interval = PumpInterval };
        timer.Tick += (_, _) =>
        {
            if (ReferenceEquals(_pump, timer)) PumpOnce();
        };
        _pump = timer;
        _pump.Start();
    }

    /// <summary>
    /// **唯一** runtime 驱动：service 持有的定时器每帧调用一次。
    /// UI 不得再持有自己的 pump 定时器（那会造成双倍 tick / 双倍模拟速度）。
    /// </summary>
    private void PumpOnce()
    {
        // StopProject / CloseProject 之后可能还有已排队的 tick：不得在 teardown 后再 pump。
        if (!_pumpActive || _host == IntPtr.Zero) return;

        var elapsed = _clock.Elapsed.TotalSeconds;
        var dt = EditorHostService.ClampPumpDeltaSeconds(elapsed - _lastPumpSeconds);
        _lastPumpSeconds = elapsed;
        _lastPumpDeltaSeconds = dt;
        Interlocked.Increment(ref _pumpInvocations);

        var result = default(EditorBridgeApi.RuntimeResult);
        int code = EditorBridgeApi.EngineHost_PumpOneFrame(_host, (float)dt, ref result);
        // pending edit 在本帧已排空；此处只做非阻塞取回，不在 UI 线程等待 completion。
        ResolvePendingEdit();
        // A2/F2-05：pump 的 typed failure 过去被完全忽略（继续 tick 一个已 fault 的 host）。
        // 现在必须停泵 + 转 Faulted + 上报一次，禁止静默继续。
        if (code != 0 || !IsFrameOk(result.Status))
        {
            // 失败的调用也计入 PumpInvocations，但不计入 native frames；由 PumpFailures 显式区分。
            Interlocked.Increment(ref _pumpFailures);
            RaiseRuntimeFault(new HostOperationException("pump frame", result.Status, result.Stage,
                ReadDiagnostic()));
            return;   // 失败的帧不得作为新帧发布给 UI
        }

        // 单一 pump 已产生新帧：UI 只负责消费/呈现，绝不在 handler 里再次 pump。
        FrameAdvancedHook?.Invoke();
    }

    private static bool IsFrameOk(int status)
        => status == EditorBridgeApi.EngineHostStatusSuccess
        || status == EditorBridgeApi.EngineHostStatusSkippedNotPlaying;

    /// <summary>幂等的 fault 上报：停泵 → 置 Faulted → 通知 UI（只通知第一次）。</summary>
    private void RaiseRuntimeFault(HostOperationException failure)
    {
        if (_runtimeFaultReported) return;
        _runtimeFaultReported = true;
        StopPump();
        RuntimeFaultedHook?.Invoke(failure);
    }

    internal Action<HostOperationException>? RuntimeFaultedHook;

    private void ResolvePendingEdit()
    {
        if (_pendingEditId < 0) return;
        var result = default(EditorBridgeApi.RuntimeResult);
        int state = EditorBridgeApi.EngineHost_PollPendingEdit(_host, _pendingEditId, ref result);
        if (state == 1) return;                 // 仍在队列中：不阻塞 UI 线程
        _pendingEditId = -1;
        ReadPendingEditProof();
        if (state < 0)
        {
            RaisePendingEditCompletedSafe(false, "pending edit id is unknown to the host");
            return;
        }
        bool executed = result.Status == EditorBridgeApi.EngineHostStatusSuccess;
        RaisePendingEditCompletedSafe(executed, executed ? string.Empty : ReadDiagnostic());
    }

    private void ReadPendingEditProof()
    {
        if (EditorBridgeApi.EngineHost_GetPendingEditProofState(
                _host, out int accepted, out int executed, out int failed,
                out int contextCurrent, out ulong _) != 0) return;
        _pendingCounts = (accepted, executed, failed);
        _lastEditExecutedWithContextCurrent = contextCurrent == 1;
    }

    /// <summary>Stage A/A1 evidence：只读最近一帧的 typed result；未 bootstrap 时为 null。</summary>
    public HostFrameProof? ReadLastFrameProof()
    {
        if (_host == IntPtr.Zero) return null;
        var result = default(EditorBridgeApi.RuntimeResult);
        if (EditorBridgeApi.EngineHost_GetLastFrameResult(_host, ref result) != 0) return null;
        return new HostFrameProof(result.OperationId, result.FrameId, result.Status,
            result.Stage, result.RawDtSeconds, result.EffectiveDtSeconds);
    }

    /// <summary>Stage A/A1 evidence：只读 native 累计帧数 / tick 次数。</summary>
    public (ulong frames, ulong ticks) ReadRuntimeCounts()
    {
        if (_host == IntPtr.Zero) return (0, 0);
        int code = EditorBridgeApi.EngineHost_GetRuntimeProofState(_host,
            out _, out _, out _, out _, out _, out _, out _, out ulong frames, out ulong ticks);
        return code == 0 ? (frames, ticks) : (0, 0);
    }

    private void RaisePendingEditCompletedSafe(bool executed, string diagnostic)
        => PendingEditCompletedHook?.Invoke(executed, diagnostic);

    internal Action<bool, string>? PendingEditCompletedHook;
    internal Action? FrameAdvancedHook;

    // ── native event → UI thread marshal + managed generation guard ──

    private void OnNativeEvent(int eventType, ulong nativeGeneration, string payload, IntPtr userData)
        => MarshalEvent(Interlocked.Read(ref _generation), eventType, payload ?? string.Empty);

    /// <summary>
    /// A7-C04：native runtime thread → UI thread marshal，并在 UI 侧校验 managed generation。
    /// generation 捕获/携带/校验全部限定在 managed seam，不依赖 native <c>m_Generation</c>。
    /// </summary>
    internal void MarshalEvent(long capturedGeneration, int eventType, string text)
    {
        // teardown 期间 native 仍会 emit；这些事件对编辑器 UI 没有意义，
        // 且到达时 runtime 已不可用，必须在 marshal 之前拦掉。
        if (Volatile.Read(ref _suppressEvents) != 0) return;
        Dispatcher.UIThread.Post(() =>
        {
            if (capturedGeneration != Interlocked.Read(ref _generation))
            {
                Interlocked.Increment(ref _droppedLateEvents);
                return;
            }
            if (_disposed) return;
            Interlocked.Increment(ref _acceptedEvents);
            RaiseEvent(eventType, text);
        });
    }

    private void BeginTeardown()
    {
        Interlocked.Exchange(ref _suppressEvents, 1);
        BumpGeneration();
    }

    private void RaiseEvent(int eventType, string payload)
        => NativeEventHook?.Invoke(eventType, payload);

    internal Action<int, string>? NativeEventHook;

    private void BumpGeneration() => Interlocked.Increment(ref _generation);

    public void CloseProject(bool faulted = false)
    {
        BeginTeardown();
        StopPump();
        _pendingEditId = -1;
        if (_host == IntPtr.Zero) return;
        // A2/F2-05：runtime 已因 emergency teardown 自毁时，不再向 native 提交 CloseProject
        // （它只会拿到 InvalidState，却仍是一次对已拆解 host 的调用）。
        if (faulted) return;
        var result = default(EditorBridgeApi.RuntimeResult);
        EditorBridgeApi.EngineHost_CloseProject(_host, ref result);
        ReadPendingEditProof();
    }

    private void StopPump()
    {
        _pumpActive = false;
        if (_pump is null) return;
        _pump.Stop();
        _pump = null;
        _clock = new Stopwatch();
        _lastPumpSeconds = 0.0;
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        BeginTeardown();
        StopPump();
        if (_host == IntPtr.Zero) return;
        var host = _host;
        EditorBridgeApi.EngineHost_SetEventCallback(host, null, IntPtr.Zero);
        var result = default(EditorBridgeApi.RuntimeResult);
        int code = EditorBridgeApi.EngineHost_Shutdown(host, ref result);
        bool shutdownComplete = code == 0
            && result.Status == EditorBridgeApi.EngineHostStatusShutdownComplete;
        // 委托必须在 Shutdown 完成之后才释放，避免 late callback 打到已回收委托。
        _pinnedCallback = null;
        NativeEventHook = null;
        PendingEditCompletedHook = null;
        _host = IntPtr.Zero;
        // A6/F2-05：Shutdown 未完成（faulted 主机恒返回 ShutdownFailed，或 join 超时）时
        // **不**调用 Destroy —— 在 runtime 线程可能仍存活时 delete host 就是 late-free，
        // 实测会在进程退出阶段表现为偶发堆损坏（0xC0000374）或退出挂死。
        // 保留句柄并上报，由进程退出回收；宁可泄漏也不在不确定状态下释放。
        if (shutdownComplete)
        {
            EditorBridgeApi.EngineHost_Destroy(host);
        }
        else
        {
            RuntimeFaultedHook?.Invoke(new HostOperationException("shutdown",
                result.Status, result.Stage,
                "shutdown did not complete; the native host is intentionally not destroyed"));
        }
    }

    // ── IEditorOps：A7-1 op-dispatch seam ───────────────────

    private int Dispatch(int op, int arg0 = 0, int arg1 = 0,
                         string? strArg = null, string? strArg2 = null, string? strArg3 = null,
                         float[]? floatArg = null)
    {
        var result = default(EditorBridgeApi.RuntimeResult);
        return EditorBridgeApi.EngineHost_Dispatch(_host, op, arg0, arg1,
                                                   strArg, strArg2, strArg3, floatArg, ref result);
    }

    private string Fetch(int op, int arg0 = 0, string? strArg = null, string? strArg2 = null)
    {
        var result = default(EditorBridgeApi.RuntimeResult);
        var buffer = new byte[FetchStringCapacity];
        int code = EditorBridgeApi.EngineHost_Fetch(_host, op, arg0, strArg, strArg2, null,
                                                    buffer, buffer.Length, ref result);
        if (code < 0) return string.Empty;
        int length = Array.IndexOf(buffer, (byte)0);
        if (length < 0) length = buffer.Length;
        return Encoding.UTF8.GetString(buffer, 0, length);
    }

    public bool OpenProject(string manifestPath, string scenePath)
    {
        var result = default(EditorBridgeApi.RuntimeResult);
        int code = EditorBridgeApi.EngineHost_OpenProject(_host, manifestPath, scenePath, ref result);
        if (code != 0) throw Failure("open project", result);
        BumpGeneration();
        return true;
    }

    public bool SaveProject() => Dispatch(EditorBridgeApi.EngineHostOpSaveProject) == 0;
    public (int entities, int assets) GetCounts()
    {
        int entities = Dispatch(EditorBridgeApi.EngineHostOpGetEntityCount);
        int assets = Dispatch(EditorBridgeApi.EngineHostOpGetAssetCount);
        return (entities, assets);
    }

    public int CreateEntity(string? name) => Dispatch(EditorBridgeApi.EngineHostOpCreateEntity, strArg: name);
    public string? GetEntityName(int index) => Fetch(EditorBridgeApi.EngineHostOpGetEntityName, index);

    public (float x, float y, float z) GetEntityPosition(int index)
    {
        var result = default(EditorBridgeApi.RuntimeResult);
        var buffer = new byte[12];
        int code = EditorBridgeApi.EngineHost_Fetch(_host, EditorBridgeApi.EngineHostOpGetEntityPosition,
                                                    index, null, null, null, buffer, buffer.Length, ref result);
        if (code < 0) throw Failure("get entity position", result);
        return (BitConverter.ToSingle(buffer, 0), BitConverter.ToSingle(buffer, 4), BitConverter.ToSingle(buffer, 8));
    }

    public bool SetEntityPosition(int index, float x, float y, float z)
        => Dispatch(EditorBridgeApi.EngineHostOpSetEntityPosition, index, floatArg: new[] { x, y, z }) == 0;

    public string? GetAssetPath(int index) => Fetch(EditorBridgeApi.EngineHostOpGetAssetPath, index);
    public int GetAssetType(int index) => Dispatch(EditorBridgeApi.EngineHostOpGetAssetType, index);
    public string GetAssetGuid(int index) => Fetch(EditorBridgeApi.EngineHostOpGetAssetGuid, index);
    public int ImportAsset(string path, int type) => Dispatch(EditorBridgeApi.EngineHostOpImportAsset, type, strArg: path);
    public bool RenameAsset(int assetIndex, string newName) => Dispatch(EditorBridgeApi.EngineHostOpRenameAsset, assetIndex, strArg: newName) == 0;
    public bool DeleteEntity(int index) => Dispatch(EditorBridgeApi.EngineHostOpDeleteEntity, index) == 0;
    public bool RenameEntity(int index, string newName) => Dispatch(EditorBridgeApi.EngineHostOpRenameEntity, index, strArg: newName) == 0;
    public string GetEntitySprite(int index) => Fetch(EditorBridgeApi.EngineHostOpGetEntitySprite, index);
    public string GetEntityScript(int index) => Fetch(EditorBridgeApi.EngineHostOpGetEntityScript, index);

    /// <summary>
    /// HRC-3 P2：原子取整帧（Fetch-only op，ABI v1.2）。
    ///
    /// 这一层**只镜像/解码** native packet：不读 FBO、不调 GL、不生成 frame id、
    /// 不缓存旧帧冒充新帧、不改 pump、不等帧完成、不引入第二条 transport。
    ///
    /// 语义：
    ///   - native 失败（code &lt; 0，含 capacity 不足）：抛异常，**保留 native 原始
    ///     status/stage/diagnostic**，并且**不返回半成品帧**；
    ///   - 没有新帧：返回 NoNewFrame，Frame 为 null，**不会**用上一次的结果顶替；
    ///   - 有新帧：按冻结 offset 解码为 <see cref="PresentationFrame"/>。
    /// </summary>
    public FrameFetchResult TryFetchPresentationFrame()
    {
        var result = default(EditorBridgeApi.RuntimeResult);
        // Slice E：容量按**最大尺寸**分配**一次**并复用。
        // 每次 fetch 都 new 一个最大容量缓冲会在 30Hz 下产生数百 MB/s 垃圾。
        // 复用是安全的：解码只读 header 声明的前 `16 + w*h*4` 字节，
        // 且每次 fetch 前不清零（native 要么写满该范围，要么不写任何字节）。
        if (_frameBuffer is null)
        {
            _frameBuffer = new byte[EditorBridgeApi.EngineHostFramePacketBytes];
            _frameBufferAllocations++;
        }
        var buffer = _frameBuffer;

        int code = EditorBridgeApi.EngineHost_Fetch(
            _host,
            EditorBridgeApi.EngineHostOpGetPresentationFrame,
            0, null, null, null,
            buffer, buffer.Length,
            ref result);

        if (code < 0)
        {
            // 失败语义完整透传（含 capacity 不足这类"不生成部分帧"的情况）
            throw Failure("fetch presentation frame", result);
        }

        if (result.Status == EditorBridgeApi.EngineHostStatusNoNewFrame || code == 0)
        {
            // 明确"没有新帧"。绝不复用旧帧。
            return FrameFetchResult.NoNewFrame(result.Status);
        }

        if (!PresentationFrame.TryDecode(buffer, code, out var frame, out var error) || frame is null)
        {
            throw Failure($"decode presentation frame packet ({error})", result);
        }

        return FrameFetchResult.New(frame, result.Status);
    }

    // ── HRC-3 D3：pick 两段式（Dispatch(PICK) → 既有 pump → Fetch(PICK_RESULT)）──
    //
    // 只做 typed plumbing：不新增 pump、不调 GL、不缓存 latest result。
    // outstanding request 由 native 侧以 capacity=1 持有；managed 只跟踪
    // operation_id 用于配对校验。
    private ulong _pickRequestId;   // 0 = 无 outstanding

    /// <summary>
    /// 提交 pick 请求。返回的 <see cref="PickRequestResult.RequestId"/> 必须
    /// 与后续 packet 的 request_id 一致。
    ///
    /// <summary>
    /// Slice E：提交 viewport 尺寸。只登记；真正生效在下一帧的
    /// current-context 窗口内、渲染之前（native 侧 last-wins）。
    /// 拒绝时返回 false 且**不**改变当前 FBO 尺寸。
    /// </summary>
    public bool SetViewportSize(int width, int height)
    {
        var result = default(EditorBridgeApi.RuntimeResult);
        int code = EditorBridgeApi.EngineHost_Dispatch(_host,
            EditorBridgeApi.EngineHostOpSetViewportSize, 0, 0, null, null, null,
            new[] { (float)width, (float)height }, ref result);
        return code == 0 && result.Status == EditorBridgeApi.EngineHostStatusSuccess;
    }

    /// capacity=1：第二个请求返回 <see cref="PickRequestResult.Busy"/>，
    /// <b>绝不替换</b>已受理的 outstanding request（也不改写 _pickRequestId）。
    /// </summary>
    public PickRequestResult RequestPick(float x, float y)
    {
        var result = default(EditorBridgeApi.RuntimeResult);
        // 直接调 P/Invoke（而非私有 Dispatch 助手），因为需要读回
        // RuntimeResult.Status（区分 PICK_BUSY）与 OperationId（配对键）。
        int code = EditorBridgeApi.EngineHost_Dispatch(_host,
            EditorBridgeApi.EngineHostOpPick, 0, 0, null, null, null,
            new[] { x, y }, ref result);

        if (result.Status == EditorBridgeApi.EngineHostStatusPickBusy)
        {
            // 保持 outstanding 不变。
            return PickRequestResult.Busy_(result.Status);
        }
        if (code <= 0)
        {
            return PickRequestResult.Rejected(result.Status);
        }

        _pickRequestId = result.OperationId;
        return PickRequestResult.Accepted_(_pickRequestId, result.Status);
    }

    /// <summary>
    /// 取回 pick 结果。
    ///
    /// 语义（与 P2 同构的 fail-closed 取数）：
    ///   - native 硬失败（code &lt; 0）：抛异常，保留原始 status/stage/diagnostic；
    ///   - INVALID_STATE（stage=EDIT）：已受理但尚未产出，只观察、不消费；
    ///   - INVALID_STATE（stage=SHUTDOWN）：被 teardown 作废，<b>不产生 packet</b>；
    ///   - SUCCESS：按冻结 offset 严格解码，并校验 request_id 与 outstanding 配对。
    /// </summary>
    public PickFetchResult TryFetchPickResult()
    {
        var result = default(EditorBridgeApi.RuntimeResult);
        // 静态容量 32B：编译期固定，不协商、不做 partial read。
        var buffer = new byte[EditorBridgeApi.EngineHostPickPacketBytes];

        int code = EditorBridgeApi.EngineHost_Fetch(_host,
            EditorBridgeApi.EngineHostOpPick,
            0, null, null, null,
            buffer, buffer.Length,
            ref result);

        // ★ native FetchPickResult 的返回码**不能**当成功/失败信号：
        //   - 成功时返回 0，packet 写进 outBuffer（与 FetchPresentationFrame 返回字节数不同）；
        //   - 结构化拒绝时 rejectAt 返回 -1，**同时**写入 status/stage。
        //   因此判据必须是 status。
        //
        // INVALID_STATE 有两个合法子情形，都**没有 packet**：
        //   stage=EDIT     → 已受理但尚未产出结果（只观察、不消费）
        //   stage=SHUTDOWN → 被 teardown 作废（依冻结契约不产生 packet）
        if (result.Status == EditorBridgeApi.EngineHostStatusInvalidState)
        {
            return PickFetchResult.NoResult(result.Status, result.Stage);
        }

        if (code < 0)
        {
            throw Failure("fetch pick result", result);
        }

        if (result.Status != EditorBridgeApi.EngineHostStatusSuccess)
        {
            return PickFetchResult.NoResult(result.Status, result.Stage);
        }

        if (!PickResult.TryDecode(buffer, EditorBridgeApi.EngineHostPickPacketBytes,
                                  out var pick, out var error) || pick is null)
        {
            throw Failure($"decode pick result packet ({error})", result);
        }

        // 配对校验：request_id 必须 == 我们 outstanding 的 operation_id。
        // 不匹配意味着 stale/旧 generation 的结果回流 —— fail-closed，不返回。
        if (_pickRequestId != 0 && pick.RequestId != _pickRequestId)
        {
            throw Failure(
                $"pick request_id mismatch: got {pick.RequestId}, expected {_pickRequestId}", result);
        }

        _pickRequestId = 0;   // 消费即释放；不缓存结果本身
        return PickFetchResult.New(pick, result.Status);
    }

    public bool AssignScript(int assetIndex, int entityIndex) => Dispatch(EditorBridgeApi.EngineHostOpAssignScript, assetIndex, entityIndex) == 0;
    public string ScriptRead(int assetIndex) => Fetch(EditorBridgeApi.EngineHostOpScriptRead, assetIndex);
    public bool ScriptSave(int assetIndex, string text) => Dispatch(EditorBridgeApi.EngineHostOpScriptSave, assetIndex, strArg: text) == 0;

    /// <summary>GL 依赖：返回 accepted，执行结果下一帧后由 PendingEditCompleted 给出。</summary>
    public bool AssignSprite(int assetIndex, int entityIndex)
    {
        int code = Dispatch(EditorBridgeApi.EngineHostOpAssignSprite, assetIndex, entityIndex);
        if (code <= 0) return false;
        _pendingEditId = code;
        return true;
    }

    public bool Play(int assetIndex)
    {
        var result = default(EditorBridgeApi.RuntimeResult);
        int code = EditorBridgeApi.EngineHost_Play(_host, assetIndex, ref result);
        if (code != 0) throw Failure("play", result);
        return true;
    }

    public bool Reload()
    {
        var result = default(EditorBridgeApi.RuntimeResult);
        int code = EditorBridgeApi.EngineHost_Reload(_host, ref result);
        if (code != 0) throw Failure("reload", result);
        return true;
    }

    public void Stop()
    {
        var result = default(EditorBridgeApi.RuntimeResult);
        int code = EditorBridgeApi.EngineHost_Stop(_host, ref result);
        if (code != 0) throw Failure("stop", result);
    }

    /// <summary>
    /// 显式 pump：**仅供 gate / 诊断**逐帧取证。产品 UI 的唯一驱动是 service 定时器；
    /// UI 调用本方法就等于新增第二个 pump 源。
    /// </summary>
    public void RuntimeTick(float dt)
    {
        Interlocked.Increment(ref _pumpInvocations);
        var effectiveDt = (float)EditorHostService.ClampPumpDeltaSeconds(dt);
        _lastPumpDeltaSeconds = effectiveDt;
        var result = default(EditorBridgeApi.RuntimeResult);
        int code = EditorBridgeApi.EngineHost_PumpOneFrame(_host, effectiveDt, ref result);
        if (code != 0 || !IsFrameOk(result.Status))
            Interlocked.Increment(ref _pumpFailures);
        ResolvePendingEdit();
    }

    /// <summary>A7-0：GetRuntimeError / GetLastError 统一走 host diagnostic。</summary>
    public string GetRuntimeError() => ReadDiagnostic();
    public string GetLastError() => ReadDiagnostic();
    public string GetWarnings() => Fetch(EditorBridgeApi.EngineHostOpGetWarnings);
    public bool IsDirty() => Dispatch(EditorBridgeApi.EngineHostOpIsDirty) == 1;
    public bool AddComponent(int entityIndex, string typeName) => Dispatch(EditorBridgeApi.EngineHostOpAddComponent, entityIndex, strArg: typeName) == 0;
    public bool RemoveComponent(int entityIndex, string typeName) => Dispatch(EditorBridgeApi.EngineHostOpRemoveComponent, entityIndex, strArg: typeName) == 0;
    public bool HasComponent(int entityIndex, string typeName) => Dispatch(EditorBridgeApi.EngineHostOpHasComponent, entityIndex, strArg: typeName) == 1;
    public string GetComponentProperty(int entityIndex, string typeName, string propName)
    {
        var result = default(EditorBridgeApi.RuntimeResult);
        var buffer = new byte[512];
        int code = EditorBridgeApi.EngineHost_Fetch(_host, EditorBridgeApi.EngineHostOpGetComponentProperty,
                                                    entityIndex, typeName, propName, null, buffer, buffer.Length, ref result);
        if (code < 0) return string.Empty;
        int length = Array.IndexOf(buffer, (byte)0);
        if (length < 0) length = buffer.Length;
        return Encoding.UTF8.GetString(buffer, 0, length);
    }

    public bool SetComponentProperty(int entityIndex, string typeName, string propName, string value)
        => Dispatch(EditorBridgeApi.EngineHostOpSetComponentProperty, entityIndex,
                    strArg: typeName, strArg2: propName, strArg3: value) == 0;

    private string ReadDiagnostic()
    {
        if (_host == IntPtr.Zero) return string.Empty;
        int len = EditorBridgeApi.EngineHost_GetLastDiagnostic(_host, IntPtr.Zero, 0);
        if (len <= 0) return string.Empty;
        IntPtr ptr = Marshal.AllocHGlobal(len + 1);
        try
        {
            EditorBridgeApi.EngineHost_GetLastDiagnostic(_host, ptr, len + 1);
            return Marshal.PtrToStringUTF8(ptr) ?? string.Empty;
        }
        finally { Marshal.FreeHGlobal(ptr); }
    }

    private Exception Failure(string what, EditorBridgeApi.RuntimeResult result)
    {
        string diag = ReadDiagnostic();
        if (string.IsNullOrEmpty(diag))
            diag = $"status={result.Status} stage={result.Stage}";
        return new HostOperationException(what, result.Status, result.Stage, diag);
    }
}

/// <summary>
/// HRC-1 gate 专用 legacy channel。仅在 <see cref="HostBackend.LegacySession"/> 下构造。
/// </summary>
internal sealed class LegacySessionChannel : IEditorOps
{
    private IntPtr _session = IntPtr.Zero;
    private EditorBridgeApi.EditorEventCallback? _pinnedCallback;
    private long _generation = 1;

    public bool IsCreated => _session != IntPtr.Zero;
    public long Generation => Interlocked.Read(ref _generation);

    public void Create()
    {
        if (_session != IntPtr.Zero) return;
        if (EditorBridgeApi.EditorBridge_RegisterMainThread() != 1)
            throw new HostOperationException("bootstrap",
                EditorBridgeApi.EngineHostStatusWrongThread,
                EditorBridgeApi.EngineHostStagePlatform,
                "EditorBridge main-thread registration failed");
        int version = EditorBridgeApi.EditorBridge_GetVersion();
        if (version != EditorBridgeApi.ExpectedVersion)
            throw new HostOperationException("bootstrap",
                EditorBridgeApi.EngineHostStatusInitFailed,
                EditorBridgeApi.EngineHostStageBootstrap,
                $"EditorBridge ABI version mismatch: got 0x{version:X8}, expected 0x{EditorBridgeApi.ExpectedVersion:X8}");
        var session = EditorBridgeApi.EditorSession_Create();
        if (session == IntPtr.Zero)
            throw new HostOperationException("bootstrap",
                EditorBridgeApi.EngineHostStatusInitFailed,
                EditorBridgeApi.EngineHostStageBootstrap,
                "EditorSession_Create returned null (legacy backend)");
        try
        {
            _session = session;
            _pinnedCallback = (type, payload, _) => RaiseEvent(type, payload);
            EditorBridgeApi.EditorSession_SetEventCallback(session, _pinnedCallback, IntPtr.Zero);
        }
        catch
        {
            _session = IntPtr.Zero;
            _pinnedCallback = null;
            try { EditorBridgeApi.EditorSession_Destroy(session); } catch { }
            throw;
        }
    }

    public void DestroySession()
    {
        Interlocked.Increment(ref _generation);
        var session = _session;
        _session = IntPtr.Zero;
        _pinnedCallback = null;
        if (session == IntPtr.Zero) return;
        try { EditorBridgeApi.EditorSession_SetEventCallback(session, null, IntPtr.Zero); }
        finally { EditorBridgeApi.EditorSession_Destroy(session); }
    }

    internal Action<int, string>? NativeEventHook;
    private void RaiseEvent(int type, string payload) => NativeEventHook?.Invoke(type, payload);

    private IntPtr Require() => _session != IntPtr.Zero
        ? _session
        : throw new InvalidOperationException("native session not created");

    public bool OpenProject(string manifestPath, string scenePath) => EditorBridgeApi.EditorSession_OpenProject(Require(), manifestPath, scenePath);
    public bool SaveProject() => EditorBridgeApi.EditorSession_SaveProject(Require());
    public (int entities, int assets) GetCounts() => (EditorBridgeApi.EditorSession_GetEntityCount(Require()), EditorBridgeApi.EditorSession_GetAssetCount(Require()));
    public int CreateEntity(string? name) => EditorBridgeApi.EditorSession_CreateEntity(Require(), name);

    public string? GetEntityName(int index) => ReadString(256, buf => EditorBridgeApi.EditorSession_GetEntityName(Require(), index, buf, 256), allowEmpty: true);

    public (float x, float y, float z) GetEntityPosition(int index)
    {
        var buf = Marshal.AllocHGlobal(12);
        try
        {
            if (EditorBridgeApi.EditorSession_GetEntityPosition(Require(), index, buf) != 0)
                throw new InvalidOperationException("GetEntityPosition failed");
            var f = new float[3];
            Marshal.Copy(buf, f, 0, 3);
            return (f[0], f[1], f[2]);
        }
        finally { Marshal.FreeHGlobal(buf); }
    }

    public bool SetEntityPosition(int index, float x, float y, float z)
    {
        var buf = Marshal.AllocHGlobal(12);
        try
        {
            Marshal.Copy(new[] { x, y, z }, 0, buf, 3);
            return EditorBridgeApi.EditorSession_SetEntityPosition(Require(), index, buf) == 0;
        }
        finally { Marshal.FreeHGlobal(buf); }
    }

    public string? GetAssetPath(int index) => ReadString(512, buf => EditorBridgeApi.EditorSession_GetAssetPath(Require(), index, buf, 512), allowEmpty: true);
    public int GetAssetType(int index) => EditorBridgeApi.EditorSession_GetAssetType(Require(), index);

    public string GetAssetGuid(int index) => ReadString(64, buf => EditorBridgeApi.EditorSession_GetAssetGuid(Require(), index, buf, 64), allowEmpty: false) ?? string.Empty;

    public int ImportAsset(string path, int type) => EditorBridgeApi.EditorSession_ImportAsset(Require(), path, type);
    public bool RenameAsset(int assetIndex, string newName) => EditorBridgeApi.EditorSession_RenameAsset(Require(), assetIndex, newName) == 0;
    public bool DeleteEntity(int index) => EditorBridgeApi.EditorSession_DeleteEntity(Require(), index) == 0;
    public bool RenameEntity(int index, string newName) => EditorBridgeApi.EditorSession_RenameEntity(Require(), index, newName) == 0;
    public string GetEntitySprite(int index) => ReadString(512, buf => EditorBridgeApi.EditorSession_GetEntitySprite(Require(), index, buf, 512), allowEmpty: false) ?? string.Empty;
    public bool AssignSprite(int assetIndex, int entityIndex) => EditorBridgeApi.EditorSession_AssignSprite(Require(), assetIndex, entityIndex) == 0;
    public string GetEntityScript(int index) => ReadString(512, buf => EditorBridgeApi.EditorSession_GetEntityScript(Require(), index, buf, 512), allowEmpty: false) ?? string.Empty;

    public bool AssignScript(int assetIndex, int entityIndex) => EditorBridgeApi.EditorSession_AssignScript(Require(), assetIndex, entityIndex) == 0;

    public string ScriptRead(int assetIndex)
    {
        var probe = Marshal.AllocHGlobal(8);
        int size;
        try { size = EditorBridgeApi.EditorSession_ScriptRead(Require(), assetIndex, probe, 8); }
        finally { Marshal.FreeHGlobal(probe); }
        if (size <= 0) return string.Empty;
        var buf = Marshal.AllocHGlobal(size + 1);
        try
        {
            if (EditorBridgeApi.EditorSession_ScriptRead(Require(), assetIndex, buf, size + 1) < 0) return string.Empty;
            return Marshal.PtrToStringUTF8(buf) ?? string.Empty;
        }
        finally { Marshal.FreeHGlobal(buf); }
    }

    public bool ScriptSave(int assetIndex, string text) => EditorBridgeApi.EditorSession_ScriptSave(Require(), assetIndex, text) == 0;
    public bool Play(int assetIndex) => EditorBridgeApi.EditorSession_Play(Require(), assetIndex) != 0;
    public bool Reload() => EditorBridgeApi.EditorSession_Reload(Require()) != 0;
    public void Stop() => EditorBridgeApi.EditorSession_Stop(Require());
    public void RuntimeTick(float dt) => EditorBridgeApi.EditorSession_RuntimeTick(Require(), dt);
    public string GetRuntimeError() => ReadString(512, buf => EditorBridgeApi.EditorSession_GetRuntimeError(Require(), buf, 512), allowEmpty: false) ?? string.Empty;

    public int RuntimePersistInt(string key, int defaultVal)
        => _session == IntPtr.Zero ? defaultVal : EditorBridgeApi.EditorSession_RuntimePersistInt(_session, key, defaultVal);

    public bool IsDirty() => EditorBridgeApi.EditorSession_IsDirty(Require()) != 0;

    public string GetLastError() => _session == IntPtr.Zero
        ? "(no session)"
        : (ReadString(512, buf => EditorBridgeApi.EditorSession_GetLastError(Require(), buf, 512), allowEmpty: false) ?? string.Empty);

    public string GetWarnings() => _session == IntPtr.Zero
        ? string.Empty
        : (ReadString(4096, buf => EditorBridgeApi.EditorSession_GetWarnings(Require(), buf, 4096), allowEmpty: false) ?? string.Empty);

    public bool AddComponent(int entityIndex, string typeName) => EditorBridgeApi.EditorSession_AddComponent(Require(), entityIndex, typeName) == 0;
    public bool RemoveComponent(int entityIndex, string typeName) => EditorBridgeApi.EditorSession_RemoveComponent(Require(), entityIndex, typeName) == 0;
    public bool HasComponent(int entityIndex, string typeName) => _session != IntPtr.Zero && EditorBridgeApi.EditorSession_HasComponent(_session, entityIndex, typeName) == 1;

    public string GetComponentProperty(int entityIndex, string typeName, string propName)
    {
        var probe = Marshal.AllocHGlobal(8);
        int len;
        try { len = EditorBridgeApi.EditorSession_GetComponentProperty(Require(), entityIndex, typeName, propName, probe, 8); }
        finally { Marshal.FreeHGlobal(probe); }
        if (len < 0) return string.Empty;
        var buf = Marshal.AllocHGlobal(len + 1);
        try
        {
            if (EditorBridgeApi.EditorSession_GetComponentProperty(Require(), entityIndex, typeName, propName, buf, len + 1) < 0)
                return string.Empty;
            return Marshal.PtrToStringUTF8(buf) ?? string.Empty;
        }
        finally { Marshal.FreeHGlobal(buf); }
    }

    public bool SetComponentProperty(int entityIndex, string typeName, string propName, string value)
        => EditorBridgeApi.EditorSession_SetComponentProperty(Require(), entityIndex, typeName, propName, value) == 0;

    // ── Viewport / Gizmo（A7 范围外，仅 legacy）─────────────

    public uint GetViewportTextureHandle() => _session == IntPtr.Zero ? 0u : EditorBridgeApi.EditorSession_GetViewportTextureHandle(_session);
    public (int width, int height) GetViewportSize() { EditorBridgeApi.EditorSession_GetViewportSize(Require(), out int w, out int h); return (w, h); }
    public void SetViewportSize(int width, int height) => EditorBridgeApi.EditorSession_SetViewportSize(Require(), width, height);
    public void ViewportTick(float dt) => EditorBridgeApi.EditorSession_ViewportTick(Require(), dt);
    public void SetViewportCameraPosition(float x, float y, float z) => EditorBridgeApi.EditorSession_SetViewportCameraPosition(Require(), x, y, z);

    public (float x, float y, float z) GetViewportCameraPosition()
    {
        var buf = Marshal.AllocHGlobal(12);
        try
        {
            EditorBridgeApi.EditorSession_GetViewportCameraPosition(Require(), buf);
            var f = new float[3];
            Marshal.Copy(buf, f, 0, 3);
            return (f[0], f[1], f[2]);
        }
        finally { Marshal.FreeHGlobal(buf); }
    }

    public void SetViewportCameraProjection(int type) => EditorBridgeApi.EditorSession_SetViewportCameraProjection(Require(), type);
    public void SetViewportCameraZoom(float zoom) => EditorBridgeApi.EditorSession_SetViewportCameraZoom(Require(), zoom);
    public void ViewportMouseMove(float x, float y) => EditorBridgeApi.EditorSession_ViewportMouseMove(Require(), x, y);
    public void ViewportMouseButton(int button, bool pressed) => EditorBridgeApi.EditorSession_ViewportMouseButton(Require(), button, pressed ? 1 : 0);
    public void ViewportScroll(float delta) => EditorBridgeApi.EditorSession_ViewportScroll(Require(), delta);
    public void ViewportKeyDown(int key) => EditorBridgeApi.EditorSession_ViewportKeyDown(Require(), key);
    public void ViewportKeyUp(int key) => EditorBridgeApi.EditorSession_ViewportKeyUp(Require(), key);
    public int ViewportPick(float ndcX, float ndcY) => EditorBridgeApi.EditorSession_ViewportPick(Require(), ndcX, ndcY);
    public void SetGizmoMode(int mode) => EditorBridgeApi.EditorSession_SetGizmoMode(Require(), mode);
    public void SetGizmoSpace(int space) => EditorBridgeApi.EditorSession_SetGizmoSpace(Require(), space);
    public void SetGizmoSnap(bool enabled, float value) => EditorBridgeApi.EditorSession_SetGizmoSnap(Require(), enabled ? 1 : 0, value);
    public int GetGizmoMode() => _session == IntPtr.Zero ? 0 : EditorBridgeApi.EditorSession_GetGizmoMode(_session);
    public int GetGizmoSpace() => _session == IntPtr.Zero ? 0 : EditorBridgeApi.EditorSession_GetGizmoSpace(_session);
    public bool GetGizmoSnapEnabled() => _session != IntPtr.Zero && EditorBridgeApi.EditorSession_GetGizmoSnapEnabled(_session) != 0;
    public float GetGizmoSnapValue() => _session == IntPtr.Zero ? 0.5f : EditorBridgeApi.EditorSession_GetGizmoSnapValue(_session);
    public void SetViewportGridVisible(bool visible) => EditorBridgeApi.EditorSession_SetViewportGridVisible(Require(), visible ? 1 : 0);
    public bool GetViewportGridVisible() => _session == IntPtr.Zero || EditorBridgeApi.EditorSession_GetViewportGridVisible(_session) != 0;

    public (int width, int height, byte[]? pixels) GetViewportPixels()
    {
        if (_session == IntPtr.Zero) return (0, 0, null);
        int requiredSize = EditorBridgeApi.EditorSession_GetViewportPixels(_session, IntPtr.Zero, 0);
        if (requiredSize <= 8) return (0, 0, null);
        IntPtr buf = Marshal.AllocHGlobal(requiredSize);
        try
        {
            int written = EditorBridgeApi.EditorSession_GetViewportPixels(_session, buf, requiredSize);
            if (written < requiredSize) return (0, 0, null);
            int w = Marshal.ReadInt32(buf, 0);
            int h = Marshal.ReadInt32(buf, 4);
            if (w <= 0 || h <= 0) return (0, 0, null);
            var pixels = new byte[w * h * 4];
            Marshal.Copy(buf + 8, pixels, 0, w * h * 4);
            return (w, h, pixels);
        }
        finally { Marshal.FreeHGlobal(buf); }
    }

    public void SetCameraExternalInput(bool enabled)
    {
        if (_session != IntPtr.Zero)
            EditorBridgeApi.EditorSession_SetCameraExternalInput(_session, enabled ? 1 : 0);
    }

    public bool GetCameraExternalInput()
        => _session != IntPtr.Zero && EditorBridgeApi.EditorSession_GetCameraExternalInput(_session) != 0;

    private static string? ReadString(int capacity, Func<IntPtr, int> call, bool allowEmpty)
    {
        var buf = Marshal.AllocHGlobal(capacity);
        try
        {
            int len = call(buf);
            if (len < 0) return allowEmpty ? null : string.Empty;
            return Marshal.PtrToStringUTF8(buf) ?? string.Empty;
        }
        finally { Marshal.FreeHGlobal(buf); }
    }
}
