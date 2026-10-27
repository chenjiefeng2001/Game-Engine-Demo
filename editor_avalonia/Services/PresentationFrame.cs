using System;
using System.Buffers.Binary;

namespace AvaloniaEditor.Services;

/// <summary>
/// HRC-3 P2：**类型化的 managed presentation frame**。
///
/// 这一层只做**镜像与解码**：native 已经把 frame_id 与像素在同一次 runtime-thread
/// Submit 里取好并打包，managed 只按**冻结的 offset** 解码。
///
/// 明确不做（分层纪律）：
///   - 不再读 FBO、不再调用任何 GL；
///   - 不自己生成 frame id（frame_id 一律来自 packet header）；
///   - 不缓存"最近一帧"再冒充新帧；
///   - 不修改 pump、不等待帧完成、不引入第二条 transport；
///   - 不涉及 WriteableBitmap / ViewportImage（那是 P2 UI proof 层）。
///
/// 解码方式：**显式 offset 逐字段读取**（BinaryPrimitives，小端），
/// 刻意**不**把 buffer reinterpret 成 managed struct —— 避免 packing/alignment
/// 把 native 的 packet 布局与 managed ABI 偶合在一起。
/// </summary>
public sealed class PresentationFrame
{
    public const int HeaderBytes = Native.EditorBridgeApi.EngineHostFramePacketHeaderBytes;

    /// <summary>
    /// 分配容量（最大尺寸）。v1.5 / Slice E 起它是**容量**而不是"帧的大小"：
    /// 帧的真实尺寸一律来自 packet header。
    /// </summary>
    public const int CapacityBytes = Native.EditorBridgeApi.EngineHostFramePacketBytes;

    /// <summary>默认帧尺寸（未 resize 时的尺寸）。**不是**帧尺寸的权威来源。</summary>
    public const int ExpectedWidth = Native.EditorBridgeApi.EngineHostPresentationDefaultWidth;
    public const int ExpectedHeight = Native.EditorBridgeApi.EngineHostPresentationDefaultHeight;

    /// <summary>协议允许的最大帧尺寸；超出即拒绝（不截断）。</summary>
    public const int MaxWidth = Native.EditorBridgeApi.EngineHostPresentationMaxWidth;
    public const int MaxHeight = Native.EditorBridgeApi.EngineHostPresentationMaxHeight;

    /// <summary>给定帧尺寸所需的确切字节数。</summary>
    public static int RequiredBytesFor(int width, int height)
        => Native.EditorBridgeApi.FramePacketBytesFor(width, height);

    public int Width { get; }
    public int Height { get; }
    public ulong FrameId { get; }

    /// <summary>RGBA8888，行优先、左下角起（GL 原生布局）。长度 = Width*Height*4。</summary>
    public byte[] Rgba { get; }

    private PresentationFrame(int width, int height, ulong frameId, byte[] rgba)
    {
        Width = width;
        Height = height;
        FrameId = frameId;
        Rgba = rgba;
    }

    /// <summary>
    /// 供 UI proof gate 构造一个**指定** identity 的帧（用于"迟到旧帧必须被拒绝"的乱序回放测试）。
    /// 正常生产路径不会用到它 —— 帧一律来自 native packet。
    /// </summary>
    public static PresentationFrame ForOrderedReplayProbe(int width, int height, ulong frameId, byte[] rgba)
        => new(width, height, frameId, rgba);

    public int PayloadOffset => HeaderBytes;
    public int PayloadLength => Rgba.Length;

    /// <summary>
    /// 按冻结 offset 解码。失败时返回 false 并给出原因，**绝不返回半成品帧**。
    /// </summary>
    public static bool TryDecode(byte[] buffer, int length, out PresentationFrame? frame, out string error)
    {
        frame = null;
        error = string.Empty;

        if (buffer is null) { error = "packet buffer is null"; return false; }
        if (length < HeaderBytes)
        {
            error = $"packet is shorter than the {HeaderBytes}B header (got {length})";
            return false;
        }

        // ── 显式 offset 解码（不使用 reinterpret cast）──
        int width = BinaryPrimitives.ReadInt32LittleEndian(buffer.AsSpan(0, 4));
        int height = BinaryPrimitives.ReadInt32LittleEndian(buffer.AsSpan(4, 4));
        ulong frameId = BinaryPrimitives.ReadUInt64LittleEndian(buffer.AsSpan(8, 8));

        // Slice E：尺寸的**唯一**来源是 header。先做范围校验，再做乘法 ——
        // 顺序不能反：先乘会让损坏 header（如 w=int.MaxValue）静默溢出，
        // 得到一个"看起来合法"的小 payload 长度。
        if (width <= 0 || height <= 0)
        {
            error = $"frame size must be positive, got {width}x{height}";
            return false;
        }
        if (width > MaxWidth || height > MaxHeight)
        {
            error = $"frame size {width}x{height} exceeds the supported maximum "
                  + $"{MaxWidth}x{MaxHeight}";
            return false;
        }
        if (frameId == 0)
        {
            error = "frame_id must correspond to a completed frame, got 0";
            return false;
        }

        // 范围已收敛 ⇒ 该乘法不会溢出（最大 1920*1080*4）。
        int expectedPayload = width * height * 4;
        int available = length - HeaderBytes;
        if (available < expectedPayload)
        {
            // 截断：**不**读取越界内存，也不返回半成品帧。
            error = $"truncated packet: payload needs {expectedPayload}B, got {available}B";
            return false;
        }
        if (length > HeaderBytes + expectedPayload)
        {
            // 多余的尾巴只可能来自"按最大容量分配的复用缓冲"，是**预期**的：
            // 调用方必须按 header 的尺寸使用前 expectedPayload 字节。
            // 这里刻意不报错，但也不把它当作 payload。
        }

        var rgba = new byte[expectedPayload];
        Array.Copy(buffer, HeaderBytes, rgba, 0, expectedPayload);
        frame = new PresentationFrame(width, height, frameId, rgba);
        return true;
    }

    /// <summary>统计非 clear 像素（仅用于 gate 证据，不参与生产语义）。</summary>
    public int CountNonClearPixels(byte clearR, byte clearG, byte clearB, int tolerance = 2)
    {
        int count = 0;
        for (int i = 0; i + 3 < Rgba.Length; i += 4)
        {
            int r = Rgba[i], g = Rgba[i + 1], b = Rgba[i + 2];
            bool isClear = Math.Abs(r - clearR) <= tolerance &&
                           Math.Abs(g - clearG) <= tolerance &&
                           Math.Abs(b - clearB) <= tolerance;
            if (!isClear) ++count;
        }
        return count;
    }

    public override string ToString()
        => $"PresentationFrame({Width}x{Height}, frameId={FrameId}, rgba={Rgba.Length}B)";
}

/// <summary>一次 fetch 的结果：有新帧 / 没有新帧。**没有新帧时 <see cref="Frame"/> 必为 null。</summary>
public sealed class FrameFetchResult
{
    private FrameFetchResult(bool hasNewFrame, PresentationFrame? frame, int status)
    {
        HasNewFrame = hasNewFrame;
        Frame = frame;
        Status = status;
    }

    public bool HasNewFrame { get; }

    /// <summary>类型化帧；仅当 <see cref="HasNewFrame"/> 为 true 时非 null。</summary>
    public PresentationFrame? Frame { get; }

    public int Status { get; }

    public static FrameFetchResult NoNewFrame(int status) => new(false, null, status);
    public static FrameFetchResult New(PresentationFrame frame, int status) => new(true, frame, status);
}
