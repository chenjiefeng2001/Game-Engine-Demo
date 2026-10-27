using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using AvaloniaEditor.Native;
using AvaloniaEditor.Services;
using AvaloniaEditor.ViewModels;
using static AvaloniaEditor.Services.GateProbe;

namespace AvaloniaEditor.Services;

// P/Invoke to the C runtime so the injected failure flag reaches native getenv().
// .NET's Environment.SetEnvironmentVariable does not update the CRT environment.
internal static partial class ViewportSizeGateNativeEnv
{
    [DllImport("ucrtbased.dll", CallingConvention = CallingConvention.Cdecl, EntryPoint = "_putenv_s")]
    private static extern int _putenv_s_debug(string name, string value);

    [DllImport("ucrtbase.dll", CallingConvention = CallingConvention.Cdecl, EntryPoint = "_putenv_s")]
    private static extern int _putenv_s_release(string name, string value);

    public static void Set(string name, string value)
    {
        try { _putenv_s_debug(name, value); } catch { }
        try { _putenv_s_release(name, value); } catch { }
    }
}

/// <summary>
/// Slice E / --viewportsize gate: dynamic presentation viewport size end to end.
/// </summary>
public static class ViewportSizeGate
{
    private const string GateName = "[VIEWPORT-SIZE GATE]";
    private const int TimeoutMs = 8000;

