using System;
using Avalonia.Controls;
using Avalonia.Media.Imaging;   // WriteableBitmap
using Avalonia.Threading;
using AvaloniaEditor.Native;

namespace AvaloniaEditor.Services;

/// <summary>
/// HRC-3 P2 **UI Proof** gate（U1 ~ U8）。
///
/// 要证明的不是"bitmap 存在"，而是：
///   "control **当前显示**的 frame 是 N" —— 因此保留 UI-side displayed-frame receipt。
///
/// 固定 800x600；**不验证 resize**（D4 仍 Deferred）。
/// 不用 delay / retry：全部用 frame identity 判定。
/// </summary>
public static class P2UiProofGate
{
    private const string ManifestRelative = "assets/gp01/manifest.json";
    private const string SceneRelative = "assets/gp01/Main.scene";

    private static void Check(bool condition, string what)
    {
        if (!condition) throw new InvalidOperationException("P2-UI GATE: " + what);
    }

    public static int Run(MainWindow window, EditorHostService host, Action<string> log)
    {
        try
        {
            Dispatcher.UIThread.VerifyAccess();

            var image = window.FindControl<Image>("ViewportImage");
            var placeholder = window.FindControl<TextBlock>("ViewportPlaceholder");
            Check(image is not null, "production ViewportImage control must exist");
            Check(placeholder is not null, "production ViewportPlaceholder control must exist");

            // 起点必须是"未绑定"的：证明我们真的改变了 production 控件的状态
            Check(image!.Source is null, $"ViewportImage.Source must start unbound (was {image.Source?.GetType().Name})");
            Check(placeholder!.IsVisible, "placeholder must be visible before the first presented frame");

            var presenter = new ViewportFramePresenter
            {
                ViewportImage = image,
                ViewportPlaceholder = placeholder,
            };

            host.OpenProject(ManifestRelative, SceneRelative);

            // ── 没有新帧时：控件必须完全不被碰（U2/U4 的负向面）──
            var before = host.TryFetchPresentationFrame();
            Check(!before.HasNewFrame, "no frame exists before the first pump");

            // ── U1 / U3 / U5：逐帧 pump → fetch → present ──
            var seenFrameIds = new System.Collections.Generic.List<ulong>();
            for (int i = 0; i < 3; ++i)
            {
                host.RuntimeTick(0.016f);   // 既有唯一 pump 入口

                FrameFetchResult fetched = host.TryFetchPresentationFrame();
                Check(fetched.HasNewFrame, $"frame {i + 1}: expected a new frame");
                var frame = fetched.Frame!;

                // 关键：present 之前先记住 fetched 的 identity，present 之后必须一致（U3）
                ulong fetchedId = frame.FrameId;
                bool presented = presenter.Present(frame);
                Check(presented, $"frame {i + 1}: presenter must accept a strictly newer frame");

                // U1：receipt 携带的 bitmap 像素必须来自同一个 packet
                var receipt = presenter.LastDisplayed!;
                Check(receipt.DisplayedFrameId == fetchedId,
                      $"U3: displayed frame_id {receipt.DisplayedFrameId} != fetched {fetchedId}");
                Check(receipt.Width == PresentationFrame.ExpectedWidth &&
                      receipt.Height == PresentationFrame.ExpectedHeight,
                      "U1: displayed frame must be 800x600 (resize not in scope)");
                Check(ReferenceEquals(receipt.Bitmap, image.Source),
                      "U2: production ViewportImage.Source must point at the frame bitmap");
                Check(receipt.BoundToViewportImage, "U2: receipt must confirm binding to ViewportImage");
                Check(!placeholder.IsVisible, "U2: legacy placeholder must be hidden once a real frame is shown");

                // U5：显示的像素**不是** clear-only（用 P1 已验证的真实 fixture）
                Check(receipt.NonClearPixels > 0,
                      $"U5: displayed pixels must not be clear-only (nonClear={receipt.NonClearPixels})");

                // U1 续：bitmap 内容必须确实等于该 packet（按声明的翻转关系）
                CheckBitmapMatchesPacket(receipt, frame, i + 1);

                seenFrameIds.Add(fetchedId);
                log($"[P2-U{i + 1}] frame={fetchedId} 800x600 nonClear={receipt.NonClearPixels} bound={receipt.BoundToViewportImage}");
            }

            // ── U3 续：displayedFrameId == fetchedFrameId == packet.frame_id（三者一致）──
            Check(presenter.DisplayedFrameId == seenFrameIds[^1],
                  $"U3: presenter displayed {presenter.DisplayedFrameId}, last fetched {seenFrameIds[^1]}");

            // ── U4：不得回退、不得复用旧帧 ──
            // 4a. 不 pump 就没有新帧：控件必须保持不动（不重贴旧 bitmap）
            var sourceBefore = image.Source;   // IImage?（Source 的静态类型）
            var noFrame = host.TryFetchPresentationFrame();
            Check(!noFrame.HasNewFrame, "U4: no new frame must be reported without a pump");
            Check(!presenter.PresentLatestFrame(host),
                  "U4: PresentLatestFrame must return false when there is no new frame");
            Check(ReferenceEquals(image.Source, sourceBefore),
                  "U4: Image.Source must be untouched when there is no new frame (no stale reuse)");
            Check(presenter.DisplayedFrameId == seenFrameIds[^1],
                  "U4: displayed frame must not advance without a new frame");

            // 4b. 乱序回放：把"迟到的旧帧"重新交给 presenter，必须被拒绝，UI 不得回退
            var staleFrame = host.TryFetchPresentationFrame();   // 仍然是 no-new-frame
            Check(!staleFrame.HasNewFrame, "U4: setup expects no new frame for the stale-replay check");
            // 用当前已显示的 identity 构造一个"更旧"的帧对象，直接喂 presenter
            var older = PresentationFrame.ForOrderedReplayProbe(
                              PresentationFrame.ExpectedWidth,
                              PresentationFrame.ExpectedHeight,
                              seenFrameIds[^1] - 1,   // 严格更旧
                              new byte[PresentationFrame.ExpectedWidth *
                                       PresentationFrame.ExpectedHeight * 4]);
            bool acceptedStale = presenter.Present(older);
            Check(!acceptedStale, "U4: presenter must reject a stale (lower frame_id) frame");
            Check(presenter.DisplayedFrameId == seenFrameIds[^1],
                  "U4: displayed frame must not regress after a stale replay");
            Check(ReferenceEquals(image.Source, sourceBefore),
                  "U4: stale replay must not replace the displayed bitmap");

            // 4c. 严格单调
            for (int i = 1; i < seenFrameIds.Count; ++i)
            {
                Check(seenFrameIds[i] > seenFrameIds[i - 1],
                      $"U4: frame_id must advance monotonically ({seenFrameIds[i - 1]} -> {seenFrameIds[i]})");
            }

            log($"[P2-UI U4] no-regress + no-stale-reuse OK (displayed={presenter.DisplayedFrameId}, " +
                $"monotonic={string.Join("->", seenFrameIds)})");
            log("[P2-UI GATE] ALL GREEN");
            return 0;
        }
        catch (Exception ex)
        {
            log("[P2-UI GATE] FAIL: " + ex.Message);
            return 1;
        }
    }

