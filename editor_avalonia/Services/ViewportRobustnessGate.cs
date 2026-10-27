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

public static class ViewportRobustnessGate
{
    private const string GateName = "[VIEWPORT-ROBUSTNESS GATE]";
    private const int TimeoutMs = 8000;

    public static async Task<int> RunAsync(MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        try
        {
            Check(host.Backend == HostBackend.EngineHost, "requires EngineHost backend");

            // S3: buffer reuse and packet boundaries.
            Click(Control<Button>(window, "OpenProjectBtn"));
            Check(host.State == HostRuntimeState.Ready && vm.IsOpen, "project opens");
            var image = Control<Image>(window, "ViewportImage");
            Check(await WaitUntilAsync(() => image.Source is WriteableBitmap, TimeoutMs), "first frame");
            var before = host.FrameFetchBuffer;
            Check(before is not null, "fetch buffer allocated");
            int allocationsBefore = host.FrameBufferAllocations;
            for (int i = 0; i < 5; i++)
            {
                host.RuntimeTick(0.016f);
                var fetched = host.TryFetchPresentationFrame();
                if (fetched.HasNewFrame)
                {
                    Check(ReferenceEquals(host.FrameFetchBuffer, before), "the fetch buffer identity is stable");
                }
            }
            Check(host.FrameBufferAllocations == allocationsBefore, "no steady-state reallocation");
            Check(host.FrameBufferAllocations == 1, "exactly one fetch buffer");
            log($"{GateName} R1: fetch buffer reused ({host.FrameFetchBuffer!.Length} bytes, {host.FrameBufferAllocations} allocation(s))");

            // Packet boundary checks.
            byte[] ValidPacket(int w, int h, ulong id)
            {
                var b = new byte[PresentationFrame.RequiredBytesFor(w, h)];
                for (int i = PresentationFrame.HeaderBytes; i + 3 < b.Length; i += 4)
                {
                    b[i] = 13; b[i + 1] = 13; b[i + 2] = 18; b[i + 3] = 255;
                }
                System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(b.AsSpan(0, 4), w);
                System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(b.AsSpan(4, 4), h);
                System.Buffers.Binary.BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(8, 8), id);
                return b;
            }

            Check(PresentationFrame.TryDecode(ValidPacket(800, 600, 7), 16 + 800 * 600 * 4, out var f1, out _) && f1 is not null, "minimum-capacity exact packet decodes");
            Check(f1!.Width == 800 && f1.Height == 600 && f1.FrameId == 7UL, "header fields decode");
            Check(f1.Rgba.Length == 800 * 600 * 4, "payload length follows header");
            Check(!PresentationFrame.TryDecode(ValidPacket(800, 600, 7), 15, out _, out _), "truncated header rejected");
            Check(!PresentationFrame.TryDecode(ValidPacket(800, 600, 7), 16 + 800 * 600 * 4 - 1, out _, out _), "truncated payload rejected");
            var oversize = ValidPacket(1921, 1080, 7);
            Check(!PresentationFrame.TryDecode(oversize, oversize.Length, out _, out _), "oversize width rejected");
            var zeroId = ValidPacket(800, 600, 0);
            Check(!PresentationFrame.TryDecode(zeroId, zeroId.Length, out _, out _), "zero frame_id rejected");
            var badLength = ValidPacket(800, 600, 7);
            System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(badLength.AsSpan(0, 4), 801);
            Check(!PresentationFrame.TryDecode(badLength, badLength.Length, out _, out _), "inconsistent width rejected");
            Check(!PresentationFrame.TryDecode(null!, 0, out _, out _), "null buffer rejected");
            log($"{GateName} R2: packet boundaries and malformed headers rejected");

            // No stale result after no new frame.
            var firstFetch = host.TryFetchPresentationFrame();
            if (firstFetch.HasNewFrame)
            {
                var secondFetch = host.TryFetchPresentationFrame();
                Check(!secondFetch.HasNewFrame && secondFetch.Frame is null, "repeated fetch without a new frame is empty");
            }
            log($"{GateName} R3: no stale frame is reused");

            // S4: display geometry determinism.
            Check(ViewportPickMapper.TryMap(10, 10, 5, 7, 20, 14, 10, 10, out var originMapped) == PickMapStatus.Ok
                  && originMapped.X == 2 && originMapped.Y == 2, "non-zero display origin maps correctly");
            Check(ViewportPickMapper.TryMap(19.9, 13.9, 5, 7, 20, 14, 10, 10, out var originEdge) == PickMapStatus.Ok
                  && originEdge.X == 7 && originEdge.Y == 4, "origin mapping uses floor");
            Check(ViewportPickMapper.TryMap(4.9, 6.9, 5, 7, 20, 14, 10, 10, out _) == PickMapStatus.OutsideFrame, "left/top of displayed rect is outside frame");
            Check(ViewportPickMapper.TryMap(25.1, 21.1, 5, 7, 20, 14, 10, 10, out _) == PickMapStatus.OutsideFrame, "right/bottom edge is outside frame");
            Check(ViewportPickMapper.TryMap(10, 10, 0, 0, 40, 20, 10, 10, out var scaled) == PickMapStatus.Ok
                  && scaled.X == 2 && scaled.Y == 5, "scaled display maps by actual scale");
            Check(ViewportPickMapper.TryMap(5, 5, 0, 0, 10, 10, 10, 10, out var identity) == PickMapStatus.Ok
                  && identity.X == 5 && identity.Y == 5, "identity mapping");
            Check(ViewportDisplayGeometry.TryCompute(new Avalonia.Rect(0, 0, 100, 100), new Avalonia.Size(800, 600), Avalonia.Media.Stretch.None, out var noneRect)
                  && noneRect.Width == 800 && noneRect.Height == 600, "Stretch.None uses natural size");
            Check(ViewportDisplayGeometry.TryCompute(new Avalonia.Rect(0, 0, 400, 300), new Avalonia.Size(800, 600), Avalonia.Media.Stretch.Uniform, out var uniformRect)
                  && uniformRect.Width == 400 && uniformRect.Height == 300, "Uniform fits the available box");
            Check(ViewportDisplayGeometry.TryCompute(new Avalonia.Rect(0, 0, 400, 300), new Avalonia.Size(800, 600), Avalonia.Media.Stretch.UniformToFill, out var fillRect)
                  && fillRect.Width == 400 && fillRect.Height == 300, "UniformToFill covers the available box");
            log($"{GateName} R4: displayed geometry and pick mapping deterministic");

            // High-DPI style scale is represented by displayed rect dimensions.
            Check(ViewportPickMapper.TryMap(15, 15, 0, 0, 160, 120, 80, 60, out var dpiMapped) == PickMapStatus.Ok
                  && dpiMapped.X == 7 && dpiMapped.Y == 7, "displayed rect scale handles DPI-like scaling");
            log($"{GateName} R5: DPI-like scaling maps through displayed rect");

            // Resize/pick race evidence (managed side).
            var bitmap = (WriteableBitmap)image.Source!;
            var sample = SampleNonClear(bitmap);
            Check(sample is not null, "frame has pickable content");
            int hitsBefore = vm.PickHitsApplied;
            Check(window.RequestPickAtPoint(sample!.X, sample.Y), "pick accepted");
            Check(window.RequestViewportResize(640, 480), "resize accepted while pick pending");
            Check(await WaitUntilAsync(() => vm.PickState == MainViewModel.PickRequestState.Idle, TimeoutMs), "pending pick consumed");
            Check(vm.PickOutcome == "not-ready", $"resize invalidates pending pick (got '{vm.PickOutcome}')");
            Check(vm.PickHitsApplied == hitsBefore, "invalidated pick is not applied");
            log($"{GateName} R6: resize invalidates pending pick");

            // S7: steady-state allocation evidence.
            int frameCount = 0;
            for (int i = 0; i < 20; i++)
            {
                host.RuntimeTick(0.016f);
                var fetched = host.TryFetchPresentationFrame();
                if (fetched.HasNewFrame) frameCount++;
            }
            Check(frameCount > 0, "steady-state fetch produced frames");
            Check(host.FrameBufferAllocations == 1, "steady-state fetch did not reallocate");
            log($"{GateName} R7: {frameCount} frames fetched with {host.FrameBufferAllocations} allocation(s)");

            log($"{GateName} ALL GREEN");
            return 0;
        }
        catch (Exception ex)
        {
            log($"{GateName} FAIL: {ex.Message}");
            return 1;
        }
    }

    private sealed class PixelSample { public int X; public int Y; }

    private static PixelSample? SampleNonClear(WriteableBitmap bitmap)
    {
        int w = bitmap.PixelSize.Width, h = bitmap.PixelSize.Height;
        using var fb = bitmap.Lock();
        var row = new byte[w * 4];
        for (int y = 0; y < h; y++)
        {
            Marshal.Copy(fb.Address + y * fb.RowBytes, row, 0, w * 4);
            for (int x = 0; x < w; x++)
            {
                int o = x * 4;
                if (!(row[o] == 13 && row[o + 1] == 13 && row[o + 2] == 18))
                    return new PixelSample { X = x, Y = y };
            }
        }
        return null;
    }
}
