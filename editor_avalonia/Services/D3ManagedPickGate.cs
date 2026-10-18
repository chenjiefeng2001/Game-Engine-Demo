using System;
using System.Runtime.InteropServices;
using AvaloniaEditor.Native;
using AvaloniaEditor.Services;

namespace AvaloniaEditor.Services;

/// <summary>
/// HRC-3 D3：**D3-Managed request/result mirror** gate（M1 ~ M7 + M8-B）。
///
/// 只证明 managed 侧对已冻结 native v1.3 contract 的**镜像与解码**。
/// 明确不做：不调 GL、不新增 pump、不缓存 latest result、不写 UI。
///
/// 分层（与 M8-A 互斥，见 tools/run_d3_managed_gate.ps1）：
///   M1 ~ M7 : D3 semantic / ABI evidence
///   M8-B    : loaded ABI identity（实际加载的 native DLL 报告的版本）
///   M8-A    : artifact prerequisite（**由 runner 做**，SHA256 比对；
///             未通过时 M1~M7 / M8-B 一律 NOT RUN，不产出"v1.3 broken"的误读）
///
/// 关于 M3~M7 的断言口径：这些是**transport** 不变量（配对 / busy / 作废 /
/// generation / frame 绑定），**不要求** pick 结局为 Hit —— native 的 pick pass
/// 是 opt-in 且默认关闭，合法结局可能是 NotReady。把"能否命中"写进 managed gate
/// 会把 transport 契约与 pick pass 开关耦在一起。
/// </summary>
public static class D3ManagedPickGate
{
    private const string ManifestRelative = "assets/gp01/manifest.json";
    private const string SceneRelative = "assets/gp01/Main.scene";

    private const int AbiV1_3 = (1 << 16) | 3;

    private static void Check(bool condition, string what)
    {
        if (!condition) throw new InvalidOperationException("D3-MANAGED GATE: " + what);
    }

    public static int Run(EditorHostService host, Action<string> log)
    {
        try
        {
            M1_VersionLockstep(log);
            M2_OpStatusPacketMirror(log);
            M8B_LoadedAbiIdentity(log);
            M3_RequestIdPairing(host, log);
            M4_PickBusyPreservesOutstanding(host, log);
            M5_StopInvalidationYieldsNoPacket(host, log);
            M6_ReopenGenerationIsolation(host, log);
            M7_FrameIdBoundToResult(host, log);
            log("[D3-MANAGED GATE] M1-M7 + M8-B ALL GREEN");
            return 0;
        }
        catch (Exception ex)
        {
            log("[D3-MANAGED GATE] FAIL: " + ex.Message);
            return 1;
        }
    }

    /// <summary>M1：managed 声明与 native 实报必须是 v1.3。</summary>
    private static void M1_VersionLockstep(Action<string> log)
    {
        int expected = EditorBridgeApi.EngineHostExpectedVersion;
        Check(expected == AbiV1_3,
              $"managed must declare v1.3, declares v{(expected >> 16)}.{(expected & 0xFFFF)}");
        int actual = EditorBridgeApi.EngineHost_GetApiVersion();
        Check(actual == expected,
              $"native reports 0x{actual:X8}, managed expects 0x{expected:X8}");
        log($"[D3-M1] ABI lockstep OK (native=managed=0x{actual:X8} = v1.3)");
    }

