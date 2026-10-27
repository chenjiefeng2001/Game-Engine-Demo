using System;
using AvaloniaEditor.Native;
using AvaloniaEditor.Services;

namespace AvaloniaEditor.Services;

/// <summary>
/// HRC-3 P2：**P2-Managed Fetch Mirror** gate（M1 ~ M7）。
///
/// 只证明 managed 侧的**镜像与解码**：native 已经把 frame_id 与像素在同一次
/// runtime-thread Submit 里打包好，本层按冻结 offset 解码。
///
/// 明确不做：不写 WriteableBitmap、不绑定 ViewportImage（那是 P2 UI proof 层）。
///
/// M1 v1.2 version lockstep
/// M2 packet offsets / size 正确
/// M3 frame_id 与 RGBA 同包保持一致
/// M4 NO_NEW_FRAME 不复用旧 frame
/// M5 repeated fetch 可正确解析连续 frame（frame_id 单调推进）
/// M6 native 回归全绿
/// M7 I2 baseline 无静默漂移
/// </summary>
public static class P2ManagedFrameGate
{
    private const string ManifestRelative = "assets/gp01/manifest.json";
    private const string SceneRelative = "assets/gp01/Main.scene";

    // presentation FBO 的 clear color（0.05, 0.05, 0.07, 1.0) → 13/13/18
    private const byte ClearR = 13, ClearG = 13, ClearB = 18;

    private static void Check(bool condition, string what)
    {
        if (!condition) throw new InvalidOperationException("P2-MANAGED GATE: " + what);
    }

    public static int Run(EditorHostService host, Action<string> log)
    {
        try
        {
            M1_VersionLockstep(host, log);
            M2_OffsetsAndSize(log);
            M4_NoNewFrameNeverReusesOldFrame(host, log);
            M3_M5_AtomicAndMonotonicFrames(host, log);
            M6_M7_RegressionAndBaseline(log);
            log("[P2-MANAGED GATE] ALL GREEN");
            return 0;
        }
        catch (Exception ex)
        {
            log("[P2-MANAGED GATE] FAIL: " + ex.Message);
            return 1;
        }
    }

    /// <summary>
    /// M1：managed 声明的 EngineHost 版本必须与 native 实际报告的一致。
    ///
    /// HRC-3 D3 contract migration v1.2 -&gt; v1.3：native 增加
    /// ENGINE_HOST_OP_PICK 与 PICK_BUSY（不新增 export，RuntimeResult 仍 76B），
    /// 因此本断言随之从 v1.2 更新为 v1.3；Slice D / P-1 把 `reserved` 正名为
    /// `entity_index`（ABI v1.4），断言再随之更新为 v1.4。
    /// </summary>
    private static void M1_VersionLockstep(EditorHostService host, Action<string> log)
    {
        int expected = EditorBridgeApi.EngineHostExpectedVersion;
        int actual = EditorBridgeApi.EngineHost_GetApiVersion();
        Check(expected == ((1 << 16) | 5), $"managed expects v1.5, declares v{(expected >> 16)}.{(expected & 0xFFFF)}");
        Check(actual == expected, $"native reports 0x{actual:X8}, managed expects 0x{expected:X8}");
        Check(host.IsEngineHost, "production backend is EngineHost");
        log($"[P2-M1] version lockstep OK (native=managed=0x{actual:X8} = v{(actual >> 16)}.{(actual & 0xFFFF)})");
    }