    public static async Task<int> RunAsync(MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        try
        {
            Check(host.Backend == HostBackend.EngineHost,
                "the viewport-size gate requires the EngineHost backend");
            Check(!host.SupportsViewport,
                "the EngineHost backend must not advertise legacy viewport capability");

            Click(Control<Button>(window, "OpenProjectBtn"));
            Check(host.State == HostRuntimeState.Ready && vm.IsOpen,
                "the Open handler reaches Ready");
            var image = Control<Image>(window, "ViewportImage");
            Check(await WaitUntilAsync(() => image.Source is WriteableBitmap, TimeoutMs),
                "the unique pump presents a frame into the production ViewportImage");
            Check(await WaitUntilAsync(() => image.Bounds.Width > 0 && image.Bounds.Height > 0, TimeoutMs),
                "the viewport image reaches a laid-out state");

            // 1. Default path.
            var initial = window.LastDisplayedFrame;
            Check(initial is not null, "V1: a frame has been displayed");
            Check(initial!.Width == 800 && initial.Height == 600,
                $"V1: default frame is 800x600 (got {initial.Width}x{initial.Height})");
            Check(window.ViewportResizeDispatchCount == 0,
                "V1: the initial layout is the baseline and is not dispatched");
            log($"{GateName} V1: default 800x600 path is intact");

            // 2. 800x600 -> 640x480.
            int frameBefore = (int)initial.DisplayedFrameId;
            int dispatchBefore = window.ViewportResizeDispatchCount;
            Check(window.RequestViewportResize(640, 480),
                "V2: 640x480 is accepted for registration");
            Check(window.ViewportResizeDispatchCount == dispatchBefore + 1,
                "V2: the resize request is registered");
            Check(await WaitUntilAsync(() =>
                window.LastDisplayedFrame is { Width: 640, Height: 480 }
                && window.LastDisplayedFrame!.DisplayedFrameId > (ulong)frameBefore, TimeoutMs),
                "V2: a 640x480 frame is published after resize");
            log($"{GateName} V2: 800x600 -> 640x480 published");

            // 3. 640x480 -> 1920x1080, buffer reuse and expansion.
            frameBefore = (int)window.LastDisplayedFrame!.DisplayedFrameId;
            int allocationsBefore = host.FrameBufferAllocations;
            Check(window.RequestViewportResize(1920, 1080),
                "V3: 1920x1080 is accepted for registration");
            bool v3ok = await WaitUntilAsync(() =>
                window.LastDisplayedFrame is { Width: 1920, Height: 1080 }
                && window.LastDisplayedFrame!.DisplayedFrameId > (ulong)frameBefore, TimeoutMs);
            var v3last = window.LastDisplayedFrame;
            Check(v3ok,
                $"V3: a 1920x1080 frame is published after resize (got {v3last?.Width}x{v3last?.Height}, frame {v3last?.DisplayedFrameId}, before {frameBefore}, pumps {host.PumpInvocations}/{host.PumpFailures})");
            Check(host.FrameBufferAllocations == allocationsBefore,
                $"V3: the fetch buffer is reused ({host.FrameBufferAllocations} allocations)");
            Check(host.FrameBufferAllocations == 1,
                "V3: exactly one max-capacity buffer was allocated");
            log($"{GateName} V3: 640x480 -> 1920x1080 published with buffer reuse");

            // 4. Continuous resize: last-wins.
            frameBefore = (int)window.LastDisplayedFrame!.DisplayedFrameId;
            window.RequestViewportResize(640, 480);
            window.RequestViewportResize(900, 700);
            Check(await WaitUntilAsync(() =>
                window.LastDisplayedFrame is { Width: 900, Height: 700 }
                && window.LastDisplayedFrame!.DisplayedFrameId > (ulong)frameBefore, TimeoutMs),
                "V4: continuous resize keeps only the last target (900x700)");
            log($"{GateName} V4: last-wins resize verified");

            // 5. Zero and oversize are rejected.
            int rejectedBefore = window.ViewportResizeRejectedCount;
            int sizeBeforeWidth = window.LastDisplayedFrame!.Width;
            int sizeBeforeHeight = window.LastDisplayedFrame!.Height;
            Check(!window.RequestViewportResize(0, 480), "V5: zero width is rejected");
            Check(!window.RequestViewportResize(640, 0), "V5: zero height is rejected");
            Check(!window.RequestViewportResize(1921, 1080), "V5: oversized width is rejected");
            Check(!window.RequestViewportResize(1920, 1081), "V5: oversized height is rejected");
            Check(!host.SetViewportSize(0, 480), "V5: the native dispatch also rejects zero");
            Check(window.ViewportResizeRejectedCount == rejectedBefore + 4,
                "V5: all four invalid requests are counted as rejected");
            Check(window.LastDisplayedFrame!.Width == sizeBeforeWidth
                  && window.LastDisplayedFrame!.Height == sizeBeforeHeight,
                "V5: invalid requests do not change the current size");
            log($"{GateName} V5: zero and oversize requests are rejected");

            // 6. FBO rebuild failure publishes no mismatched frame.
            var failedSize = window.LastDisplayedFrame!;
            var failedFrameId = failedSize.DisplayedFrameId;
            ViewportSizeGateNativeEnv.Set("EDITOR_BRIDGE_FAIL_VIEWPORT_RESIZE", "1");
            try
            {
                Check(window.RequestViewportResize(640, 480),
                    "V6: the resize request is registered even when rebuild will fail");
                await Task.Delay(250);
                Check(window.LastDisplayedFrame!.Width == failedSize.Width
                      && window.LastDisplayedFrame!.Height == failedSize.Height,
                    "V6: a failed rebuild keeps the previous frame size");
                Check(window.LastDisplayedFrame!.DisplayedFrameId == failedFrameId,
                    "V6: a failed rebuild publishes no new frame");
                log($"{GateName} V6: rebuild failure is fail-closed");
            }
            finally
            {
                ViewportSizeGateNativeEnv.Set("EDITOR_BRIDGE_FAIL_VIEWPORT_RESIZE", "");
            }
            // Recovery after the injected failure is cleared.
            frameBefore = (int)window.LastDisplayedFrame!.DisplayedFrameId;
            Check(window.RequestViewportResize(800, 600),
                "V6: resize is accepted again after the failure is cleared");
            Check(await WaitUntilAsync(() =>
                window.LastDisplayedFrame is { Width: 800, Height: 600 }
                && window.LastDisplayedFrame!.DisplayedFrameId > (ulong)frameBefore, TimeoutMs),
                "V6: the presentation target recovers to 800x600");
            log($"{GateName} V6: recovery after rebuild failure verified");

            // 7. Resize invalidates a pending pick from the old coordinate space.
            var bitmap = (WriteableBitmap)image.Source!;
            var sample = SampleNonClear(bitmap);
            Check(sample is not null, "V7: the frame contains a non-clear pixel to pick");
            int hitsBefore = vm.PickHitsApplied;
            var selectedBefore = vm.SelectedEntity;
            Check(window.RequestPickAtPoint(sample!.X, sample.Y),
                "V7: a pick request is accepted before resize");
            Check(vm.PickState == MainViewModel.PickRequestState.Pending,
                "V7: the pick request is pending");
            Check(window.RequestViewportResize(640, 480),
                "V7: a resize is registered while the pick is pending");
            Check(await WaitUntilAsync(() =>
                vm.PickState == MainViewModel.PickRequestState.Idle
                && window.LastDisplayedFrame is { Width: 640, Height: 480 }, TimeoutMs),
                "V7: the pending pick is consumed on the resized frame");
            Check(vm.PickOutcome == "not-ready",
                $"V7: the old-coordinate pick is not-ready (got '{vm.PickOutcome}')");
            Check(vm.PickHitsApplied == hitsBefore,
                "V7: an invalidated pick does not count as an applied hit");
            Check(Equals(vm.SelectedEntity, selectedBefore),
                "V7: an invalidated pick does not change the selection");
            log($"{GateName} V7: resize invalidates the pending pick");

            // 8. Production pick coordinates for identity, clipping and scaling.
            Check(ViewportPickMapper.TryMap(400, 300, 0, 0, 800, 600, 800, 600, out var center)
                  == PickMapStatus.Ok && center.X == 400 && center.Y == 300,
                "V8: identity mapping maps the centre pixel");
            Check(ViewportPickMapper.TryMap(100.9, 200.9, 0, 0, 800, 600, 800, 600, out var floored)
                  == PickMapStatus.Ok && floored.X == 100 && floored.Y == 200,
                "V8: mapping uses floor, not round");
            Check(ViewportPickMapper.TryMap(800, 300, 0, 0, 800, 600, 800, 600, out _) == PickMapStatus.OutsideFrame,
                "V8: the right edge is outside the half-open frame");
            Check(ViewportPickMapper.TryMap(double.NaN, 10, 0, 0, 800, 600, 800, 600, out _) == PickMapStatus.InvalidCoordinate,
                "V8: non-finite coordinates are rejected");
            Check(ViewportPickMapper.TryMap(200, 100, 0, 0, 400, 200, 800, 600, out var scaled)
                  == PickMapStatus.Ok && scaled.X == 400 && scaled.Y == 300,
                "V8: a scaled display maps to frame pixels by the real scale");
            Check(ViewportPickMapper.TryMap(900, 100, 0, 0, 800, 600, 800, 600, out _) == PickMapStatus.OutsideFrame,
                "V8: a clipped control does not clamp an out-of-frame click");
            // Real production pick on a non-default frame size.
            Check(window.RequestViewportResize(640, 480), "V8: resize to 640x480 for production pick");
            Check(await WaitUntilAsync(() => window.LastDisplayedFrame is { Width: 640, Height: 480 }, TimeoutMs),
                "V8: the 640x480 frame is displayed");
            var resizedBitmap = (WriteableBitmap)image.Source!;
            var resizedSample = SampleNonClear(resizedBitmap);
            Check(resizedSample is not null, "V8: the resized frame contains content");
            int resizedHitsBefore = vm.PickHitsApplied;
            Check(window.RequestPickAtPoint(resizedSample!.X, resizedSample.Y),
                "V8: a production pick is accepted on the resized frame");
            Check(await WaitUntilAsync(() => vm.PickState == MainViewModel.PickRequestState.Idle, TimeoutMs),
                "V8: the resized-frame pick result is consumed");
            Check(vm.PickOutcome == "hit", $"V8: the resized-frame pick hits (got '{vm.PickOutcome}')");
            Check(vm.PickHitsApplied == resizedHitsBefore + 1,
                "V8: exactly one hit is applied on the resized frame");
            log($"{GateName} V8: production pick coordinates verified");

            // 9. v1.4/v1.5 mix is rejected at Create.
            Check(EditorBridgeApi.EngineHostExpectedVersion == ((1 << 16) | 5),
                "V9: managed expects ABI v1.5");
            bool threw = false;
            try { EditorHostService.VerifyEngineHostAbiVersion((1 << 16) | 4); }
            catch (HostOperationException) { threw = true; }
            Check(threw, "V9: a v1.4 native version is rejected at Create");
            log($"{GateName} V9: ABI mix-and-match is rejected");

            // 10. Packet header, payload length and capacity are consistent.
            var displayed = window.LastDisplayedFrame!;
            Check(displayed.Width > 0 && displayed.Height > 0, "V10: a frame is displayed");
            int required = PresentationFrame.RequiredBytesFor(displayed.Width, displayed.Height);
            Check(required == displayed.Width * displayed.Height * 4 + PresentationFrame.HeaderBytes,
                "V10: payload length follows width*height*4 plus the 16-byte header");
            Check(required <= PresentationFrame.CapacityBytes,
                "V10: the required bytes fit in the allocated capacity");
            Check(host.FrameBufferAllocations == 1,
                "V10: one max-capacity buffer backs all frame sizes");
            log($"{GateName} V10: packet header, payload and capacity are consistent");

            log($"{GateName} ALL GREEN");
            return 0;
        }
        catch (Exception ex)
        {
            log($"{GateName} FAIL: {ex.Message}");
            return 1;
        }
    }

    private sealed class PixelSample
    {
        public int X { get; init; }
        public int Y { get; init; }
    }

    private static PixelSample? SampleNonClear(WriteableBitmap bitmap)
    {
        int w = bitmap.PixelSize.Width;
        int h = bitmap.PixelSize.Height;
        using var fb = bitmap.Lock();
        var row = new byte[w * 4];
        for (int y = 0; y < h; y++)
        {
            Marshal.Copy(fb.Address + y * fb.RowBytes, row, 0, w * 4);
            for (int x = 0; x < w; x++)
            {
                int o = x * 4;
                bool isClear = row[o] == 13 && row[o + 1] == 13 && row[o + 2] == 18;
                if (!isClear) return new PixelSample { X = x, Y = y };
            }
        }
        return null;
    }
}