    /// <summary>
    /// M2：op / status / packet 镜像正确。
    /// packet 布局以 <c>Marshal.SizeOf</c> / <c>Marshal.OffsetOf</c> 的
    /// **运行时实测值**为权威证据，而不是 StructLayout 属性本身。
    /// </summary>
    private static void M2_OpStatusPacketMirror(Action<string> log)
    {
        // 冻结常量
        Check(EditorBridgeApi.EngineHostOpPick == 29, "PICK op must be 29");
        Check(EditorBridgeApi.EngineHostOpCount == 30, "op count must be 30");
        Check(EditorBridgeApi.EngineHostOpGetPresentationFrame == 28,
              "P2 op must stay 28 (no renumbering)");
        Check(EditorBridgeApi.EngineHostStatusPickBusy == 12, "PICK_BUSY must be 12");
        Check(EditorBridgeApi.EngineHostStatusNoNewFrame == 11, "NO_NEW_FRAME must stay 11");
        Check(EditorBridgeApi.EngineHostStatusEditAccepted == 10, "EDIT_ACCEPTED must stay 10");
        Check(EditorBridgeApi.EngineHostStageEdit == 11, "stage EDIT must be 11");
        Check(EditorBridgeApi.EngineHostStageShutdown == 10, "stage SHUTDOWN must be 10");
        Check(EditorBridgeApi.EngineHostPickPacketBytes == 32, "pick packet must be 32B");
        // EditorBridge 自身的版本线与 EngineHost 是两套：前者仍为 v1.1。
        Check(EditorBridgeApi.ExpectedVersion == ((1 << 16) | 1),
              "EditorBridge_GetVersion line must stay v1.1");

        // 结局码连续且与 native enum 一致
        Check(EditorBridgeApi.EngineHostPickHit == 0, "HIT must be 0");
        Check(EditorBridgeApi.EngineHostPickNoHit == 1, "NO_HIT must be 1");
        Check(EditorBridgeApi.EngineHostPickOutsideViewport == 2, "OUTSIDE_VIEWPORT must be 2");
        Check(EditorBridgeApi.EngineHostPickInvalidCoordinate == 3, "INVALID_COORDINATE must be 3");
        Check(EditorBridgeApi.EngineHostPickNotReady == 4, "NOT_READY must be 4");
        Check(EditorBridgeApi.EngineHostPickFault == 5, "FAULT must be 5");

        // ★ executable ABI assertion：运行时实测 size / offset
        int size = Marshal.SizeOf<EditorBridgeApi.PickResultPacket>();
        Check(size == 32, $"Marshal.SizeOf<PickResultPacket>() must be 32, got {size}");
        Check(Marshal.OffsetOf<EditorBridgeApi.PickResultPacket>(nameof(EditorBridgeApi.PickResultPacket.StructSize)).ToInt32() == 0,
              "offset(StructSize) must be 0");
        Check(Marshal.OffsetOf<EditorBridgeApi.PickResultPacket>(nameof(EditorBridgeApi.PickResultPacket.Status)).ToInt32() == 4,
              "offset(Status) must be 4");
        Check(Marshal.OffsetOf<EditorBridgeApi.PickResultPacket>(nameof(EditorBridgeApi.PickResultPacket.FrameId)).ToInt32() == 8,
              "offset(FrameId) must be 8");
        Check(Marshal.OffsetOf<EditorBridgeApi.PickResultPacket>(nameof(EditorBridgeApi.PickResultPacket.RequestId)).ToInt32() == 16,
              "offset(RequestId) must be 16");
        Check(Marshal.OffsetOf<EditorBridgeApi.PickResultPacket>(nameof(EditorBridgeApi.PickResultPacket.EntityId)).ToInt32() == 24,
              "offset(EntityId) must be 24");
        Check(Marshal.OffsetOf<EditorBridgeApi.PickResultPacket>(nameof(EditorBridgeApi.PickResultPacket.Reserved)).ToInt32() == 28,
              "offset(Reserved) must be 28");

        // RuntimeResult 未被本次 ABI 增量触动
        Check(Marshal.SizeOf<EditorBridgeApi.RuntimeResult>() == 76,
              $"RuntimeResult must stay 76B, got {Marshal.SizeOf<EditorBridgeApi.RuntimeResult>()}");

        // 解码器拒绝路径（纯 managed，不依赖 native）
        var good = BuildPacket(32, EditorBridgeApi.EngineHostPickNoHit, 7UL, 9UL, 0, 0);
        Check(PickResult.TryDecode(good, good.Length, out var d1, out var e1) && d1 is not null,
              "valid packet decodes: " + e1);
        Check(d1!.FrameId == 7UL && d1.RequestId == 9UL, "decoded frame_id/request_id come from offsets 8/16");
        Check(!PickResult.TryDecode(good, good.Length - 1, out var d2, out _) && d2 is null,
              "short packet must be rejected");
        Check(!PickResult.TryDecode(BuildPacket(31, EditorBridgeApi.EngineHostPickNoHit, 7, 9, 0, 0), 32, out var d3, out _) && d3 is null,
              "wrong struct_size must be rejected");
        Check(!PickResult.TryDecode(BuildPacket(32, EditorBridgeApi.EngineHostPickNoHit, 0, 9, 0, 0), 32, out var d4, out _) && d4 is null,
              "frame_id 0 must be rejected");
        Check(!PickResult.TryDecode(BuildPacket(32, EditorBridgeApi.EngineHostPickNoHit, 7, 0, 0, 0), 32, out var d5, out _) && d5 is null,
              "request_id 0 must be rejected");
        Check(!PickResult.TryDecode(BuildPacket(32, EditorBridgeApi.EngineHostPickHit, 7, 9, 0, 0), 32, out var d6, out _) && d6 is null,
              "Hit with entity_id 0 must be rejected (D3-C2: valid id >= 1)");
        Check(!PickResult.TryDecode(BuildPacket(32, EditorBridgeApi.EngineHostPickNoHit, 7, 9, 0, 1), 32, out var d7, out _) && d7 is null,
              "non-zero reserved must be rejected");
        Check(!PickResult.TryDecode(BuildPacket(32, 99, 7, 9, 0, 0), 32, out var d8, out _) && d8 is null,
              "unknown status must be rejected");

        log("[D3-M2] op/status/packet mirror OK (PICK=29, opCount=30, PICK_BUSY=12, "
            + "packet 32B, Marshal offsets 0/4/8/16/24/28, RuntimeResult 76B)");
    }