    /// <summary>
    /// M2：解码器必须按冻结 offset 工作，且在字节数/尺寸不符时**拒绝**而不是半解码。
    /// 这里直接用构造出来的 buffer 验证解码器本身（不依赖 native）。
    /// </summary>
    private static void M2_OffsetsAndSize(Action<string> log)
    {
        // 常量必须与 native 的编译期常量一致
        Check(PresentationFrame.ExpectedWidth == 800, "expected width 800");
        Check(PresentationFrame.ExpectedHeight == 600, "expected height 600");
        Check(PresentationFrame.HeaderBytes == 16, "header is 16 bytes");
        // v1.5：帧的字节数由 header 的尺寸决定；容量按**最大尺寸**分配。
        Check(PresentationFrame.RequiredBytesFor(800, 600) == 16 + 800 * 600 * 4,
              "per-frame size formula");
        Check(PresentationFrame.RequiredBytesFor(800, 600) == 1_920_016,
              "the default frame is still 1,920,016 bytes");
        Check(PresentationFrame.CapacityBytes == 16 + 1920 * 1080 * 4,
              "the allocation capacity follows the maximum size");
        Check(PresentationFrame.MaxWidth == 1920 && PresentationFrame.MaxHeight == 1080,
              "the maximum presentation size is 1920x1080");

        // 造一个合法 packet：offset 0/4/8 分别是 width/height/frame_id
        // payload 先填成 clear color，再把**一个**像素改成非 clear，
        // 这样"非 clear 像素计数"才有精确期望值（否则整块 0 也是非 clear）。
        var good = new byte[PresentationFrame.RequiredBytesFor(800, 600)];
        for (int i = PresentationFrame.HeaderBytes; i + 3 < good.Length; i += 4)
        {
            good[i + 0] = ClearR; good[i + 1] = ClearG; good[i + 2] = ClearB; good[i + 3] = 255;
        }
        WriteI32(good, 0, 800);
        WriteI32(good, 4, 600);
        WriteU64(good, 8, 7UL);
        good[PresentationFrame.HeaderBytes + 0] = 0xAB;   // 第一个像素：非 clear
        Check(PresentationFrame.TryDecode(good, good.Length, out var f1, out var e1) && f1 is not null,
              "valid packet decodes: " + e1);
        Check(f1!.Width == 800 && f1.Height == 600, "decoded size is 800x600");
        Check(f1.FrameId == 7UL, "decoded frame_id comes from offset 8");
        Check(f1.Rgba.Length == 800 * 600 * 4, "decoded payload length is width*height*4");
        Check(f1.Rgba[0] == 0xAB, "payload starts at offset 16");
        Check(f1.Rgba[PresentationFrame.HeaderBytes - PresentationFrame.HeaderBytes + 4] == ClearG,
              "decoded pixels are copied verbatim from the payload region");
        Check(f1.CountNonClearPixels(ClearR, ClearG, ClearB) == 1,
              $"exactly one non-clear pixel expected, got {f1.CountNonClearPixels(ClearR, ClearG, ClearB)}");

        // 拒绝路径：字节数不足 / 尺寸错 / frame_id 为 0 / header 不完整
        Check(!PresentationFrame.TryDecode(good, good.Length - 1, out var f2, out _) && f2 is null,
              "byte-count mismatch must be rejected");
        Check(!PresentationFrame.TryDecode(good, 8, out var f3, out _) && f3 is null,
              "short header must be rejected");
        var wrongSize = (byte[])good.Clone();
        WriteI32(wrongSize, 4, 599);
        Check(!PresentationFrame.TryDecode(wrongSize, wrongSize.Length, out var f4, out _) && f4 is null,
              "wrong height must be rejected");
        var zeroId = (byte[])good.Clone();
        WriteU64(zeroId, 8, 0UL);
        Check(!PresentationFrame.TryDecode(zeroId, zeroId.Length, out var f5, out _) && f5 is null,
              "frame_id 0 must be rejected");
        Check(!PresentationFrame.TryDecode(null!, 0, out var f6, out _) && f6 is null,
              "null buffer must be rejected");
        log("[P2-M2] packet offsets/size + rejection paths OK (header 16B, total 1,920,016B)");
    }

    /// <summary>
    /// M4：没有新帧时**必须**是 HasNewFrame=false / Frame=null，
    /// 绝不能把上一次的帧顶替上来。
    /// </summary>
    private static void M4_NoNewFrameNeverReusesOldFrame(EditorHostService host, Action<string> log)
    {
        host.OpenProject(ManifestRelative, SceneRelative);

        // 尚未 pump 任何帧 ⇒ native 必然回答"没有新帧"
        var first = host.TryFetchPresentationFrame();
        Check(!first.HasNewFrame, "fetch before any frame must report no new frame");
        Check(first.Frame is null, "no-new-frame must not hand back a frame object");
        Check(first.Status == EditorBridgeApi.EngineHostStatusNoNewFrame,
              $"status must be NO_NEW_FRAME, got {first.Status}");

        // 结果类型本身也不允许在没有新帧时携带 frame
        var synthetic = FrameFetchResult.NoNewFrame(EditorBridgeApi.EngineHostStatusNoNewFrame);
        Check(!synthetic.HasNewFrame && synthetic.Frame is null, "NoNewFrame() can never carry a frame");
        log("[P2-M4] NO_NEW_FRAME never reuses an old frame");
    }

