using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using AvaloniaEditor.Native;
using AvaloniaEditor.ViewModels;
using static AvaloniaEditor.Services.GateProbe;

namespace AvaloniaEditor.Services;

/// <summary>
/// Slice D 生产路径 gate：viewport 点击 → canonical 像素 → EngineHost PICK → 帧内结果 → 选中。
///
/// 刻意**不**断言"点击一定会命中某个实体"，而是分别证明每一段：
///   P1  几何前提（可执行）：帧 1:1 且以控件原点锚定，且 mapper 的边界语义正确；
///   P2  受理 ≠ 选中：Dispatch 返回 Accepted 后状态是 Pending，选中**未被改动**；
///   P3  单笔在途：Pending 期间再点不排队、不替换在途请求；
///   P4  fail-closed：身份 seam 不可用时，`Hit` **不**落到选中上，并给出明确诊断；
///   P5  seam 接通后选中链路可用（gate 内注入**仅用于证明链路**的 resolver）；
///   P6  `NoHit` 是正常结果（点空白 → 清选中），不是失败；
///   P7  越界点击**不提交请求**，也不 clamp；
///   P8  legacy 路径未被使用（且在生产后端上会被显式拒绝）；
///   P9  键盘只在 viewport 持有焦点时被消费（D-C3）；
///   P10 边界：ABI 版本 / op 数量 / packet 大小未变。
///
/// 不新增 ABI、不新增 pump、不写 fixture；pick 结果与帧走同一个 FrameAdvanced 发布点。
/// </summary>
public static class ProductionPickGate
{
    private const string GateName = "[PROD-PICK GATE]";
    private const int TimeoutMs = 5000;

    /// <summary>presentation FBO 的 clear color（0.05,0.05,0.07,1.0）→ 13/13/18。</summary>
    private const byte ClearR = 13, ClearG = 13, ClearB = 18;