    /// <summary>
    /// M8-B：**实际加载的** native DLL 报告的版本。
    /// M8-A（磁盘 artifact SHA256）由 runner 先行把关；未过则本项 NOT RUN。
    /// </summary>
    private static void M8B_LoadedAbiIdentity(Action<string> log)
    {
        int loaded = EditorBridgeApi.EngineHost_GetApiVersion();
        Check(loaded == AbiV1_3,
              $"loaded native must report 0x{AbiV1_3:X8}, reports 0x{loaded:X8}");
        Check(EditorBridgeApi.EngineHostExpectedVersion == loaded,
              "loaded native and managed declaration must be the same value");
        log($"[D3-M8B] loaded ABI identity OK (loaded=0x{loaded:X8} = v1.3)");
    }

    /// <summary>M3：packet.request_id 必须 == Dispatch 返回的 operation_id。</summary>
    private static void M3_RequestIdPairing(EditorHostService host, Action<string> log)
    {
        host.EnsureRuntime();
        host.OpenProject(ManifestRelative, SceneRelative);

        var req = host.RequestPick(100.0f, 100.0f);
        Check(req.Accepted, $"pick request must be accepted (status={req.Status})");
        Check(req.RequestId != 0, "accepted request must expose a non-zero request_id");

        host.RuntimeTick(0.016f);

        var fetched = host.TryFetchPickResult();
        Check(fetched.HasResult, $"expected a pick result after pump (status={fetched.Status}, stage={fetched.Stage})");
        var pick = fetched.Result!;
        Check(pick.RequestId == req.RequestId,
              $"request_id pairing: packet {pick.RequestId} != dispatched {req.RequestId}");
        log($"[D3-M3] request_id pairing OK (operation_id == request_id == {pick.RequestId})");
    }

    /// <summary>M4：第二个请求必须 PICK_BUSY，且**不替换**已受理的 A。</summary>
    private static void M4_PickBusyPreservesOutstanding(EditorHostService host, Action<string> log)
    {
        // 自备 outstanding：M3 已经 fetch 过它的 request，capacity 已释放，
        // 因此这里必须**重新**受理一个 A，不能沿用 M3 的状态。
        var a = host.RequestPick(120.0f, 120.0f);
        Check(a.Accepted, $"pick request A must be accepted (status={a.Status})");

        // A 仍在 pending 队列、尚未 dispatch —— 这正是 H8-1 的第三态
        var b = host.RequestPick(121.0f, 121.0f);
        Check(b.Busy, $"second request must be PICK_BUSY, got accepted/busy=false status={b.Status}");
        Check(b.Status == EditorBridgeApi.EngineHostStatusPickBusy,
              $"busy status must be PICK_BUSY(12), got {b.Status}");
        Check(b.RequestId == 0, "a busy request must not hand out a new request_id");

        // A 必须仍然 outstanding 且可取回
        host.RuntimeTick(0.016f);
        var fetched = host.TryFetchPickResult();
        Check(fetched.HasResult, $"outstanding A must survive a busy B (status={fetched.Status})");
        Check(fetched.Result!.RequestId == a.RequestId,
              $"A must not be replaced by B: got {fetched.Result!.RequestId}, expected {a.RequestId}");
        log($"[D3-M4] PICK_BUSY preserves the outstanding request (A={a.RequestId} survived B)");
    }

