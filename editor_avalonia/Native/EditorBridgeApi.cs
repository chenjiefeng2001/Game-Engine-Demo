using System;
using System.Runtime.InteropServices;

namespace AvaloniaEditor.Native;

/// <summary>
/// EditorBridge C ABI 的 P/Invoke 面（AV-002）。
/// C# 只见句柄与 UTF-8 文本，绝无 C++ 指针/STL 泄漏（迁移方案 §3/§4）。
/// </summary>
public static class EditorBridgeApi
{
    public const int EvProjectLoaded = 1;
    public const int EvEntityCreated = 2;
    public const int EvEntityMoved = 3;
    public const int EvProjectSaved = 4;
    public const int EvEntityDeleted = 5;
    public const int EvEntityRenamed = 6;
    public const int EvEntityAssigned = 7;
    public const int EvEntityScriptAssigned = 8;
    public const int EvAssetImported = 9;
    public const int EvAssetRenamed = 10;

    // ── Phase 3-D (P3-D) 运行时 ──
    public const int EvPlayStarted = 11;
    public const int EvPlayStopped = 12;
    // ── F1 (Component Contract)：契约组件变更（Camera 等）──
    public const int EvComponentChanged = 13;
    public const int EvCount = 14;

    /// <summary>EditorBridge 自身的 ABI 版本（与 EngineHost 的版本号是两套，不随 P2 递增）。</summary>
    /// EditorBridge_GetVersion 未发生变化，仍为 v1.1。</summary>
    public const int ExpectedVersion = (1 << 16) | 1;
    /// <summary>
    /// EngineHost ABI 版本。HRC-3 P2 做了明确的 ABI increment：v1.1 -> v1.2。
    /// 增量只有一个：在既有 EngineHost_Fetch 的 op 空间内新增
    /// ENGINE_HOST_OP_GET_PRESENTATION_FRAME；没有新增 export function，
    /// RuntimeResult 仍是 76B 且结构不变，既有 op 编号/容量/语义全部不动。
    /// </summary>
    /// <remarks>
    /// HRC-3 D3：v1.2 -> v1.3。仍然**没有新增 export function**，只是在既有
    /// EngineHost_Dispatch / EngineHost_Fetch 的 op 空间内新增
    /// ENGINE_HOST_OP_PICK，并新增一个状态码 PICK_BUSY。
    /// RuntimeResult 仍是 76B 且结构不变。
    /// </remarks>
    public const int EngineHostExpectedVersion = (1 << 16) | 3;

    public const int EngineHostStatusSuccess = 0;
    public const int EngineHostStatusSkippedNotPlaying = 1;
    public const int EngineHostStatusInvalidState = 2;
    public const int EngineHostStatusWrongThread = 3;
    public const int EngineHostStatusInitFailed = 4;
    public const int EngineHostStatusGlFailed = 5;
    public const int EngineHostStatusSessionFailed = 6;
    public const int EngineHostStatusTickFault = 7;
    public const int EngineHostStatusShutdownComplete = 8;
    public const int EngineHostStatusShutdownFailed = 9;
    public const int EngineHostStatusEditAccepted = 10;

    /// <summary>
    /// HRC-3 P2：自上次成功 fetch 之后**没有新帧**。这不是失败。
    /// 此时 native 返回 0 且**不写任何 packet 字节** —— managed 绝不可复用旧帧冒充新帧。
    /// </summary>
    public const int EngineHostStatusNoNewFrame = 11;

    /// <summary>
    /// HRC-3 D3：pick pending capacity = 1，第二个 request 返回此状态而
    /// **不替换**已受理的第一个。outstanding request 可能处于
    /// "已受理但尚未 dispatch" 或 "已 dispatch 尚未取走" 两种状态。
    /// </summary>
    public const int EngineHostStatusPickBusy = 12;

    public const int EngineHostStageBootstrap = 0;
    public const int EngineHostStagePlatform = 1;
    public const int EngineHostStageGlContext = 2;
    public const int EngineHostStageGlad = 3;
    public const int EngineHostStageEngineSubsystem = 4;
    public const int EngineHostStageFbo = 5;
    public const int EngineHostStageSession = 6;
    public const int EngineHostStageOpenProject = 7;
    public const int EngineHostStagePlay = 8;
    public const int EngineHostStageTick = 9;
    public const int EngineHostStageShutdown = 10;
    public const int EngineHostStageEdit = 11;