    public static async Task<int> RunAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        try
        {
            Check(host.Backend == HostBackend.EngineHost,
                "the production pick gate requires the EngineHost backend");
            Check(!host.SupportsViewport,
                "the EngineHost backend must not advertise legacy viewport capability");

            Click(Control<Button>(window, "OpenProjectBtn"));
            Check(host.State == HostRuntimeState.Ready && vm.IsOpen,
                "the Open handler reaches Ready");

            var image = Control<Image>(window, "ViewportImage");
            Check(await WaitUntilAsync(() => image.Source is WriteableBitmap, TimeoutMs),
                "the unique pump presents a frame into the production ViewportImage");

            // 布局必须先完成：`Opened` 早于首次 arrange，此时 Bounds 仍是 0x0。
            // 这是 gate 的时序问题，不是契约问题 —— 等布局，而不是放宽断言。
            Check(await WaitUntilAsync(
                      () => image.Bounds.Width > 0 && image.Bounds.Height > 0, TimeoutMs),
                "P1: the viewport image reaches a laid-out state");

            // ── P1：几何前提 + mapper 边界语义（可执行断言）──
            AssertGeometryPremise(image, log);

            // ── P8：legacy 路径在生产后端上不可用（本 gate 也不使用它）──
            Check(Throws<HostCapabilityNotAvailableException>(() => host.ViewportPick(0f, 0f)),
                "P8: the legacy ViewportPick path must be rejected on the EngineHost backend");

            // ── P9：键盘所有权 ──
            var border = Control<Border>(window, "ViewportBorder");
            // 用 TextBox 作为"另一个可聚焦控件"：它与 ListBox 不同，不依赖选择语义，
            // 是键盘焦点的确定性子。
            var renameBox = Control<TextBox>(window, "RenameBox");
            Check(border.Focus(), "P9: the viewport accepts keyboard focus (Focusable=True)");
            Check(border.IsKeyboardFocusWithin,
                "P9: focus gives the viewport keyboard ownership");
            Check(renameBox.Focus(), "P9: another control can take focus away from the viewport");
            Check(!border.IsKeyboardFocusWithin,
                "P9: keyboard input must not be consumed once the viewport loses focus");
            Check(border.Focus(), "P9: the viewport can re-acquire keyboard focus");
            Check(border.IsKeyboardFocusWithin,
                "P9: re-acquired focus restores keyboard ownership");
            log($"{GateName} P9: keyboard ownership follows viewport focus");

            var hierarchy = Control<ListBox>(window, "HierarchyList");

            // ── 采样当前已显示帧，得到"有内容"和"空"两类候选像素 ──
            var bitmap = (WriteableBitmap)image.Source!;
            var sample = SamplePixels(bitmap);
            Check(sample.NonClear.Count > 0,
                "the presented frame must contain non-clear pixels to pick against");
            log($"{GateName} sample: nonClear={sample.NonClearCount} "
                + $"candidates={sample.NonClear.Count} clear={sample.ClearPoint}");

            // ── P2：受理 ≠ 选中 ──
            var anchor = vm.FilteredEntities[0];
            hierarchy.SelectedItem = anchor;
            Check(vm.SelectedEntity?.Index == anchor.Index,
                "a pre-existing selection is in place (so 'unchanged' is observable)");
            int hitsBefore = vm.PickHitsApplied;

            var target = sample.NonClear[0];
            Check(window.RequestPickAtPoint(target.X, target.Y),
                $"P2: the pick request is accepted at {target}");
            Check(vm.PickState == MainViewModel.PickRequestState.Pending,
                $"P2: an accepted request is Pending, not selected (state={vm.PickState})");
            Check(vm.PickHitsApplied == hitsBefore,
                "P2: acceptance must not count as an applied hit");
            Check(vm.SelectedEntity?.Index == anchor.Index,
                "P2: acceptance must not change the selection");
            log($"{GateName} P2: accepted -> Pending, selection unchanged "
                + $"(request #{vm.PendingPickRequestId})");

            // ── P3：单笔在途，不排队、不替换 ──
            ulong pendingId = vm.PendingPickRequestId;
            int consoleBefore = vm.ConsoleLines.Count;
            Check(!window.RequestPickAtPoint(target.X, target.Y),
                "P3: a second click while Pending must be refused");
            Check(vm.PendingPickRequestId == pendingId,
                "P3: the outstanding request id must not be replaced");
            Check(LogSince(vm, consoleBefore).Contains("still awaiting"),
                "P3: the refusal is explained in the console");
            log($"{GateName} P3: single outstanding request preserved (#{pendingId})");

            // ── P4：结果由唯一 pump 消费，且带帧身份（typed result）──
            Check(await WaitUntilAsync(
                      () => vm.PickState == MainViewModel.PickRequestState.Idle, TimeoutMs),
                "P4: the frame result is consumed by the single pump");
            Check(vm.LastPickFrameId > 0,
                $"P4: the result carries its owning frame id (got {vm.LastPickFrameId})");
            Check(vm.PickOutcome is "hit" or "no-hit",
                $"P4: the outcome is a typed result (got '{vm.PickOutcome}')");
            log($"{GateName} P4: result consumed on frame {vm.LastPickFrameId} "
                + $"(outcome '{vm.PickOutcome}')");

            // ── P5：v1.4 身份桥 —— hit -> entity_index -> 选中（**生产** resolver）──
            PickPoint? hitAt = null;
            string hitName = string.Empty;
            int hitIndex = -1;
            int entityId = 0;

            if (vm.PickOutcome == "hit" && vm.LastPickedEntityId is int firstId)
            {
                // P2 用的第一个候选恰好就是命中：直接用它，避免多打一帧。
                hitAt = new PickPoint(target.X, target.Y);
                entityId = firstId;
                hitIndex = vm.LastPickEntityIndex;
                hitName = vm.SelectedEntity?.Name ?? string.Empty;
            }
            else
            {
                foreach (var candidate in sample.NonClear)
                {
                    Check(await WaitUntilAsync(
                              () => vm.PickState == MainViewModel.PickRequestState.Idle, TimeoutMs),
                        "P5: the previous result was consumed before the next request");
                    if (!window.RequestPickAtPoint(candidate.X, candidate.Y)) continue;
                    Check(await WaitUntilAsync(
                              () => vm.PickState == MainViewModel.PickRequestState.Idle, TimeoutMs),
                        "P5: the result for this candidate is consumed");
                    if (vm.PickOutcome == "hit" && vm.LastPickedEntityId is int id)
                    {
                        hitAt = new PickPoint(candidate.X, candidate.Y);
                        entityId = id;
                        hitIndex = vm.LastPickEntityIndex;
                        hitName = vm.SelectedEntity?.Name ?? string.Empty;
                        break;
                    }
                }
            }

            Check(hitAt is not null,
                "P5: at least one non-clear pixel must resolve to a hit "
                + $"(tried {sample.NonClear.Count} candidates)");
            Check(hitIndex >= 0,
                "P5: a hit must carry a resolved scene index (v1.4 entity_index)");
            Check(vm.SelectedEntity is not null, "P5: a resolved hit selects an entity");
            Check(vm.SelectedEntity!.Index == hitIndex,
                $"P5: the selection follows the packet's entity_index {hitIndex} "
                + $"(selected {vm.SelectedEntity.Index})");
            Check(vm.SelectedEntity.Name == hitName && hitName.Length > 0,
                "P5: the selected entity is the one the index addresses");
            Check(vm.PickHitsApplied > hitsBefore,
                $"P5: the hit was applied exactly through the seam (got {vm.PickHitsApplied})");
            Check(vm.PickHitsApplied == hitsBefore + 1,
                $"P5: exactly one applied hit so far (got {vm.PickHitsApplied})");
            log($"{GateName} P5: hit id={entityId} -> index {hitIndex} '{hitName}' "
                + "(production resolver, no injection)");

            // ── P6：NoHit 是正常结果（点空白 → 清选中）──
            Check(await WaitUntilAsync(
                      () => vm.PickState == MainViewModel.PickRequestState.Idle, TimeoutMs),
                "P6: the previous result was consumed");
            Check(vm.SelectedEntity is not null,
                "P6: a selection exists before clicking empty space");
            Check(window.RequestPickAtPoint(sample.ClearPoint.X, sample.ClearPoint.Y),
                $"P6: the empty-space click is accepted at {sample.ClearPoint}");
            Check(await WaitUntilAsync(
                      () => vm.PickState == MainViewModel.PickRequestState.Idle, TimeoutMs),
                "P6: the no-hit result is consumed");
            Check(vm.PickOutcome == "no-hit",
                $"P6: clicking a clear pixel is a normal no-hit (got '{vm.PickOutcome}')");
            Check(vm.SelectedEntity is null,
                "P6: no-hit clears the selection (it is a valid result, not a failure)");
            log($"{GateName} P6: no-hit is a normal result and clears the selection");

            // ── P8b（**永久回归**）：索引必须跟随实体身份，而不是 id-1 ──
            //
            // 这是 P-1 的核心证据，也是它存在的理由：删除索引 0 之后，同一像素的
            // entity_id 不变，而它的场景索引必须**前移一位**。任何"只用 ID 推导索引"
            // 的实现（例如 id-1）在此处都会给出与删除前**相同**的索引 ⇒ 选中邻居实体。
            int countBeforeDelete = vm.Entities.Count;
            hierarchy.SelectedItem = vm.FilteredEntities.First(e => e.Index == 0);
            Check(vm.SelectedEntity?.Index == 0,
                "P8b: the first entity is selected for deletion");
            Click(Control<Button>(window, "DeleteEntityBtn"));   // 真实生产删除路径
            Check(await WaitUntilAsync(
                      () => vm.Entities.Count == countBeforeDelete - 1, TimeoutMs),
                "P8b: the deletion reaches the hierarchy projection");

            Check(await WaitUntilAsync(
                      () => vm.PickState == MainViewModel.PickRequestState.Idle, TimeoutMs),
                "P8b: no request is outstanding before the identity probe");
            Check(window.RequestPickAtPoint(hitAt!.Value.X, hitAt.Value.Y),
                $"P8b: the same pixel {hitAt} is picked again");
            Check(await WaitUntilAsync(
                      () => vm.PickState == MainViewModel.PickRequestState.Idle, TimeoutMs),
                "P8b: the identity probe result is consumed");
            Check(vm.PickOutcome == "hit",
                $"P8b: the same pixel still hits an entity (got '{vm.PickOutcome}')");
            Check(vm.LastPickedEntityId == entityId,
                $"P8b: the hit entity keeps its id across the deletion "
                + $"(was {entityId}, now {vm.LastPickedEntityId})");
            Check(vm.LastPickEntityIndex == hitIndex - 1,
                $"P8b: the scene index must shift with the snapshot "
                + $"({hitIndex} -> {hitIndex - 1}); got {vm.LastPickEntityIndex} "
                + $"(an id-derived index would stay at {hitIndex})");
            Check(vm.SelectedEntity is not null && vm.SelectedEntity.Name == hitName,
                $"P8b: the shifted index must still address '{hitName}', "
                + $"got '{vm.SelectedEntity?.Name}'");
            Check(vm.LastPickEntityIndex != hitIndex,
                "P8b: any id-derived index would be unchanged by the deletion");
            log($"{GateName} P8b: identity regression OK - same pixel, same entity '{hitName}', "
                + $"index {hitIndex} -> {vm.LastPickEntityIndex} (id {entityId} unchanged)");

            // ── P9：v1.4 解码/解析契约（纯 managed，不依赖 native）──
            AssertPacketContract(log);

            // ── P7：越界点击不提交请求、不 clamp ──
            consoleBefore = vm.ConsoleLines.Count;
            Check(!window.RequestPickAtPoint(PresentationFrame.ExpectedWidth + 5, 100),
                "P7: a click beyond the frame's right edge must not submit a request");
            Check(vm.PickState == MainViewModel.PickRequestState.Idle,
                "P7: an out-of-frame click must not create a pending request");
            Check(LogSince(vm, consoleBefore).Contains("outside the rendered frame"),
                "P7: the out-of-frame click is explained in the console");
            log($"{GateName} P7: out-of-frame clicks are rejected (never clamped)");

            // ── P10：边界 —— ABI 版本/布局/op 数量 ──
            Check(EditorBridgeApi.EngineHostExpectedVersion == ((1 << 16) | 5),
                "P10: the ABI version is v1.5 (append-only SET_VIEWPORT_SIZE op)");
            Check(EditorBridgeApi.EngineHostPickIndexNone == 0xFFFFFFFFu,
                "P10: the unresolved-index sentinel is 0xFFFFFFFF (0 is a legal index)");
            Check(EditorBridgeApi.EngineHostOpPick == 29
                  && EditorBridgeApi.EngineHostOpSetViewportSize == 30
                  && EditorBridgeApi.EngineHostOpCount == 31,
                "P10: PICK=29 / SET_VIEWPORT_SIZE=30 / OP_COUNT=31");
            Check(EditorBridgeApi.EngineHostPickPacketBytes == 32,
                "P10: the pick packet size is unchanged (32B)");
            Check(PickResult.OffsetEntityIndex == 28,
                "P10: entity_index occupies the old reserved slot (offset 28)");

            log($"{GateName} ALL GREEN");
            return 0;
        }
        catch (Exception ex)
        {
            log($"{GateName} FAIL: {ex.Message}");
            return 1;
        }
    }

    // ── P1：几何前提 + mapper 边界语义 ────────────────────────────

    private static void AssertGeometryPremise(Image image, Action<string> log)
    {
        int w = PresentationFrame.ExpectedWidth, h = PresentationFrame.ExpectedHeight;

        // 前提（D-C2）：帧以 1:1 + 控件原点锚定绘制 ⇒ 不缩放。
        // **不能**用"控件尺寸 == 帧尺寸"当判据：窗口比帧窄时帧会被裁剪
        // （实测 554x600 vs 800x600），而裁剪区域仍然逐像素对齐。
        Check(image.Stretch == Avalonia.Media.Stretch.None,
            $"P1: ViewportImage must use Stretch=None (no scaling); got {image.Stretch}");

        var source = image.Source as WriteableBitmap;
        Check(source is not null, "P1: the presented frame is a WriteableBitmap");
        Check(source!.PixelSize.Width == w && source.PixelSize.Height == h,
            $"P1: the displayed bitmap is {w}x{h} (got {source.PixelSize})");

        var bounds = image.Bounds;
        Check(bounds.Width > 0 && bounds.Height > 0,
            $"P1: ViewportImage must be laid out (got {bounds.Width:0.#}x{bounds.Height:0.#})");

        // Slice E（E-C3/E-C4）：显示矩形由 View 计算，mapper 只做算术。
        // 生产模式（Stretch=None）⇒ 矩形 = 帧的自然尺寸（裁剪不是缩放）。
        Check(ViewportDisplayGeometry.TryCompute(bounds, new Avalonia.Size(w, h),
                                                 Avalonia.Media.Stretch.None, out var rect),
            "P1: the display geometry must be computable");
        Check(rect.X == 0 && rect.Y == 0 && rect.Width == w && rect.Height == h,
            $"P1: the displayed rect must be the frame's natural size (got {rect})");

        // 1:1（原点在控件原点）⇒ displayed rect 与帧尺寸相同 ⇒ scale = 1。
        Check(ViewportPickMapper.TryMap(w / 2.0, h / 2.0,
                                        rect.X, rect.Y, rect.Width, rect.Height,
                                        w, h, out var center)
              == PickMapStatus.Ok && center.X == w / 2 && center.Y == h / 2,
            $"P1: the centre maps to ({w / 2},{h / 2}), got {center}");

        // floor 语义（不是 round）
        Check(ViewportPickMapper.TryMap(100.9, 200.9,
                                        rect.X, rect.Y, rect.Width, rect.Height,
                                        w, h, out var floored)
              == PickMapStatus.Ok && floored.X == 100 && floored.Y == 200,
            $"P1: pixel p covers [p,p+1) - floor, not round (got {floored})");

        // 上/右边界是**开区间**
        Check(ViewportPickMapper.TryMap(w, h / 2.0,
                                        rect.X, rect.Y, rect.Width, rect.Height,
                                        w, h, out _) == PickMapStatus.OutsideFrame,
            "P1: x == W is outside the frame (half-open interval)");
        Check(ViewportPickMapper.TryMap(w / 2.0, h,
                                        rect.X, rect.Y, rect.Width, rect.Height,
                                        w, h, out _) == PickMapStatus.OutsideFrame,
            "P1: y == H is outside the frame (half-open interval)");

        // 负坐标越界，绝不 clamp 到 0
        Check(ViewportPickMapper.TryMap(-0.5, 10,
                                        rect.X, rect.Y, rect.Width, rect.Height,
                                        w, h, out _) == PickMapStatus.OutsideFrame,
            "P1: a negative coordinate is outside the frame, never clamped to 0");

        // 非有限值
        Check(ViewportPickMapper.TryMap(double.NaN, 10,
                                        rect.X, rect.Y, rect.Width, rect.Height,
                                        w, h, out _)
              == PickMapStatus.InvalidCoordinate, "P1: NaN is InvalidCoordinate");
        Check(ViewportPickMapper.TryMap(10, double.PositiveInfinity,
                                        rect.X, rect.Y, rect.Width, rect.Height,
                                        w, h, out _)
              == PickMapStatus.InvalidCoordinate, "P1: +Inf is InvalidCoordinate");

        // 非正帧尺寸 / 非正显示尺寸 ⇒ GeometryMismatch（fail-closed）
        Check(ViewportPickMapper.TryMap(10, 10, 0, 0, w, h, 0, h, out _)
              == PickMapStatus.GeometryMismatch,
            "P1: a non-positive frame size is a geometry mismatch (fail-closed)");
        Check(ViewportPickMapper.TryMap(10, 10, 0, 0, 0, 0, w, h, out _)
              == PickMapStatus.GeometryMismatch,
            "P1: a non-positive displayed size is a geometry mismatch (fail-closed)");

        // 缩放场景：显示矩形与帧尺寸不同 ⇒ 映射必须按真实比例走（E-C3）。
        Check(ViewportPickMapper.TryMap(200.0, 100.0, 0, 0, 400, 200, w, h, out var scaled)
              == PickMapStatus.Ok && scaled.X == 400 && scaled.Y == 300,
            $"P1: a 2x-scaled display maps to the frame's own pixels (got {scaled})");

        log($"{GateName} P1: geometry premise holds (stretch=None, source {w}x{h}, "
            + $"laid-out {bounds.Width:0.#}x{bounds.Height:0.#}, displayed {rect}); "
            + "mapper semantics verified");
    }

    // ── 帧像素采样 ────────────────────────────────────────────────

    private sealed class PixelSample
    {
        public List<PickPoint> NonClear { get; init; } = new();
        public int NonClearCount { get; init; }
        public PickPoint ClearPoint { get; init; }
    }

    /// <summary>
    /// 从**已显示**的帧里取候选像素。只做采样，**不假设**"非 clear ⇒ 必定有 ID"：
    /// P5 会按候选逐个验证，全部落空即失败。
    /// </summary>
    private static PixelSample SamplePixels(WriteableBitmap bitmap)
    {
        int w = bitmap.PixelSize.Width, h = bitmap.PixelSize.Height;
        using var fb = bitmap.Lock();
        var row = new byte[w * 4];
        int nonClear = 0;
        var candidates = new List<PickPoint>();
        var clear = new PickPoint(1, 1);
        bool haveClear = false;

        // 稀疏采样：每 16 行 / 每 16 列取一个，足够找到内容像素，也不遍历 48 万像素。
        for (int y = 8; y < h && (!haveClear || candidates.Count < 12); y += 16)
        {
            Marshal.Copy(fb.Address + y * fb.RowBytes, row, 0, w * 4);
            for (int x = 8; x < w; x += 16)
            {
                int o = x * 4;
                bool isClear = row[o] == ClearR && row[o + 1] == ClearG && row[o + 2] == ClearB;
                if (isClear)
                {
                    if (!haveClear) { clear = new PickPoint(x, y); haveClear = true; }
                    continue;
                }
                nonClear++;
                // 分散取候选：避免全部落在同一个精灵的局部。
                if (candidates.Count < 12
                    && (candidates.Count == 0
                        || Math.Abs(candidates[^1].X - x) + Math.Abs(candidates[^1].Y - y) > 40))
                    candidates.Add(new PickPoint(x, y));
            }
        }

        return new PixelSample
        {
            NonClear = candidates,
            NonClearCount = nonClear,
            ClearPoint = clear,
        };
    }

    // ── P9：v1.4 packet 契约（纯 managed，可脱离 native 独立验证）──────

    /// <summary>
    /// 构造一个 v1.4 `PickResultPacket`（32B，字段按冻结偏移写入）。
    /// 只用于验证**解码/解析**契约，不参与任何生产路径。
    /// </summary>
    private static byte[] BuildPacket(int status, uint entityId, uint entityIndex)
    {
        var b = new byte[PickResult.PacketBytes];
        var span = b.AsSpan();
        System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(span.Slice(0, 4),
            (uint)PickResult.PacketBytes);
        System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(span.Slice(4, 4), status);
        System.Buffers.Binary.BinaryPrimitives.WriteUInt64LittleEndian(span.Slice(8, 8), 42);   // frame_id
        System.Buffers.Binary.BinaryPrimitives.WriteUInt64LittleEndian(span.Slice(16, 8), 9);   // request_id
        System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(span.Slice(24, 4), entityId);
        System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(span.Slice(28, 4), entityIndex);
        return b;
    }

    private static void AssertPacketContract(Action<string> log)
    {
        var resolver = new PacketPickIdentityResolver();

        // (a) HIT + NONE ⇒ 可解码，但**不可解析**（native FindByID 失败的真实形状）。
        var unresolved = BuildPacket(EditorBridgeApi.EngineHostPickHit, 7, PickResult.IndexNone);
        Check(PickResult.TryDecode(unresolved, unresolved.Length, out var r1, out string e1)
              && r1 is not null,
            $"P9: a HIT packet carrying entity_index=NONE must still decode ({e1})");
        Check(!r1!.HasResolvedIndex,
            "P9: entity_index=NONE must never be treated as a resolved index");
        Check(!resolver.TryResolve(r1, out _, out string d1) && d1.Length > 0,
            "P9: the resolver must refuse an unresolved hit and explain why");

        // (b) HIT + index ⇒ 原样使用（不做任何 ID 算术）。
        var resolved = BuildPacket(EditorBridgeApi.EngineHostPickHit, 7, 3);
        Check(PickResult.TryDecode(resolved, resolved.Length, out var r2, out string e2)
              && r2 is not null,
            $"P9: a HIT packet with a real index must decode ({e2})");
        Check(resolver.TryResolve(r2!, out int idx2, out _) && idx2 == 3,
            $"P9: entity_index is used verbatim (expected 3, got {idx2})");

        // (c) 非 HIT 却带索引 ⇒ 解码必须 fail-closed（生产方违约）。
        var bad = BuildPacket(EditorBridgeApi.EngineHostPickNoHit, 0, 0);
        Check(!PickResult.TryDecode(bad, bad.Length, out _, out string e3) && e3.Length > 0,
            "P9: a non-HIT packet must carry entity_index=NONE; decoding must reject otherwise");

        log($"{GateName} P9: v1.4 contract verified (index used verbatim, NONE refused, "
            + "non-HIT+index rejected)");
    }

    private static string LogSince(MainViewModel vm, int index)
    {
        var sb = new System.Text.StringBuilder();
        for (int i = Math.Max(0, index); i < vm.ConsoleLines.Count; i++)
            sb.AppendLine(vm.ConsoleLines[i]);
        return sb.ToString();
    }
}
