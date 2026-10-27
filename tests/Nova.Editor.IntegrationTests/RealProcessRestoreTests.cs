using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using Nova.Editor.Services;
using Nova.Editor.Services.Layout;
using Nova.Editor.Views.Docking;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Core.Docking;
using Nova.UI.Core.Layout;

namespace Nova.Editor.IntegrationTests;

/// <summary>
/// Starts the real editor executable twice to prove that dock recovery comes
/// from the persisted file, not from an in-memory workspace of the test host.
/// </summary>
[TestClass]
public sealed class RealProcessRestoreTests
{
    private const uint WmClose = 0x0010;
    private const int SwMaximize = 3;
    private const int SwRestore = 9;
    private static readonly TimeSpan WindowTimeout = TimeSpan.FromSeconds(30);
    private static readonly TimeSpan ExitTimeout = TimeSpan.FromSeconds(60);
    private static readonly TimeSpan NoFloatGrace = TimeSpan.FromSeconds(2);

    [TestMethod]
    public async Task Restarted_editor_process_restores_the_persisted_float_and_rewrites_the_layout()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process restart gate requires an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d7c-real-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");

        try
        {
            var expectedDock = CreateRestorableSnapshot();
            AssertExpectedDock(expectedDock);

            var seeded = WorkspaceLayout.Default with
            {
                Dock = expectedDock,
                HierarchyWidth = 236,
            };
            await WriteNonCanonicalAsync(layoutPath, seeded);

            var firstRun = await RunEditorUntilCloseAsync(appPath, layoutPath);
            Assert.IsTrue(
                firstRun.MaximizedFloat,
                $"The restored float window must be maximized. Float title: {firstRun.FloatTitle}");
            StringAssert.Contains(firstRun.FloatTitle!, "Console");

            var firstSaved = await ReadCanonicalLayoutAsync(layoutPath);
            Assert.AreEqual(236d, firstSaved.HierarchyWidth, 0.001,
                "The shell preferences restored by the first process must be saved back.");
            AssertDock(expectedDock, firstSaved.Dock);

            // A distinct shell preference proves the second process read the file
            // written by the first process and did not reuse test-host state.
            await WriteNonCanonicalAsync(layoutPath, firstSaved with { HierarchyWidth = 252 });

            var secondRun = await RunEditorUntilCloseAsync(appPath, layoutPath);
            Assert.IsTrue(
                secondRun.MaximizedFloat,
                $"The restarted float window must be maximized. Float title: {secondRun.FloatTitle}");
            StringAssert.Contains(secondRun.FloatTitle!, "Console");

            var secondSaved = await ReadCanonicalLayoutAsync(layoutPath);
            Assert.AreEqual(252d, secondSaved.HierarchyWidth, 0.001,
                "The second process must persist the shell preferences it restored.");
            AssertDock(expectedDock, secondSaved.Dock);
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    [TestMethod]
    public async Task Exit_final_save_failure_does_not_block_close_or_overwrite_the_layout()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process restart gate requires an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d7c-locked-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");
        FileStream? locked = null;
        string? beforeLock = null;

        try
        {
            var expectedDock = CreateRestorableSnapshot();
            await WriteNonCanonicalAsync(
                layoutPath,
                WorkspaceLayout.Default with { Dock = expectedDock, HierarchyWidth = 236 });

            var run = await RunEditorUntilCloseAsync(
                appPath,
                layoutPath,
                beforeClose: _ =>
                {
                    locked = new FileStream(
                        layoutPath,
                        FileMode.Open,
                        FileAccess.ReadWrite,
                        FileShare.None);
                    using var reader = new StreamReader(
                        locked,
                        Encoding.UTF8,
                        detectEncodingFromByteOrderMarks: true,
                        bufferSize: 1024,
                        leaveOpen: true);
                    beforeLock = reader.ReadToEnd();
                    return Task.CompletedTask;
                });

            Assert.IsTrue(run.MaximizedFloat, "The suspended float must be restored before shutdown.");
            StringAssert.Contains(run.FloatTitle!, "Console");

            locked?.Dispose();
            locked = null;

            // The final save could not replace the locked file; the exit path must
            // still complete and the seeded floating layout must remain intact.
            Assert.IsNotNull(beforeLock);
            Assert.AreEqual(beforeLock, await File.ReadAllTextAsync(layoutPath));
            var parsed = WorkspaceLayoutSerializer.Deserialize(beforeLock);
            Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
            AssertDock(expectedDock, parsed.Layout.Dock);
        }
        finally
        {
            locked?.Dispose();
            TryDeleteDirectory(directory);
        }
    }

    [TestMethod]
    public async Task Locked_layout_at_startup_is_neither_partially_restored_nor_overwritten()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process restart gate requires an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d7d-read-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");
        FileStream? locked = null;