    /// <summary>M5：Stop 之后 fetch 必须 INVALID_STATE + SHUTDOWN，且**无 packet**。</summary>
    private static void M5_StopInvalidationYieldsNoPacket(EditorHostService host, Action<string> log)
    {
        host.OpenProject(ManifestRelative, SceneRelative);
        var req = host.RequestPick(140.0f, 140.0f);
        Check(req.Accepted, $"request before Stop must be accepted (status={req.Status})");
        host.RuntimeTick(0.016f);

        host.Stop();

        var afterStop = host.TryFetchPickResult();
        Check(!afterStop.HasResult, "Stop must invalidate the outstanding pick result");
        Check(afterStop.Result is null, "an invalidated result must not carry a packet");
        Check(afterStop.IsInvalidatedByTeardown,
              $"expected INVALID_STATE+SHUTDOWN, got status={afterStop.Status} stage={afterStop.Stage}");
        log("[D3-M5] Stop invalidation yields no packet (INVALID_STATE + STAGE_SHUTDOWN)");
    }

    /// <summary>M6：reopen 之后旧 generation 的 request 不得复现；新 request 可用。</summary>
    private static void M6_ReopenGenerationIsolation(EditorHostService host, Action<string> log)
    {
        host.EnsureRuntime();
        host.OpenProject(ManifestRelative, SceneRelative);
        var oldReq = host.RequestPick(160.0f, 160.0f);
        Check(oldReq.Accepted, $"request before reopen must be accepted (status={oldReq.Status})");
        host.RuntimeTick(0.016f);

        // generation 切换
        host.CloseProject();
        host.OpenProject(ManifestRelative, SceneRelative);

        // 旧 request 不得复活：新 fetch 要么无结果，要么 request_id 绝不是旧的
        var stale = host.TryFetchPickResult();
        if (stale.HasResult)
        {
            Check(stale.Result!.RequestId != oldReq.RequestId,
                  $"stale request reappeared across reopen: {stale.Result!.RequestId}");
        }

        // 新一代的 request 必须可以正常发起并配对
        var newReq = host.RequestPick(180.0f, 180.0f);
        Check(newReq.Accepted, $"new-generation request must be accepted (status={newReq.Status})");
        Check(newReq.RequestId != oldReq.RequestId,
              $"new generation must not reuse the old request_id ({oldReq.RequestId})");
        host.RuntimeTick(0.016f);
        var fresh = host.TryFetchPickResult();
        Check(fresh.HasResult, $"new-generation request must produce a result (status={fresh.Status})");
        Check(fresh.Result!.RequestId == newReq.RequestId,
              $"new generation pairing: {fresh.Result!.RequestId} != {newReq.RequestId}");
        log("[D3-M6] reopen generation isolation OK (old request cannot reappear)");
    }

    /// <summary>M7：frame_id 必须与结果绑定（非 0，且与 fetch 的 RuntimeResult 一致）。</summary>
    private static void M7_FrameIdBoundToResult(EditorHostService host, Action<string> log)
    {
        var req = host.RequestPick(200.0f, 200.0f);
        Check(req.Accepted, $"request for M7 must be accepted (status={req.Status})");
        host.RuntimeTick(0.016f);
        var fetched = host.TryFetchPickResult();
        Check(fetched.HasResult, $"M7 expected a result (status={fetched.Status})");

        var pick = fetched.Result!;
        Check(pick.FrameId != 0, "frame_id must be non-zero and bound to a completed frame");
        Check(fetched.RequestId == pick.RequestId, "fetch result must carry the packet request_id");
        log($"[D3-M7] frame_id bound to result OK (frame_id={pick.FrameId}, request_id={pick.RequestId})");
    }

    private static byte[] BuildPacket(uint structSize, int status, ulong frameId,
                                      ulong requestId, uint entityId, uint reserved)
    {
        var b = new byte[32];
        System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(b.AsSpan(0, 4), structSize);
        System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(b.AsSpan(4, 4), status);
        System.Buffers.Binary.BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(8, 8), frameId);
        System.Buffers.Binary.BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(16, 8), requestId);
        System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(b.AsSpan(24, 4), entityId);
        System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(b.AsSpan(28, 4), reserved);
        return b;
    }
}