    public const int EngineHostOpCreateEntity = 1;
    public const int EngineHostOpDeleteEntity = 2;
    public const int EngineHostOpRenameEntity = 3;
    public const int EngineHostOpSetEntityPosition = 4;
    public const int EngineHostOpGetEntityCount = 5;
    public const int EngineHostOpGetEntityName = 6;
    public const int EngineHostOpGetAssetCount = 7;
    public const int EngineHostOpGetAssetPath = 8;
    public const int EngineHostOpGetAssetType = 9;
    public const int EngineHostOpGetAssetGuid = 10;
    public const int EngineHostOpImportAsset = 11;
    public const int EngineHostOpRenameAsset = 12;
    public const int EngineHostOpGetEntitySprite = 13;
    public const int EngineHostOpGetEntityScript = 14;
    public const int EngineHostOpAssignScript = 15;
    public const int EngineHostOpScriptRead = 16;
    public const int EngineHostOpScriptSave = 17;
    public const int EngineHostOpSaveProject = 18;
    public const int EngineHostOpAddComponent = 19;
    public const int EngineHostOpRemoveComponent = 20;
    public const int EngineHostOpHasComponent = 21;
    public const int EngineHostOpGetComponentProperty = 22;
    public const int EngineHostOpSetComponentProperty = 23;
    public const int EngineHostOpIsDirty = 24;
    public const int EngineHostOpGetWarnings = 25;
    public const int EngineHostOpAssignSprite = 26;
    public const int EngineHostOpGetEntityPosition = 27;

    // ── HRC-3 P2：presentation frame（**Fetch-only**，ABI v1.2）──
    /// <summary>原子取整帧。通过既有 EngineHost_Fetch 使用，不新增 export。</summary>
    public const int EngineHostOpGetPresentationFrame = 28;

    // ── HRC-3 D3：pick（**两段式**：Dispatch(PICK) → PumpOneFrame → Fetch(PICK_RESULT)）──
    /// <summary>
    /// 提交一次 pick 请求。通过既有 EngineHost_Dispatch，不新增 export。
    /// 请求经 floatArg[0]=x, floatArg[1]=y 传入；受理后返回的
    /// RuntimeResult.OperationId 即后续 packet 的 request_id 配对键。
    /// </summary>
    public const int EngineHostOpPick = 29;
    public const int EngineHostOpCount = 30;

    // ── HRC-3 D3：PickResultPacket 静态容量（编译期常量）──
    /// <summary>packet 定长 32B。pick 只需一个像素 readback，不把 ID 表面搬过 ABI。</summary>
    public const int EngineHostPickPacketBytes = 32;

    /// <summary>packet 内的 pick 结局码（native <c>ENGINE_HOST_PICK_*</c>）。</summary>
    public const int EngineHostPickHit = 0;
    /// <summary>查询有效、该处无实体（背景 sentinel -1）。这不是失败。</summary>
    public const int EngineHostPickNoHit = 1;
    /// <summary>越界：查询无效。与 <see cref="EngineHostPickNoHit"/> 必须结构化区分。</summary>
    public const int EngineHostPickOutsideViewport = 2;
    /// <summary>坐标非法（NaN / Inf / 无效值）。</summary>
    public const int EngineHostPickInvalidCoordinate = 3;
    /// <summary>未启用 pick pass / 非 owner thread / 无 GL context。</summary>
    public const int EngineHostPickNotReady = 4;
    /// <summary>host faulted 或 teardown 中。</summary>
    public const int EngineHostPickFault = 5;

    // ── HRC-3 P2：presentation target 尺寸与 packet 静态容量（编译期常量）──
    public const int EngineHostPresentationWidth = 800;
    public const int EngineHostPresentationHeight = 600;
    public const int EngineHostFramePacketHeaderBytes = 16;
    public const int EngineHostFramePacketBytes =
        EngineHostFramePacketHeaderBytes +
        EngineHostPresentationWidth * EngineHostPresentationHeight * 4;


    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct RuntimeResult
    {
        public uint StructSize;
        public uint AbiVersion;
        public int Status;
        public int Stage;
        public ulong OperationId;
        public ulong FrameId;
        public ulong Generation;
        public ulong OwnerThreadId;
        public int StateBefore;
        public int StateAfter;
        public int ErrorCode;
        public uint DiagnosticLength;
        public uint DiagnosticTruncated;
        public float RawDtSeconds;
        public float EffectiveDtSeconds;
    }