    /// <summary>
    /// U1 续证：把 displayed bitmap 的像素读回来，按声明的翻转关系与 packet 逐字节比对。
    /// 这证明"bitmap 像素来自同一个 packet"，而不是"某张来路不明的位图"。
    /// </summary>
    private static void CheckBitmapMatchesPacket(ViewportDisplayReceipt receipt, PresentationFrame frame, int index)
    {
        int w = frame.Width, h = frame.Height;
        var bitmap = receipt.Bitmap;
        Check(bitmap.PixelSize.Width == w && bitmap.PixelSize.Height == h,
              $"U1 #{index}: bitmap size must match the frame");

        using var fb = bitmap.Lock();
        Check(fb.RowBytes >= w * 4, $"U1 #{index}: RowBytes {fb.RowBytes} too small for {w}*4");
        var shown = new byte[w * 4];
        for (int y = 0; y < h; ++y)
        {
            int dstOffset = y * fb.RowBytes;
            System.Runtime.InteropServices.Marshal.Copy(fb.Address + dstOffset, shown, 0, w * 4);
            int srcRow = receipt.VerticallyFlipped ? (h - 1 - y) : y;
            int srcOffset = srcRow * w * 4;
            for (int x = 0; x < w * 4; ++x)
            {
                if (shown[x] != frame.Rgba[srcOffset + x])
                {
                    throw new InvalidOperationException(
                        $"P2-UI GATE: U1 #{index}: displayed bitmap pixel differs from the packet " +
                        $"(row {y}, byte {x}: bitmap={shown[x]}, packet={frame.Rgba[srcOffset + x]})");
                }
            }
        }
    }
}