    /// <summary>
    /// M3 + M5：连续新帧必须单调推进，且 frame_id 与 RGBA 来自**同一个 packet**。
    /// </summary>
    private static void M3_M5_AtomicAndMonotonicFrames(EditorHostService host, Action<string> log)
    {
        ulong previous = 0;
        int framesWithContent = 0;
        const int frameCount = 3;

        for (int i = 0; i < frameCount; ++i)
        {
            // 走生产 pump 入口（唯一 pump 入口），不新增第二条 tick 路径
            host.RuntimeTick(0.016f);

            var fetched = host.TryFetchPresentationFrame();
            Check(fetched.HasNewFrame, $"frame {i + 1}: expected a new frame after PumpOneFrame");
            var frame = fetched.Frame!;

            // M5：单调推进
            Check(frame.FrameId > previous,
                  $"frame_id must advance monotonically: {frame.FrameId} <= {previous}");
            previous = frame.FrameId;

            // M2/M3：尺寸与 payload
            Check(frame.Width == 800 && frame.Height == 600, $"frame {i + 1}: size must be 800x600");
            Check(frame.Rgba.Length == 800 * 600 * 4, $"frame {i + 1}: payload length");
            Check(frame.FrameId != 0, $"frame {i + 1}: frame_id must be non-zero");

            // M3：像素必须是真实 scene 内容（与 host 报告的 frame_id 同一个 packet）
            int nonClear = frame.CountNonClearPixels(ClearR, ClearG, ClearB);
            Check(nonClear > 0, $"frame {i + 1}: packet must carry real rendered scene content");
            if (nonClear > 0) ++framesWithContent;

            // 不再 pump ⇒ 立刻变成"没有新帧"（M4 复验）
            var again = host.TryFetchPresentationFrame();
            Check(!again.HasNewFrame && again.Frame is null,
                  $"frame {i + 1}: repeated fetch without a new frame must report no new frame");
        }

        Check(framesWithContent == frameCount, "every fetched frame carried real content");
        log($"[P2-M3/M5] {frameCount} consecutive frames decoded; last frame_id={previous}; atomicity + monotonicity OK");
    }

    /// <summary>
    /// M6 + M7：native 回归由外部 runner 复核；这里只做**本地可判定**的完整性检查，
    /// 并且明确不做"静默基线"：把当前观察到的数量打印出来供人工比对 Evidence-Baseline。
    /// </summary>
    private static void M6_M7_RegressionAndBaseline(Action<string> log)
    {
        // managed 侧观察到的常量（不是基线断言，而是把"会漂移的东西"显式化）
        log($"[P2-M6/M7] managed-visible inventory: opGetPresentationFrame={EditorBridgeApi.EngineHostOpGetPresentationFrame} " +
            $"(opCount={EditorBridgeApi.EngineHostOpCount}), statusNoNewFrame={EditorBridgeApi.EngineHostStatusNoNewFrame}, " +
            $"frameBytes={PresentationFrame.RequiredBytesFor(800, 600)}, " +
            $"capacity={PresentationFrame.CapacityBytes}, abi=0x{EditorBridgeApi.EngineHostExpectedVersion:X8}");
        // EditorBridge 自身的版本线与 EngineHost 是两套：前者仍为 v1.1，且必须如此。
        Check(EditorBridgeApi.ExpectedVersion == ((1 << 16) | 1),
              "EditorBridge_GetVersion line must stay v1.1 (independent from EngineHost v1.5)");
        // D3 v1.3/v1.4、Slice E v1.5 都没有给 P2 重新编号：P2 的 op 仍是 28（PICK=29，SET_VIEWPORT_SIZE=30），
        // NO_NEW_FRAME 仍是 11。这两条在 v1.4 下**依然成立**，不得迁移。
        Check(EditorBridgeApi.EngineHostOpGetPresentationFrame == 28, "P2 op must still be 28 (PICK took 29)");
        // HRC-3 D3 contract migration v1.2 -> v1.3：op 空间 +1（PICK）。
        Check(EditorBridgeApi.EngineHostOpCount == 31, "op count must be 31 under v1.5");
        Check(EditorBridgeApi.EngineHostStatusNoNewFrame == 11, "NO_NEW_FRAME must be 11");
        log("[P2-M6/M7] inventory explicit (no silent baseline rewrite); EditorBridge line still v1.1");
    }

    private static void WriteI32(byte[] b, int offset, int value)
        => System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(b.AsSpan(offset, 4), value);

    private static void WriteU64(byte[] b, int offset, ulong value)
        => System.Buffers.Binary.BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(offset, 8), value);
}