    /// <summary>
    /// HRC-3 D3 typed pick 结果。
    ///
    /// native 侧位于 <c>#pragma pack(push, 1)</c> 内，因此镜像同样使用
    /// <c>Pack = 1</c>（与 <see cref="RuntimeResult"/> 镜像同理）。
    /// 本布局在 Pack=1 与 Pack=8 下 offset 恰好一致，但**不依赖该巧合**：
    /// D3-Managed gate 以 <c>Marshal.SizeOf</c> / <c>Marshal.OffsetOf</c>
    /// 的**运行时实测值**作为权威证据，而不是结构体属性本身。
    ///
    /// 期望：size=32，offset(status)=4，offset(frame_id)=8，
    /// offset(request_id)=16，offset(entity_id)=24，offset(reserved)=28。
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct PickResultPacket
    {
        /// <summary>必须 == sizeof(PickResultPacket) == 32。</summary>
        public uint StructSize;
        /// <summary>EngineHostPick* 之一。</summary>
        public int Status;
        /// <summary>产生该结果的那一帧。必带（D3-C4）。</summary>
        public ulong FrameId;
        /// <summary>配对键，必须 == Dispatch 返回的 OperationId。</summary>
        public ulong RequestId;
        /// <summary>仅当 Status == Hit 时有效，且必须 &gt;= 1。</summary>
        public uint EntityId;
        /// <summary>保留，必须为 0。</summary>
        public uint Reserved;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct EditorSessionRuntimeStats
    {
        public uint StructSize;
        public uint StopCount;
        public uint OnCreateCount;
        public uint OnDestroyCount;
        public uint GameplayHandleCount;
        public int Playing;
        public int RuntimeScenePresent;
        public int ScriptValid;
        public int CallbackAttached;
        public int GameplayScenePresent;
        public int HudDirty;
        public int SessionPresent;
        public int AllocatorPresent;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void EditorEventCallback(int eventType,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string payload, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void EngineHostEventCallback(int eventType, ulong generation,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string payload, IntPtr userData);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorBridge_GetVersion();

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorBridge_RegisterMainThread();

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_GetApiVersion();

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr EngineHost_Create(ref RuntimeResult result);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_OpenProject(IntPtr host,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string manifestPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string scenePath,
        ref RuntimeResult result);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_Play(IntPtr host, int assetIndex,
        ref RuntimeResult result);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_Reload(IntPtr host,
        ref RuntimeResult result);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_Stop(IntPtr host,
        ref RuntimeResult result);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_PumpOneFrame(IntPtr host,
        float requestedDtSeconds, ref RuntimeResult result);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_CloseProject(IntPtr host,
        ref RuntimeResult result);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_Shutdown(IntPtr host,
        ref RuntimeResult result);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EngineHost_Destroy(IntPtr host);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_GetLastFrameResult(IntPtr host,
        ref RuntimeResult result);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_SetEventCallback(IntPtr host,
        EngineHostEventCallback? callback, IntPtr userData);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_GetLastDiagnostic(IntPtr host,
        IntPtr outBuffer, int capacity);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_GetGraphicsState(IntPtr host,
        out int ready, out int current);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_GetGraphicsCounters(IntPtr host,
        out ulong makeCurrent, out ulong uncurrent);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_GetGlobalProofState(IntPtr host,
        out int applicationPresent, out int timeInitialized,
        out int sceneInitialized, out int jobSystemPresent,
        out int resourceManagerPresent, out int fileWatcherPresent);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_GetSessionProofState(
        IntPtr host, ref EditorSessionRuntimeStats stats);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_GetRuntimeProofState(IntPtr host,
        out int applicationReady, out int fboReady, out int sessionAttached,
        out int readbackVerified, out uint fboId, out uint fboWidth,
        out uint fboHeight, out ulong frameCount, out ulong tickCount);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_GetTeardownState(IntPtr host,
        out int emergencyRequested, out int emergencyCompleted,
        out int graphicsTeardownAttempted, out int graphicsTeardownSucceeded,
        out int lifecycleTimedOut);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_GetGlfwLifetimeState(out int users,
        out int initCount, out int terminateCount);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_Dispatch(IntPtr host, int op,
        int arg0, int arg1, string? strArg, string? strArg2, string? strArg3,
        float[]? floatArg, ref RuntimeResult result);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_Fetch(IntPtr host, int op, int arg0,
        string? strArg, string? strArg2, string? strArg3,
        [Out] byte[] outBuffer, int capacity, ref RuntimeResult result);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_PollPendingEdit(IntPtr host, long editId,
        ref RuntimeResult result);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EngineHost_GetPendingEditProofState(IntPtr host,
        out int accepted, out int executed, out int failed,
        out int lastExecutedWithContextCurrent, out ulong lastExecutedFrameId);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr EditorSession_Create();

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_Destroy(IntPtr session);

    [return: MarshalAs(UnmanagedType.I1)]
    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern bool EditorSession_OpenProject(IntPtr session,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string manifestPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string scenePath);

    [return: MarshalAs(UnmanagedType.I1)]
    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern bool EditorSession_SaveProject(IntPtr session);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetEntityCount(IntPtr session);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetAssetCount(IntPtr session);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_CreateEntity(IntPtr session,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? name);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetEntityName(IntPtr session,
        int index, IntPtr outBuffer, int capacity);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetEntityPosition(IntPtr session,
        int index, IntPtr outPos3);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_SetEntityPosition(IntPtr session,
        int index, IntPtr pos3);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetAssetPath(IntPtr session,
        int index, IntPtr outBuffer, int capacity);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetAssetType(IntPtr session,
        int index);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_DeleteEntity(IntPtr session,
        int index);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_RenameEntity(IntPtr session,
        int index, [MarshalAs(UnmanagedType.LPUTF8Str)] string newName);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetEntitySprite(IntPtr session,
        int index, IntPtr outBuffer, int capacity);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_AssignSprite(IntPtr session,
        int assetIndex, int entityIndex);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetEntityScript(IntPtr session,
        int index, IntPtr outBuffer, int capacity);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_AssignScript(IntPtr session,
        int assetIndex, int entityIndex);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetAssetGuid(IntPtr session,
        int index, IntPtr outBuffer, int capacity);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_ImportAsset(IntPtr session,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string path, int type);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_RenameAsset(IntPtr session,
        int assetIndex, [MarshalAs(UnmanagedType.LPUTF8Str)] string newName);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_ScriptRead(IntPtr session,
        int assetIndex, IntPtr outBuffer, int capacity);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_ScriptSave(IntPtr session,
        int assetIndex, [MarshalAs(UnmanagedType.LPUTF8Str)] string text);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_IsDirty(IntPtr session);

    // ── Phase 3-D (P3-D)：运行时 Play / Reload / Stop ──

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_Play(IntPtr session, int assetIndex);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_Reload(IntPtr session);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_Stop(IntPtr session);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_RuntimeTick(IntPtr session, float dt);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetRuntimeStats(
        IntPtr session, ref EditorSessionRuntimeStats stats);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_IsPlaying(IntPtr session);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetRuntimeError(IntPtr session,
        IntPtr outBuffer, int capacity);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_RuntimePersistInt(IntPtr session,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string key, int defaultVal);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_SetEventCallback(IntPtr session,
        EditorEventCallback? callback, IntPtr userData);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetLastError(IntPtr session,
        IntPtr outBuffer, int capacity);

    // ── Phase 3-G (AV-G3)：OpenProject 非致命告警（缺失 GUID 资产，实体保留）──
    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetWarnings(IntPtr session,
        IntPtr outBuffer, int capacity);

    // ── F1 (Component Contract)：契约组件（Camera 等）──
    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_AddComponent(IntPtr session,
        int entityIndex,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string typeName);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_RemoveComponent(IntPtr session,
        int entityIndex,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string typeName);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_HasComponent(IntPtr session,
        int entityIndex,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string typeName);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetComponentProperty(IntPtr session,
        int entityIndex,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string typeName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string propName,
        IntPtr outBuffer, int capacity);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_SetComponentProperty(IntPtr session,
        int entityIndex,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string typeName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string propName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string valueStr);

    // ── Viewport（Phase 7）──

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern uint EditorSession_GetViewportTextureHandle(IntPtr session);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetViewportSize(IntPtr session,
        out int outWidth, out int outHeight);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_SetViewportSize(IntPtr session,
        int width, int height);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_ViewportTick(IntPtr session, float dt);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_SetViewportCameraPosition(IntPtr session,
        float x, float y, float z);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_GetViewportCameraPosition(IntPtr session,
        IntPtr outPos3);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_SetViewportCameraProjection(IntPtr session,
        int type);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_SetViewportCameraZoom(IntPtr session,
        float zoom);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_ViewportMouseMove(IntPtr session,
        float x, float y);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_ViewportMouseButton(IntPtr session,
        int button, int pressed);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_ViewportScroll(IntPtr session,
        float delta);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_ViewportKeyDown(IntPtr session, int key);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_ViewportKeyUp(IntPtr session, int key);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_ViewportPick(IntPtr session,
        float ndcX, float ndcY);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_SetGizmoMode(IntPtr session, int mode);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_SetGizmoSpace(IntPtr session, int space);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_SetGizmoSnap(IntPtr session,
        int enabled, float value);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetGizmoMode(IntPtr session);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetGizmoSpace(IntPtr session);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetGizmoSnapEnabled(IntPtr session);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern float EditorSession_GetGizmoSnapValue(IntPtr session);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_SetViewportGridVisible(IntPtr session,
        int visible);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetViewportGridVisible(IntPtr session);

    // ── Phase 7: Viewport pixel readback ──

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetViewportPixels(IntPtr session,
        IntPtr outBuffer, int bufferSize);

    // ── Phase 7-B: Camera external input mode ──

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_SetCameraExternalInput(IntPtr session,
        int enabled);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetCameraExternalInput(IntPtr session);
}