        try
        {
            var expectedDock = CreateRestorableSnapshot();
            await WriteNonCanonicalAsync(
                layoutPath,
                WorkspaceLayout.Default with { Dock = expectedDock, HierarchyWidth = 236 });
            var original = await File.ReadAllTextAsync(layoutPath);

            locked = new FileStream(
                layoutPath,
                FileMode.Open,
                FileAccess.ReadWrite,
                FileShare.None);

            var run = await RunEditorUntilCloseAsync(
                appPath,
                layoutPath,
                expectFloat: false,
                beforeClose: _ =>
                {
                    // The file becomes readable again before shutdown. The failed
                    // read must still keep this process from replacing data it
                    // never interpreted.
                    locked.Dispose();
                    locked = null;
                    return Task.CompletedTask;
                });

            Assert.IsNull(run.FloatTitle, "No float may be restored from an unreadable file.");
            Assert.AreEqual(
                original,
                await File.ReadAllTextAsync(layoutPath),
                "A layout that could not be read must be left byte-identical.");
            Assert.AreEqual(
                0,
                Directory.GetFiles(directory, "layout.json.corrupt-*").Length,
                "A read failure is not corruption and must not quarantine the file.");
            Assert.AreEqual(0, Directory.GetFiles(directory, "*.tmp").Length);

            // The untouched file is still valid, so the next process must restore it.
            var restored = await RunEditorUntilCloseAsync(appPath, layoutPath);
            Assert.IsTrue(restored.FloatObserved, "The untouched layout must restore its float.");
            Assert.IsTrue(restored.MaximizedFloat, "The restored float must be maximized.");
            StringAssert.Contains(restored.FloatTitle!, "Console");

            var saved = await ReadCanonicalLayoutAsync(layoutPath);
            Assert.AreEqual(236d, saved.HierarchyWidth, 0.001);
            AssertDock(expectedDock, saved.Dock);
        }
        finally
        {
            locked?.Dispose();
            TryDeleteDirectory(directory);
        }
    }

    [TestMethod]
    public async Task Corrupt_layout_at_startup_is_quarantined_and_replaced_by_a_valid_default()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process restart gate requires an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d7d-corrupt-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");

        try
        {
            const string corrupt = "{ \"schemaVersion\": 2, \"dock\": { \"main\": {";
            await File.WriteAllTextAsync(layoutPath, corrupt);

            var corruptRun = await RunEditorUntilCloseAsync(appPath, layoutPath, expectFloat: false);
            Assert.IsNull(corruptRun.FloatTitle, "A corrupt file must not restore any float.");

            var quarantined = Directory.GetFiles(directory, "layout.json.corrupt-*");
            Assert.AreEqual(
                1,
                quarantined.Length,
                "The corrupt layout must be quarantined exactly once.");
            Assert.AreEqual(
                corrupt,
                await File.ReadAllTextAsync(quarantined[0]),
                "Quarantine must preserve the original bytes.");

            var saved = await ReadCanonicalLayoutAsync(layoutPath);
            Assert.IsNotNull(saved.Dock);
            Assert.AreEqual(0, saved.Dock!.Floats.Length);
            Assert.IsTrue(SnapshotContainsPanel(saved.Dock.Main, "editor.hierarchy"));
            Assert.IsTrue(SnapshotContainsPanel(saved.Dock.Main, "editor.console"));

            var restartedRun = await RunEditorUntilCloseAsync(appPath, layoutPath, expectFloat: false);
            Assert.IsNull(restartedRun.FloatTitle);

            var restarted = await ReadCanonicalLayoutAsync(layoutPath);
            Assert.AreEqual(0, restarted.Dock!.Floats.Length);
            Assert.AreEqual(
                1,
                Directory.GetFiles(directory, "layout.json.corrupt-*").Length,
                "The valid replacement must load without being quarantined.");
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    [TestMethod]
    public async Task Restored_float_returns_to_the_main_workspace_on_system_close_and_stays_after_restart()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process restart gate requires an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d7d-redock-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");

        try
        {
            var expectedDock = CreateRestorableSnapshot();
            await WriteNonCanonicalAsync(
                layoutPath,
                WorkspaceLayout.Default with { Dock = expectedDock, HierarchyWidth = 236 });

            var firstRun = await RunEditorUntilCloseAsync(
                appPath,
                layoutPath,
                beforeClose: async windows =>
                {
                    Assert.AreNotEqual(IntPtr.Zero, windows.Float);
                    Assert.IsTrue(
                        PostMessage(windows.Float, WmClose, IntPtr.Zero, IntPtr.Zero),
                        "WM_CLOSE could not be posted to the floating window.");
                    Assert.IsTrue(
                        await WaitForConditionAsync(
                            () => FindWindow(windows.Process.Id, IsFloatWindow) == IntPtr.Zero,
                            TimeSpan.FromSeconds(15)),
                        "The floating window did not close after WM_CLOSE.");
                    Assert.IsTrue(
                        await WaitForConditionAsync(
                            () => LayoutHasNoFloatingRoots(layoutPath),
                            TimeSpan.FromSeconds(15)),
                        "The re-docked layout was not saved before the main window closed.");
                });

            StringAssert.Contains(firstRun.FloatTitle!, "Console");

            var saved = await ReadCanonicalLayoutAsync(layoutPath);
            Assert.AreEqual(0, saved.Dock!.Floats.Length, "The closed float must not be saved.");
            Assert.IsTrue(
                SnapshotContainsPanel(saved.Dock.Main, "editor.console"),
                "The re-docked console panel must be in the main root.");
            Assert.IsTrue(
                SnapshotContainsPanel(saved.Dock.Main, "editor.scene"),
                "The re-docked scene panel must be in the main root.");

            var secondRun = await RunEditorUntilCloseAsync(appPath, layoutPath, expectFloat: false);
            Assert.IsNull(secondRun.FloatTitle, "The re-docked float must not reappear.");

            var restarted = await ReadCanonicalLayoutAsync(layoutPath);
            Assert.AreEqual(0, restarted.Dock!.Floats.Length);
            Assert.IsTrue(SnapshotContainsPanel(restarted.Dock.Main, "editor.console"));
            Assert.IsTrue(SnapshotContainsPanel(restarted.Dock.Main, "editor.scene"));
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    [TestMethod]
    public async Task Explicitly_closed_panel_stays_closed_across_real_process_restarts()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process restart gate requires an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d7d-close-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");

        try
        {
            // Close through the production facade so the seeded file is produced
            // by the same code path the shell uses, not by editing JSON.
            var producer = EditorWorkspace.CreateSample(layoutPath);
            var viewModel = new EditorShellViewModel(producer);
            using (var dock = new EditorDockWorkspace(
                       viewModel,
                       panelFactory: new EditorDockPanelFactory(viewModel)))
            {
                var closed = dock.RemovePanel(DockPanelIds.Console);
                Assert.IsTrue(closed.Success, closed.Diagnostic);

                producer.AttachDockPersistence(dock, dirty: true);
                var saved = await producer.SaveLayoutAsync();
                Assert.AreEqual(LayoutSaveStatus.Saved, saved.Status, saved.Error);
            }

            var seeded = await ReadCanonicalLayoutAsync(layoutPath);
            Assert.IsFalse(SnapshotContainsPanel(seeded.Dock?.Main, "editor.console"));
            Assert.AreEqual(0, seeded.Dock!.Floats.Length);

            var firstRun = await RunEditorUntilCloseAsync(appPath, layoutPath, expectFloat: false);
            Assert.IsNull(firstRun.FloatTitle);

            var firstSaved = await ReadCanonicalLayoutAsync(layoutPath);
            Assert.IsFalse(
                SnapshotContainsPanel(firstSaved.Dock?.Main, "editor.console"),
                "An explicitly closed panel was reintroduced by the first restart.");
            Assert.AreEqual(0, firstSaved.Dock!.Floats.Length);

            var secondRun = await RunEditorUntilCloseAsync(appPath, layoutPath, expectFloat: false);
            Assert.IsNull(secondRun.FloatTitle);

            var secondSaved = await ReadCanonicalLayoutAsync(layoutPath);
            Assert.IsFalse(
                SnapshotContainsPanel(secondSaved.Dock?.Main, "editor.console"),
                "An explicitly closed panel was reintroduced by the second restart.");
            Assert.AreEqual(0, secondSaved.Dock!.Floats.Length);
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    [TestMethod]
    public async Task Maximizing_a_restored_float_persists_state_and_restore_bounds_across_a_real_restart()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process restart gate requires an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d7d-max-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");

        try
        {
            var expectedDock = CreateRestorableSnapshot();
            var normalDock = expectedDock with
            {
                Floats = expectedDock.Floats
                    .Select(root => root with { Maximized = false })
                    .ToArray(),
            };
            await WriteNonCanonicalAsync(
                layoutPath,
                WorkspaceLayout.Default with { Dock = normalDock, HierarchyWidth = 236 });

            var firstRun = await RunEditorUntilCloseAsync(
                appPath,
                layoutPath,
                expectMaximized: false,
                beforeClose: async windows =>
                {
                    Assert.IsFalse(
                        windows.MaximizedFloat,
                        "The seeded float must start unmaximized.");
                    ShowWindow(windows.Float, SwMaximize);
                    Assert.IsTrue(
                        await WaitForConditionAsync(
                            () => IsZoomed(windows.Float),
                            TimeSpan.FromSeconds(10)),
                        "The floating window did not become maximized.");
                    Assert.IsTrue(
                        await WaitForConditionAsync(
                            () => TryReadSingleFloat(layoutPath, out var floatRoot)
                                  && floatRoot is { Maximized: true },
                            TimeSpan.FromSeconds(15)),
                        "The maximized state was not saved before the main window closed.");
                });

            Assert.IsTrue(firstRun.FloatObserved);
            StringAssert.Contains(firstRun.FloatTitle!, "Console");

            var firstSaved = await ReadCanonicalLayoutAsync(layoutPath);
            var firstFloat = firstSaved.Dock!.Floats.Single();
            Assert.IsTrue(firstFloat.Maximized, "Maximizing must set the persisted maximized flag.");
            Assert.AreEqual(180d, firstFloat.Bounds.X, 1d);
            Assert.AreEqual(160d, firstFloat.Bounds.Y, 1d);
            Assert.AreEqual(720d, firstFloat.Bounds.Width, 1d);
            Assert.AreEqual(500d, firstFloat.Bounds.Height, 1d);

            var secondRun = await RunEditorUntilCloseAsync(appPath, layoutPath);
            Assert.IsTrue(secondRun.FloatObserved, "The maximized float must be restored.");
            Assert.IsTrue(secondRun.MaximizedFloat, "The float must stay maximized after a restart.");
            StringAssert.Contains(secondRun.FloatTitle!, "Console");

            var secondSaved = await ReadCanonicalLayoutAsync(layoutPath);
            var secondFloat = secondSaved.Dock!.Floats.Single();
            Assert.IsTrue(secondFloat.Maximized);
            Assert.AreEqual(firstFloat.Bounds.X, secondFloat.Bounds.X, 0.001);
            Assert.AreEqual(firstFloat.Bounds.Y, secondFloat.Bounds.Y, 0.001);
            Assert.AreEqual(firstFloat.Bounds.Width, secondFloat.Bounds.Width, 0.001);
            Assert.AreEqual(firstFloat.Bounds.Height, secondFloat.Bounds.Height, 0.001);
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    [TestMethod]
    public async Task Unmaximizing_a_restored_float_persists_normal_bounds_across_a_real_restart()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process restart gate requires an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d7d-restore-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");

        try
        {
            var expectedDock = CreateRestorableSnapshot();
            await WriteNonCanonicalAsync(
                layoutPath,
                WorkspaceLayout.Default with { Dock = expectedDock, HierarchyWidth = 236 });

            var firstRun = await RunEditorUntilCloseAsync(
                appPath,
                layoutPath,
                beforeClose: async windows =>
                {
                    Assert.IsTrue(
                        windows.MaximizedFloat,
                        "The float must be maximized before the restore request.");
                    // The persisted maximized state is applied while the host
                    // is still settling; a single restore request can race
                    // that startup work and be re-maximized, so keep sending
                    // the restore until the state sticks.
                    Assert.IsTrue(
                        await WaitForConditionAsync(
                            () =>
                            {
                                if (!IsZoomed(windows.Float))
                                    return true;
                                ShowWindow(windows.Float, SwRestore);
                                return false;
                            },
                            TimeSpan.FromSeconds(10)),
                        "The floating window did not leave the maximized state.");
                    Assert.IsTrue(
                        await WaitForConditionAsync(
                            () => TryReadSingleFloat(layoutPath, out var floatRoot)
                                  && floatRoot is { Maximized: false },
                            TimeSpan.FromSeconds(15)),
                        "The unmaximized state was not saved before the main window closed.");
                });

            StringAssert.Contains(firstRun.FloatTitle!, "Console");

            var firstSaved = await ReadCanonicalLayoutAsync(layoutPath);
            var firstFloat = firstSaved.Dock!.Floats.Single();
            Assert.IsFalse(firstFloat.Maximized, "Unmaximizing must clear the persisted maximized flag.");
            Assert.AreEqual(180d, firstFloat.Bounds.X, 1d);
            Assert.AreEqual(160d, firstFloat.Bounds.Y, 1d);
            Assert.AreEqual(720d, firstFloat.Bounds.Width, 1d);
            Assert.AreEqual(500d, firstFloat.Bounds.Height, 1d);

            var secondRun = await RunEditorUntilCloseAsync(
                appPath,
                layoutPath,
                expectMaximized: false);
            Assert.IsTrue(secondRun.FloatObserved, "The float must still be restored.");
            Assert.IsFalse(secondRun.MaximizedFloat, "The float must stay unmaximized after a restart.");
            StringAssert.Contains(secondRun.FloatTitle!, "Console");

            var secondSaved = await ReadCanonicalLayoutAsync(layoutPath);
            var secondFloat = secondSaved.Dock!.Floats.Single();
            Assert.IsFalse(secondFloat.Maximized);
            Assert.AreEqual(firstFloat.Bounds.X, secondFloat.Bounds.X, 0.001);
            Assert.AreEqual(firstFloat.Bounds.Y, secondFloat.Bounds.Y, 0.001);
            Assert.AreEqual(firstFloat.Bounds.Width, secondFloat.Bounds.Width, 0.001);
            Assert.AreEqual(firstFloat.Bounds.Height, secondFloat.Bounds.Height, 0.001);
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    // ADR-011 DM-RP-01 — O-2 验收（O-2.2 / O-2.4 / O-2.5）：磁盘上落在所有显示器工作区之外
    // 的浮动根，恢复时由宿主钳制到可见工作区；真实窗口外框必须完整落在某个工作区内，磁盘
    // 模型必须不再是播种的越界值。三条前提断言（屏枚举非空、探针确实在所有工作区之外、窗口
    // 矩形可读）任一不成立就先失败在前提上——只有前提全绿而窗口仍在外面，才是产品失败。
    [TestMethod]
    public async Task DmRp01_an_off_screen_float_is_restored_inside_a_work_area()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process geometry gate requires an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var workAreas = ReadWorkAreas();
        Assert.IsTrue(workAreas.Count > 0, "The desktop reports no monitors.");
        var offScreen = new DockBounds(20000d, 20000d, 640d, 480d);
        foreach (var area in workAreas)
        {
            Assert.IsFalse(
                Intersects(offScreen, area),
                "The gate premise is broken: the probe bounds are already on a screen.");
        }

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d10c-offscreen-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");
        var evidencePath = Path.Combine(
            Path.GetTempPath(),
            "nova-dm-rp-01-evidence.txt");

        try
        {
            await WriteNonCanonicalAsync(
                layoutPath,
                WorkspaceLayout.Default with
                {
                    Dock = CreateOffScreenFloatSnapshot(offScreen),
                    HierarchyWidth = 236,
                });

            NativeRect? sampled = null;
            await RunEditorUntilCloseAsync(
                appPath,
                layoutPath,
                expectFloat: true,
                expectMaximized: false,
                beforeClose: async windows =>
                {
                    sampled = await WaitForSettledWindowRectAsync(
                        windows.Float,
                        workAreas,
                        WindowTimeout);
                });

            Assert.IsTrue(
                sampled.HasValue,
                "GetWindowRect could never read the restored floating window.");
            var observed = sampled.GetValueOrDefault();
            var saved = await ReadCanonicalLayoutAsync(layoutPath);
            var savedBounds = ToBounds(saved.Dock!.Floats.Single().Bounds);
            var observedBounds = observed.ToBounds();
            var visible = workAreas.Any(area => Contains(area, observedBounds));

            File.AppendAllText(
                evidencePath,
                FormatEvidence(
                    "DM-RP-01",
                    "outer-frame-fully-inside-a-work-area",
                    workAreas,
                    offScreen,
                    savedBounds,
                    observedBounds,
                    visible));

            Assert.IsTrue(
                visible,
                string.Concat(
                    "The restored outer frame still crosses every work area: ",
                    $"observed={Bounds(observedBounds)} areas={FormatAreas(workAreas)}"));
            Assert.AreNotEqual(
                offScreen,
                savedBounds,
                string.Concat(
                    "The persisted model still holds the off-screen rectangle: ",
                    $"seeded={Bounds(offScreen)} persisted={Bounds(savedBounds)}"));
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    private static DockWorkspaceSnapshot CreateOffScreenFloatSnapshot(DockBounds bounds)
    {
        var workspace = DockWorkspace.CreateDefault();
        var floated = workspace.FloatPanel(DockPanelIds.Console, bounds);
        Assert.IsTrue(floated.Success, floated.Diagnostic);
        return DockWorkspaceSnapshotCodec.FromWorkspace(floated.Workspace!);
    }

    // ADR-011 DM-RP-02 — O-2 验收（O-2.2 / O-2.5）：与某块工作区相交、但没有被任何一块完整
    // 包含的浮动根，恢复时选择交叠面积最大的工作区并钳制进去；真实窗口外框必须完整落在工作区
    // 内，磁盘模型必须不再是播种值的原样拷贝。播种矩形由当场读到的工作区推导，所以前提断言
    // （确实相交、确实未被包含）在拓扑改变时会先失败，把"门禁环境假设不成立"与"产品行为"
    // 分开。
    [TestMethod]
    public async Task DmRp02_a_partially_overhanging_float_is_restored_inside_the_largest_overlap_work_area()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process geometry gate requires an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var workAreas = ReadWorkAreas();
        Assert.IsTrue(workAreas.Count > 0, "The desktop reports no monitors.");
        var anchor = workAreas.FirstOrDefault(area => area.Primary) ?? workAreas[0];
        var overhang = new DockBounds(
            anchor.Right - 100d,
            anchor.Bottom - 60d,
            640d,
            480d);
        Assert.IsTrue(
            workAreas.Any(area => Intersects(overhang, area)),
            "The gate premise is broken: the probe bounds do not overlap any work area.");
        Assert.IsFalse(
            workAreas.Any(area => Contains(area, overhang)),
            "The gate premise is broken: the probe bounds are already fully on screen.");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d10c-overhang-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");
        var evidencePath = Path.Combine(
            Path.GetTempPath(),
            "nova-dm-rp-02-evidence.txt");

        try
        {
            await WriteNonCanonicalAsync(
                layoutPath,
                WorkspaceLayout.Default with
                {
                    Dock = CreateOffScreenFloatSnapshot(overhang),
                    HierarchyWidth = 236,
                });

            var observedBounds = await RunGeometryRestoreProbeAsync(
                appPath,
                layoutPath,
                workAreas,
                "The partially overhanging float was not restored.");

            var savedBounds = ToBounds((await ReadCanonicalLayoutAsync(layoutPath))
                .Dock!.Floats.Single().Bounds);
            var visible = workAreas.Any(area => Contains(area, observedBounds));
            File.AppendAllText(
                evidencePath,
                FormatEvidence(
                    "DM-RP-02",
                    "outer-frame-fully-inside-a-work-area",
                    workAreas,
                    overhang,
                    savedBounds,
                    observedBounds,
                    visible));

            Assert.IsTrue(
                visible,
                string.Concat(
                    "The restored outer frame still crosses its work area: ",
                    $"seeded={Bounds(overhang)} observed={Bounds(observedBounds)} ",
                    $"areas={FormatAreas(workAreas)}"));
            Assert.AreNotEqual(
                overhang,
                savedBounds,
                string.Concat(
                    "The persisted model still holds the partially overhanging rectangle: ",
                    $"seeded={Bounds(overhang)} persisted={Bounds(savedBounds)}"));
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    // ADR-011 DM-RP-03 — O-2 验收（O-2.4）：尺寸超过任何一块工作区的浮动根，恢复时必须
    // "先缩小再定位"。两条判据分别检查产品模型（磁盘宽度/高度必须小于播种值，证明缩小发生在
    // 产品层而不是只被平台限制）与真实窗口外框（必须完整落在某个工作区内）。若只有 Windows
    // 的跟踪尺寸上限限制了窗口而磁盘值未变，模型侧判据会失败，消息会指出这一原因。
    [TestMethod]
    public async Task DmRp03_an_oversized_float_is_shrunk_before_it_is_placed()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process geometry gate requires an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var workAreas = ReadWorkAreas();
        Assert.IsTrue(workAreas.Count > 0, "The desktop reports no monitors.");
        var anchor = workAreas.FirstOrDefault(area => area.Primary) ?? workAreas[0];
        var oversized = new DockBounds(
            anchor.Left + 20d,
            anchor.Top + 20d,
            anchor.Right - anchor.Left + 400d,
            anchor.Bottom - anchor.Top + 300d);
        Assert.IsTrue(
            workAreas.Any(area => Intersects(oversized, area)),
            "The gate premise is broken: the probe bounds do not overlap any work area.");
        Assert.IsTrue(
            workAreas.All(area => !Contains(area, oversized)),
            "The gate premise is broken: a work area can already contain the probe bounds.");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d10c-oversized-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");
        var evidencePath = Path.Combine(
            Path.GetTempPath(),
            "nova-dm-rp-03-evidence.txt");

        try
        {
            await WriteNonCanonicalAsync(
                layoutPath,
                WorkspaceLayout.Default with
                {
                    Dock = CreateOffScreenFloatSnapshot(oversized),
                    HierarchyWidth = 236,
                });

            var observedBounds = await RunGeometryRestoreProbeAsync(
                appPath,
                layoutPath,
                workAreas,
                "The oversized float was not restored.");

            var savedBounds = ToBounds((await ReadCanonicalLayoutAsync(layoutPath))
                .Dock!.Floats.Single().Bounds);
            var visible = workAreas.Any(area => Contains(area, observedBounds));
            var shrunk = savedBounds.Width < oversized.Width - 1d
                         && savedBounds.Height < oversized.Height - 1d;
            File.AppendAllText(
                evidencePath,
                FormatEvidence(
                    "DM-RP-03",
                    "outer-frame-visible-and-model-shrunk",
                    workAreas,
                    oversized,
                    savedBounds,
                    observedBounds,
                    visible && shrunk));

            Assert.IsTrue(
                visible,
                string.Concat(
                    "The restored outer frame still does not fit inside a work area: ",
                    $"seeded={Bounds(oversized)} observed={Bounds(observedBounds)} ",
                    $"areas={FormatAreas(workAreas)}"));
            Assert.IsTrue(
                shrunk,
                string.Concat(
                    "The persisted model was not shrunk, so O-2.4 was only satisfied, if at all, ",
                    $"by the platform: seeded={Bounds(oversized)} persisted={Bounds(savedBounds)}. ",
                    "Check whether Windows limited the tracking size before reading this as a fix."));
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    // ADR-011 DM-RP-04 — 现状测量（O-5.1…O-5.4）：最大化／还原快速连续切换后，窗口停在
    // Normal，落盘的必须是还原后的正常几何，而不是任何一帧最大化矩形或过渡矩形；中间帧是
    // 本门禁刻意制造的，所以不引入任何固定延迟，只用真实状态与几何观测判定。
    [TestMethod]
    public async Task DmRp04_rapid_maximize_and_restore_cycles_persist_the_settled_normal_bounds()
    {
        if (!OperatingSystem.IsWindows())
            Assert.Inconclusive("The real-process geometry gate requires an interactive Windows desktop.");

        var appPath = ResolveAppPath();
        Assert.IsTrue(File.Exists(appPath), $"The real editor executable was not built: {appPath}");

        var workAreas = ReadWorkAreas();
        Assert.IsTrue(workAreas.Count > 0, "The desktop reports no monitors.");

        var directory = Path.Combine(Path.GetTempPath(), $"nova-d10b-churn-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var layoutPath = Path.Combine(directory, "layout.json");
        var evidencePath = Path.Combine(
            Path.GetTempPath(),
            "nova-dm-rp-04-evidence.txt");

        try
        {
            var expectedDock = CreateRestorableSnapshot();
            var normalDock = expectedDock with
            {
                Floats = expectedDock.Floats
                    .Select(root => root with { Maximized = false })
                    .ToArray(),
            };
            await WriteNonCanonicalAsync(
                layoutPath,
                WorkspaceLayout.Default with { Dock = normalDock, HierarchyWidth = 236 });

            var normalBefore = default(DockBounds);
            var maximizedFrame = default(DockBounds);
            var settledFrame = default(DockBounds);

            var firstRun = await RunEditorUntilCloseAsync(
                appPath,
                layoutPath,
                expectMaximized: false,
                beforeClose: async windows =>
                {
                    normalBefore = RequireWindowRect(windows.Float).ToBounds();
                    Assert.IsFalse(
                        windows.MaximizedFloat,
                        "The seeded float must start unmaximized.");

                    // Premise: maximizing must genuinely change the geometry, otherwise the
                    // oracle below could not tell a transition frame from the normal frame.
                    ShowWindow(windows.Float, SwMaximize);
                    Assert.IsTrue(
                        await WaitForConditionAsync(
                            () => IsZoomed(windows.Float),
                            TimeSpan.FromSeconds(10)),
                        "The floating window did not become maximized.");
                    maximizedFrame = RequireWindowRect(windows.Float).ToBounds();
                    Assert.IsTrue(
                        maximizedFrame.Width > normalBefore.Width,
                        string.Concat(
                            "The gate premise is broken: maximizing did not change the geometry. ",
                            $"normal={Bounds(normalBefore)} maximized={Bounds(maximizedFrame)}"));

                    ShowWindow(windows.Float, SwRestore);
                    Assert.IsTrue(
                        await WaitForConditionAsync(
                            () => !IsZoomed(windows.Float),
                            TimeSpan.FromSeconds(10)),
                        "The floating window did not leave the maximized state.");

                    // O-5.3: the two commands are issued back to back, with no settling
                    // delay, so the platform produces the intermediate frames on purpose.
                    for (var cycle = 0; cycle < 6; cycle++)
                    {
                        ShowWindow(windows.Float, SwMaximize);
                        ShowWindow(windows.Float, SwRestore);
                    }

                    Assert.IsTrue(
                        await WaitForConditionAsync(
                            () =>
                            {
                                if (IsZoomed(windows.Float))
                                    ShowWindow(windows.Float, SwRestore);
                                return !IsZoomed(windows.Float);
                            },
                            TimeSpan.FromSeconds(10)),
                        "The window did not settle in the Normal state after the rapid cycles.");

                    // O-5.3 is judged by observation, not by a fixed delay: the persisted
                    // record must say Normal and two consecutive polls must agree on the
                    // geometry before the value is considered past the transition frames.
                    DockBounds? previousPersisted = null;
                    Assert.IsTrue(
                        await WaitForConditionAsync(
                            () =>
                            {
                                if (!TryReadSingleFloat(layoutPath, out var floatRoot)
                                    || floatRoot!.Maximized)
                                {
                                    return false;
                                }

                                var current = ToBounds(floatRoot.Bounds);
                                var stable = previousPersisted is { } last && last == current;
                                previousPersisted = current;
                                return stable;
                            },
                            TimeSpan.FromSeconds(20)),
                        "The persisted geometry never stabilized in the Normal state.");
                    settledFrame = RequireWindowRect(windows.Float).ToBounds();
                });

            Assert.IsTrue(firstRun.FloatObserved);
            StringAssert.Contains(firstRun.FloatTitle!, "Console");

            var firstSaved = await ReadCanonicalLayoutAsync(layoutPath);
            var firstFloat = firstSaved.Dock!.Floats.Single();
            var persistedBounds = ToBounds(firstFloat.Bounds);

            File.AppendAllText(
                evidencePath,
                string.Concat(
                    DateTime.UtcNow.ToString("O"),
                    " gate=DM-RP-04 monitors=",
                    workAreas.Count.ToString(System.Globalization.CultureInfo.InvariantCulture),
                    " areas=[",
                    string.Join(
                        " | ",
                        workAreas.Select(area =>
                            $"{area.Device} ({area.Left},{area.Top})-({area.Right},{area.Bottom}) primary={area.Primary}")),
                    "] normalBefore=",
                    Bounds(normalBefore),
                    " maximizedFrame=",
                    Bounds(maximizedFrame),
                    " settledFrame=",
                    Bounds(settledFrame),
                    " persisted=",
                    Bounds(persistedBounds),
                    " persistedMaximized=",
                    firstFloat.Maximized.ToString(System.Globalization.CultureInfo.InvariantCulture),
                    Environment.NewLine));

            Assert.IsFalse(
                firstFloat.Maximized,
                "The persisted flag must follow the settled Normal state.");
            Assert.AreEqual(
                180d,
                persistedBounds.X,
                1d,
                string.Concat(
                    "A transition frame was persisted instead of the settled normal geometry. ",
                    $"normalBefore={Bounds(normalBefore)} maximizedFrame={Bounds(maximizedFrame)} ",
                    $"settledFrame={Bounds(settledFrame)} persisted={Bounds(persistedBounds)}"));
            Assert.AreEqual(160d, persistedBounds.Y, 1d);
            Assert.AreEqual(720d, persistedBounds.Width, 1d);
            Assert.AreEqual(500d, persistedBounds.Height, 1d);

            // O-5 also covers the process restart after the churn.
            var secondRun = await RunEditorUntilCloseAsync(
                appPath,
                layoutPath,
                expectMaximized: false);
            Assert.IsTrue(secondRun.FloatObserved, "The float must still be restored.");
            Assert.IsFalse(secondRun.MaximizedFloat, "The float must stay unmaximized after a restart.");

            var secondSaved = await ReadCanonicalLayoutAsync(layoutPath);
            var secondFloat = secondSaved.Dock!.Floats.Single();
            Assert.IsFalse(secondFloat.Maximized);
            Assert.AreEqual(firstFloat.Bounds.X, secondFloat.Bounds.X, 0.001);
            Assert.AreEqual(firstFloat.Bounds.Y, secondFloat.Bounds.Y, 0.001);
            Assert.AreEqual(firstFloat.Bounds.Width, secondFloat.Bounds.Width, 0.001);
            Assert.AreEqual(firstFloat.Bounds.Height, secondFloat.Bounds.Height, 0.001);
        }
        finally
        {
            TryDeleteDirectory(directory);
        }
    }

    private static async Task<DockBounds> RunGeometryRestoreProbeAsync(
        string appPath,
        string layoutPath,
        List<WorkArea> workAreas,
        string restoreFailureMessage)
    {
        NativeRect? sampled = null;

        var run = await RunEditorUntilCloseAsync(
            appPath,
            layoutPath,
            expectFloat: true,
            expectMaximized: false,
            beforeClose: async windows =>
            {
                Assert.AreNotEqual(IntPtr.Zero, windows.Float, restoreFailureMessage);
                sampled = await WaitForSettledWindowRectAsync(
                    windows.Float,
                    workAreas,
                    WindowTimeout);
            });

        Assert.IsTrue(sampled.HasValue, $"The real window rectangle was never readable. {restoreFailureMessage}");
        Assert.IsTrue(run.FloatObserved, restoreFailureMessage);
        Assert.IsFalse(run.MaximizedFloat, "The probe layout must restore an unmaximized float.");
        return sampled.GetValueOrDefault().ToBounds();
    }

    // The host may place the window in two synchronous passes: the non-client
    // frame exists only after the platform window does. This waits for the
    // contract state itself (the outer frame fully inside a work area) with a
    // bounded deadline; it is an observation of the product state, not a fixed
    // delay inserted before an assertion (ADR-011 O-5).
    private static async Task<NativeRect?> WaitForSettledWindowRectAsync(
        IntPtr handle,
        List<WorkArea> workAreas,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        NativeRect? last = null;
        while (true)
        {
            if (GetWindowRect(handle, out var rect)
                && rect.Right > rect.Left
                && rect.Bottom > rect.Top)
            {
                last = rect;
                if (workAreas.Any(area => Contains(area, rect.ToBounds())))
                    return rect;
            }

            if (DateTime.UtcNow >= deadline)
                return last;

            await Task.Delay(50);
        }
    }

    private static NativeRect RequireWindowRect(IntPtr handle)
    {
        Assert.IsTrue(
            GetWindowRect(handle, out var rect),
            "GetWindowRect could not read the restored floating window.");
        return rect;
    }

    private static DockBounds ToBounds(DockBoundsSnapshot snapshot)
        => new(snapshot.X, snapshot.Y, snapshot.Width, snapshot.Height);

    private static string Bounds(DockBounds bounds)
        => string.Format(
            System.Globalization.CultureInfo.InvariantCulture,
            "{0:0.###},{1:0.###},{2:0.###},{3:0.###}",
            bounds.X,
            bounds.Y,
            bounds.Width,
            bounds.Height);

    private static string FormatAreas(List<WorkArea> workAreas)
        => string.Join(
            " | ",
            workAreas.Select(area =>
                $"{area.Device} ({area.Left},{area.Top})-({area.Right},{area.Bottom}) primary={area.Primary}"));

    private static string FormatEvidence(
        string gate,
        string criterion,
        List<WorkArea> workAreas,
        DockBounds seeded,
        DockBounds persisted,
        DockBounds observed,
        bool conclusion)
        => string.Concat(
            DateTime.UtcNow.ToString("O"),
            " gate=",
            gate,
            " criterion=",
            criterion,
            " monitors=",
            workAreas.Count.ToString(System.Globalization.CultureInfo.InvariantCulture),
            " areas=[",
            FormatAreas(workAreas),
            "] seeded=",
            Bounds(seeded),
            " persisted=",
            Bounds(persisted),
            " observedWindowRect=",
            Bounds(observed),
            " conclusion=",
            conclusion.ToString(System.Globalization.CultureInfo.InvariantCulture),
            Environment.NewLine);

    private static List<WorkArea> ReadWorkAreas()
        => System.Windows.Forms.Screen.AllScreens
            .Select(screen => new WorkArea(
                screen.WorkingArea.Left,
                screen.WorkingArea.Top,
                screen.WorkingArea.Right,
                screen.WorkingArea.Bottom,
                screen.DeviceName,
                screen.Primary))
            .ToList();

    private static bool Intersects(DockBounds bounds, WorkArea area)
        => bounds.X < area.Right
           && bounds.X + bounds.Width > area.Left
           && bounds.Y < area.Bottom
           && bounds.Y + bounds.Height > area.Top;

    private static bool Contains(WorkArea area, DockBounds bounds)
        => bounds.X >= area.Left
           && bounds.Y >= area.Top
           && bounds.X + bounds.Width <= area.Right
           && bounds.Y + bounds.Height <= area.Bottom;

    private sealed record WorkArea(
        int Left,
        int Top,
        int Right,
        int Bottom,
        string Device,
        bool Primary);

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeRect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;

        public readonly DockBounds ToBounds()
            => new(Left, Top, Right - Left, Bottom - Top);
    }

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetWindowRect(IntPtr handle, out NativeRect rect);

    private static async Task<RunEvidence> RunEditorUntilCloseAsync(
        string appPath,
        string layoutPath,
        bool expectFloat = true,
        bool expectMaximized = true,
        Func<WindowHandles, Task>? beforeClose = null)
    {
        var startInfo = new ProcessStartInfo(appPath)
        {
            UseShellExecute = false,
            WorkingDirectory = Path.GetDirectoryName(appPath)!,
        };
        startInfo.ArgumentList.Add("--layout-file");
        startInfo.ArgumentList.Add(layoutPath);

        using var process = Process.Start(startInfo)
                            ?? throw new InvalidOperationException(
                                $"Could not start the editor process: {appPath}");
        try
        {
            var mainWindow = await WaitForWindowAsync(
                process,
                handle => IsMainWindow(handle),
                "the main editor window");

            var floatWindow = IntPtr.Zero;
            string? floatTitle = null;
            if (expectFloat)
            {
                floatWindow = await WaitForWindowAsync(
                    process,
                    handle => IsFloatWindow(handle),
                    "the restored floating panel window");
                floatTitle = GetWindowText(floatWindow);

                if (expectMaximized)
                {
                    Assert.IsTrue(
                        await WaitForConditionAsync(
                            () => IsZoomed(floatWindow),
                            TimeSpan.FromSeconds(10)),
                        "The restored floating window did not become maximized.");
                }
                else
                {
                    Assert.IsFalse(
                        IsZoomed(floatWindow),
                        "The restored floating window must not be maximized.");
                }
            }
            else
            {
                await Task.Delay(NoFloatGrace);
                var unexpected = FindWindow(process.Id, IsFloatWindow);
                Assert.AreEqual(
                    IntPtr.Zero,
                    unexpected,
                    "A floating window was restored when the layout contains no floats.");
            }

            var maximizedBeforeClose = expectFloat && IsZoomed(floatWindow);
            if (beforeClose is not null)
            {
                await beforeClose(new WindowHandles(
                    process,
                    mainWindow,
                    floatWindow,
                    floatTitle,
                    maximizedBeforeClose));
            }

            var observed = expectFloat && FindWindow(process.Id, IsFloatWindow) != IntPtr.Zero;
            var maximized = observed && IsZoomed(floatWindow);

            Assert.IsTrue(
                PostMessage(mainWindow, WmClose, IntPtr.Zero, IntPtr.Zero),
                "WM_CLOSE could not be posted to the main editor window.");

            if (!await WaitForExitAsync(process, ExitTimeout))
                Assert.Fail($"The editor process did not exit within {ExitTimeout.TotalSeconds:0}s.");

            Assert.AreEqual(0, process.ExitCode, "The editor process reported a failing exit code.");
            return new RunEvidence(floatTitle, observed, maximized);
        }
        finally
        {
            if (!process.HasExited)
            {
                try
                {
                    process.Kill(entireProcessTree: true);
                }
                catch
                {
                    // Best-effort cleanup of the process started by this test.
                }

                await process.WaitForExitAsync();
            }
        }
    }

    private static DockWorkspaceSnapshot CreateRestorableSnapshot()
    {
        var workspace = DockWorkspace.CreateDefault();

        var floated = workspace.FloatPanel(
            DockPanelIds.Console,
            new DockBounds(120, 140, 480, 320));
        Assert.IsTrue(floated.Success, floated.Diagnostic);
        workspace = floated.Workspace!;

        var floatRoot = workspace.FloatingRoots.Single();
        var floatGroup = floatRoot.Root.EnumerateNodes().OfType<DockTabGroupNode>().Single();

        var moved = workspace.MovePanel(
            DockPanelIds.Scene,
            new DockTarget.Center(floatGroup.Id));
        Assert.IsTrue(moved.Success, moved.Diagnostic);
        workspace = moved.Workspace!;

        var normal = workspace.UpdateFloatBounds(
            floatRoot.Id,
            new DockBounds(180, 160, 720, 500),
            maximized: false);
        Assert.IsTrue(normal.Success, normal.Diagnostic);
        workspace = normal.Workspace!;

        var maximized = workspace.UpdateFloatBounds(
            floatRoot.Id,
            new DockBounds(0, 0, 1280, 800),
            maximized: true);
        Assert.IsTrue(maximized.Success, maximized.Diagnostic);
        workspace = maximized.Workspace!;

        var activated = workspace.ActivateTab(floatGroup.Id, DockPanelIds.Console);
        Assert.IsTrue(activated.Success, activated.Diagnostic);
        workspace = activated.Workspace!;

        return DockWorkspaceSnapshotCodec.FromWorkspace(workspace);
    }

    private static void AssertExpectedDock(DockWorkspaceSnapshot expected)
    {
        Assert.AreEqual(1, expected.Floats.Length);
        var floatRoot = expected.Floats[0];
        Assert.AreEqual("dock.float.1", floatRoot.Id);
        Assert.AreEqual(180d, floatRoot.Bounds.X, 0.001);
        Assert.AreEqual(160d, floatRoot.Bounds.Y, 0.001);
        Assert.AreEqual(720d, floatRoot.Bounds.Width, 0.001);
        Assert.AreEqual(500d, floatRoot.Bounds.Height, 0.001);
        Assert.IsTrue(floatRoot.Maximized);
        Assert.IsNotNull(floatRoot.Root);
        CollectionAssert.AreEqual(
            new[] { "editor.console", "editor.scene" },
            floatRoot.Root.Panels);
        Assert.AreEqual("editor.console", floatRoot.Root.ActivePanel);
    }

    private static void AssertDock(
        DockWorkspaceSnapshot expected,
        DockWorkspaceSnapshot? actual)
    {
        Assert.IsNotNull(actual, "The saved layout must contain a dock snapshot.");

        Assert.AreEqual(1, actual.Floats.Length);
        var floatRoot = actual.Floats[0];
        Assert.AreEqual("dock.float.1", floatRoot.Id);
        Assert.AreEqual(180d, floatRoot.Bounds.X, 0.001);
        Assert.AreEqual(160d, floatRoot.Bounds.Y, 0.001);
        Assert.AreEqual(720d, floatRoot.Bounds.Width, 0.001);
        Assert.AreEqual(500d, floatRoot.Bounds.Height, 0.001);
        Assert.IsTrue(floatRoot.Maximized, "The maximized state must survive the restart.");
        Assert.IsNotNull(floatRoot.Root);
        CollectionAssert.AreEqual(
            new[] { "editor.console", "editor.scene" },
            floatRoot.Root.Panels,
            "The floating panel order must survive the restart.");
        Assert.AreEqual(
            "editor.console",
            floatRoot.Root.ActivePanel,
            "The active floating tab must survive the restart.");

        // Serialize both snapshots with identical shell fields so the comparison
        // covers main/floating roots, node ids, weights, order and active tabs.
        Assert.AreEqual(
            WorkspaceLayoutSerializer.Serialize(WorkspaceLayout.Default with { Dock = expected }),
            WorkspaceLayoutSerializer.Serialize(WorkspaceLayout.Default with { Dock = actual }),
            "The main and floating dock roots must survive the restart unchanged.");
    }

    private static async Task WriteNonCanonicalAsync(string path, WorkspaceLayout layout)
    {
        var canonical = WorkspaceLayoutSerializer.Serialize(layout);
        // Whitespace never changes the parse result, so only the process's own
        // final save can make the file canonical again.
        await File.WriteAllTextAsync(path, "\r\n\t" + canonical + "\r\n");
    }

    private static async Task<WorkspaceLayout> ReadCanonicalLayoutAsync(string path)
    {
        var raw = await File.ReadAllTextAsync(path);
        var parsed = WorkspaceLayoutSerializer.Deserialize(raw);
        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.AreEqual(
            WorkspaceLayoutSerializer.Serialize(parsed.Layout),
            raw,
            "The editor exit path must write canonical layout JSON.");
        return parsed.Layout;
    }

    private static bool LayoutHasNoFloatingRoots(string path)
        => TryReadDockSnapshot(path, out var dock) && dock!.Floats.Length == 0;

    private static bool TryReadSingleFloat(string path, out DockFloatRootSnapshot? floatRoot)
    {
        floatRoot = null;
        if (!TryReadDockSnapshot(path, out var dock) || dock!.Floats.Length != 1)
            return false;

        floatRoot = dock.Floats[0];
        return true;
    }

    /// <summary>
    /// Opens with delete sharing so polling cannot make the editor's atomic
    /// replace fail with a sharing violation.
    /// </summary>
    private static bool TryReadDockSnapshot(string path, out DockWorkspaceSnapshot? dock)
    {
        dock = null;
        try
        {
            using var stream = new FileStream(
                path,
                FileMode.Open,
                FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete);
            using var reader = new StreamReader(
                stream,
                Encoding.UTF8,
                detectEncodingFromByteOrderMarks: true);
            var parsed = WorkspaceLayoutSerializer.Deserialize(reader.ReadToEnd());
            if (parsed.UsedFallback || parsed.Layout.Dock is not { } candidate)
                return false;

            dock = candidate;
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static bool SnapshotContainsPanel(DockNodeSnapshot? node, string panel)
    {
        if (node is null)
            return false;

        if (node.Panels is { } panels && panels.Contains(panel, StringComparer.Ordinal))
            return true;

        return SnapshotContainsPanel(node.First, panel)
               || SnapshotContainsPanel(node.Second, panel);
    }

    private static async Task<IntPtr> WaitForWindowAsync(
        Process process,
        Func<IntPtr, bool> predicate,
        string description)
    {
        var deadline = DateTime.UtcNow + WindowTimeout;
        while (DateTime.UtcNow < deadline)
        {
            var handle = FindWindow(process.Id, predicate);
            if (handle != IntPtr.Zero)
                return handle;

            if (process.HasExited)
            {
                Assert.Fail(
                    $"The editor process exited with code {process.ExitCode} before showing {description}.");
            }

            await Task.Delay(100);
        }

        Assert.Fail($"The editor process did not show {description} within {WindowTimeout.TotalSeconds:0}s.");
        return IntPtr.Zero;
    }

    private static async Task<bool> WaitForConditionAsync(Func<bool> condition, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (condition())
                return true;

            await Task.Delay(100);
        }

        return false;
    }

    private static async Task<bool> WaitForExitAsync(Process process, TimeSpan timeout)
    {
        using var cancellation = new CancellationTokenSource(timeout);
        try
        {
            await process.WaitForExitAsync(cancellation.Token);
            return true;
        }
        catch (OperationCanceledException)
        {
            return false;
        }
    }

    private static IntPtr FindWindow(int processId, Func<IntPtr, bool> predicate)
    {
        var result = IntPtr.Zero;
        EnumWindows(
            (handle, _) =>
            {
                GetWindowThreadProcessId(handle, out var owner);
                if (owner != processId || !IsWindowVisible(handle) || !predicate(handle))
                    return true;

                result = handle;
                return false;
            },
            IntPtr.Zero);
        return result;
    }

    private static bool IsMainWindow(IntPtr handle)
        => GetWindowText(handle).StartsWith("Nova Editor", StringComparison.Ordinal);

    private static bool IsFloatWindow(IntPtr handle)
        => GetWindowText(handle).StartsWith("Nova —", StringComparison.Ordinal);

    private static string GetWindowText(IntPtr handle)
    {
        var text = new StringBuilder(512);
        GetWindowTextCore(handle, text, text.Capacity);
        return text.ToString();
    }

    private static string ResolveAppPath()
    {
        var testDirectory = new DirectoryInfo(AppContext.BaseDirectory);
        // The editor app itself always targets net9.0 even when this test
        // assembly builds against net9.0-windows for the UIA client.
        var targetFramework = testDirectory.Name.EndsWith("-windows", StringComparison.Ordinal)
            ? testDirectory.Name[..^"-windows".Length]
            : testDirectory.Name;
        var configuration = testDirectory.Parent?.Name ?? "Debug";
        var repoRoot = testDirectory.Parent?.Parent?.Parent?.Parent?.Parent?.FullName;
        Assert.IsNotNull(repoRoot, "The repository root could not be resolved from the test output.");
        return Path.Combine(
            repoRoot!, "src", "Nova.Editor.App", "bin", configuration, targetFramework,
            "Nova.Editor.App.exe");
    }

    private static void TryDeleteDirectory(string directory)
    {
        try
        {
            Directory.Delete(directory, recursive: true);
        }
        catch
        {
            // A leftover temp directory must not hide the actual gate result.
        }
    }

    private sealed record RunEvidence(
        string? FloatTitle,
        bool FloatObserved,
        bool MaximizedFloat);

    private sealed record WindowHandles(
        Process Process,
        IntPtr Main,
        IntPtr Float,
        string? FloatTitle,
        bool MaximizedFloat);

    private delegate bool EnumWindowsProc(IntPtr handle, IntPtr parameter);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr handle, out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "GetWindowTextW")]
    private static extern int GetWindowTextCore(IntPtr handle, StringBuilder text, int count);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(IntPtr handle);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsZoomed(IntPtr handle);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ShowWindow(IntPtr handle, int command);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessage(
        IntPtr handle,
        uint message,
        IntPtr wParam,
        IntPtr lParam);
}
