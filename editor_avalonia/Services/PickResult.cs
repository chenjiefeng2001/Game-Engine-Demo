using System;
using AvaloniaEditor.Native;

namespace AvaloniaEditor.Services;

/// <summary>
/// HRC-3 D3 typed pick 结果（managed 镜像层）。
///
/// 这一层**只镜像/解码** native <c>PickResultPacket</c>：不读 FBO、不调 GL、
/// 不生成 frame_id、不缓存旧结果冒充当前 request、不改 pump、不引入第二条 transport。
///
/// 解码按**冻结 offset** 逐字段进行，并对每个不变量 fail-closed：
/// size、struct_size、frame_id != 0、request_id != 0、
/// 以及 Hit 时 entity_id &gt;= 1（D3-C2 有效 ID 约束）。
/// </summary>
public sealed class PickResult
{
    /// <summary>packet 定长字节数（native <c>sizeof(PickResultPacket)</c>）。</summary>
    public const int PacketBytes = 32;

    // 冻结 offset。权威证据是 M2 gate 的 Marshal.SizeOf / Marshal.OffsetOf 实测值，
    // 这些常量只是"解码时按哪一刀切"的意图声明。
    public const int OffsetStructSize = 0;
    public const int OffsetStatus = 4;
    public const int OffsetFrameId = 8;
    public const int OffsetRequestId = 16;
    public const int OffsetEntityId = 24;
    public const int OffsetReserved = 28;

    private PickResult(int status, ulong frameId, ulong requestId, uint entityId)
    {
        Status = status;
        FrameId = frameId;
        RequestId = requestId;
        EntityId = entityId;
    }

    /// <summary><c>EngineHostPick*</c> 之一。</summary>
    public int Status { get; }

    /// <summary>产生该结果的那一帧（D3-C4 必带）。</summary>
    public ulong FrameId { get; }

    /// <summary>配对键，必须 == Dispatch 返回的 OperationId。</summary>
    public ulong RequestId { get; }

    /// <summary>仅当 <see cref="IsHit"/> 时有效，且必须 &gt;= 1。</summary>
    public uint EntityId { get; }

    public bool IsHit => Status == EditorBridgeApi.EngineHostPickHit;
    public bool IsNoHit => Status == EditorBridgeApi.EngineHostPickNoHit;
    public bool IsOutsideViewport => Status == EditorBridgeApi.EngineHostPickOutsideViewport;
    public bool IsInvalidCoordinate => Status == EditorBridgeApi.EngineHostPickInvalidCoordinate;
    public bool IsNotReady => Status == EditorBridgeApi.EngineHostPickNotReady;
    public bool IsFault => Status == EditorBridgeApi.EngineHostPickFault;

    /// <summary>
    /// 严格解码。任何不变量违例都 <c>false</c> + error，**不返回半成品**。
    /// length 是 native 实际写入的字节数。
    /// </summary>
    public static bool TryDecode(byte[] buffer, int length,
                                 out PickResult? result, out string error)
    {
        result = null;
        error = string.Empty;

        if (buffer is null) { error = "null buffer"; return false; }
        if (length < PacketBytes)
        {
            error = $"packet too short: got {length}B, expected {PacketBytes}B";
            return false;
        }

        uint structSize = ReadU32(buffer, OffsetStructSize);
        if (structSize != PacketBytes)
        {
            // native 一定会写 struct_size；不匹配说明布局/版本漂移，必须 fail-closed。
            error = $"struct_size mismatch: got {structSize}, expected {PacketBytes}";
            return false;
        }

        int status = ReadI32(buffer, OffsetStatus);
        if (status < EditorBridgeApi.EngineHostPickHit || status > EditorBridgeApi.EngineHostPickFault)
        {
            error = $"unknown pick status {status}";
            return false;
        }

        ulong frameId = ReadU64(buffer, OffsetFrameId);
        if (frameId == 0)
        {
            // frame_id 必须对应一个已完成的帧，0 不是合法结果（D3-C4）。
            error = "frame_id must correspond to a completed frame, got 0";
            return false;
        }

        ulong requestId = ReadU64(buffer, OffsetRequestId);
        if (requestId == 0)
        {
            error = "request_id must pair with a Dispatch operation_id, got 0";
            return false;
        }

        uint entityId = ReadU32(buffer, OffsetEntityId);
        if (status == EditorBridgeApi.EngineHostPickHit && entityId < 1)
        {
            // D3-C2：有效 ID >= 1，0 保留，背景是 sentinel -1。
            error = $"Hit requires entity_id >= 1, got {entityId}";
            return false;
        }

        uint reserved = ReadU32(buffer, OffsetReserved);
        if (reserved != 0)
        {
            error = $"reserved must be 0, got {reserved}";
            return false;
        }

        result = new PickResult(status, frameId, requestId, entityId);
        return true;
    }

    private static uint ReadU32(byte[] b, int o)
        => (uint)(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24));

    private static int ReadI32(byte[] b, int o) => unchecked((int)ReadU32(b, o));

    private static ulong ReadU64(byte[] b, int o)
        => ReadU32(b, o) | ((ulong)ReadU32(b, o + 4) << 32);

    public override string ToString()
        => $"PickResult(status={Status}, frameId={FrameId}, requestId={RequestId}, entityId={EntityId})";
}

/// <summary>
/// 一次 fetch 的结果。<b>没有结果时 <see cref="Result"/> 必为 null。</b>
///
/// 刻意<b>不</b>缓存 "latest result"：每次 fetch 都重新解码，
/// 绝不用上一次的结果顶替当前 request。
/// </summary>
public sealed class PickFetchResult
{
    private PickFetchResult(bool hasResult, PickResult? result, int status, int stage, ulong requestId)
    {
        HasResult = hasResult;
        Result = result;
        Status = status;
        Stage = stage;
        RequestId = requestId;
    }

    public bool HasResult { get; }

    /// <summary>类型化结果；仅当 <see cref="HasResult"/> 为 true 时非 null。</summary>
    public PickResult? Result { get; }

    public int Status { get; }

    public int Stage { get; }

    /// <summary>与之配对的 Dispatch operation_id；无结果时为 0。</summary>
    public ulong RequestId { get; }

    /// <summary>请求已受理但尚未产出结果（stage = EDIT）。<b>不是</b> teardown 作废。</summary>
    public bool IsNotReady => Status == EditorBridgeApi.EngineHostStatusInvalidState
                              && Stage == EditorBridgeApi.EngineHostStageEdit;

    /// <summary>被 teardown 作废（stage = SHUTDOWN）—— 依冻结契约<b>不产生 packet</b>。</summary>
    public bool IsInvalidatedByTeardown => Status == EditorBridgeApi.EngineHostStatusInvalidState
                                            && Stage == EditorBridgeApi.EngineHostStageShutdown;

    public static PickFetchResult NoResult(int status, int stage) => new(false, null, status, stage, 0);

    public static PickFetchResult New(PickResult result, int status)
        => new(true, result, status, EditorBridgeApi.EngineHostStageEdit, result.RequestId);
}

/// <summary>一次 pick 请求的受理结果。</summary>
public sealed class PickRequestResult
{
    private PickRequestResult(bool accepted, bool busy, ulong requestId, int status)
    {
        Accepted = accepted;
        Busy = busy;
        RequestId = requestId;
        Status = status;
    }

    public bool Accepted { get; }

    /// <summary>capacity=1 已满。<b>不替换</b>已受理的 outstanding request。</summary>
    public bool Busy { get; }

    /// <summary>配对键 == <c>RuntimeResult.OperationId</c>；未受理时为 0。</summary>
    public ulong RequestId { get; }

    public int Status { get; }

    public static PickRequestResult Accepted_(ulong requestId, int status) => new(true, false, requestId, status);

    public static PickRequestResult Busy_(int status) => new(false, true, 0, status);

    public static PickRequestResult Rejected(int status) => new(false, false, 0, status);
}
