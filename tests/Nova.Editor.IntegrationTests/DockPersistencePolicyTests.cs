using System.Reflection;
using Nova.Editor.Services;
using Nova.Editor.Services.Layout;
using Nova.Editor.Views.Docking;
using Nova.Editor.Views.ViewModels;
using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;
using Nova.UI.Core.Layout;

namespace Nova.Editor.IntegrationTests;

[TestClass]
public sealed class DockPersistencePolicyTests
{
    private string _directory = null!;
    private string _layoutPath = null!;

    [TestInitialize]
    public void Setup()
    {
        _directory = Path.Combine(Path.GetTempPath(), "nova-d7c-tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_directory);
        _layoutPath = Path.Combine(_directory, "layout.json");
    }

    [TestCleanup]
    public void Cleanup()
    {
        try { Directory.Delete(_directory, recursive: true); }
        catch { /* best-effort */ }
    }

    [TestMethod]
    public async Task Auto_save_debounces_and_writes_only_the_latest_snapshot()
    {
        var delay = new ManualDelay();
        var workspace = EditorWorkspace.CreateSample(_layoutPath, delay.Delay);
        var source = new FakeDockSource();
        workspace.AttachDockPersistence(source);
        workspace.EnableAutoSave();
        var completions = new CompletionProbe(workspace);
        var baseSnapshot = CreateFloatingSnapshot();

        source.Update(1, MoveFloat(baseSnapshot, 120));
        source.RaiseChanged();
        Assert.AreEqual(1, delay.Count);

        source.Update(2, MoveFloat(baseSnapshot, 220));
        source.RaiseChanged();
        Assert.AreEqual(2, delay.Count);

        source.Update(3, MoveFloat(baseSnapshot, 320));
        source.RaiseChanged();
        Assert.AreEqual(3, delay.Count);
        Assert.IsFalse(File.Exists(_layoutPath));

        delay.CompleteAll();
        var result = await completions.Next();

        Assert.AreEqual(LayoutSaveStatus.Saved, result.Status, result.Error);
        Assert.AreEqual(1, completions.Count);
        var parsed = WorkspaceLayoutSerializer.Deserialize(await File.ReadAllTextAsync(_layoutPath));
        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.AreEqual(320, SingleFloatX(parsed.DockWorkspace!));

        source.Update(4, MoveFloat(baseSnapshot, 320));
        source.RaiseChanged();
        Assert.AreEqual(4, delay.Count);
        var before = await File.ReadAllTextAsync(_layoutPath);
        delay.CompleteAll();
        var duplicate = await completions.Next();

        Assert.AreEqual(LayoutSaveStatus.Skipped, duplicate.Status, duplicate.Error);
        StringAssert.Contains(duplicate.Error, "unchanged");
        Assert.AreEqual(before, await File.ReadAllTextAsync(_layoutPath));
    }

    [TestMethod]
    public async Task Explicit_save_cancels_a_pending_auto_save()
    {
        var delay = new ManualDelay();
        var workspace = EditorWorkspace.CreateSample(_layoutPath, delay.Delay);
        var source = new FakeDockSource();
        workspace.AttachDockPersistence(source);
        workspace.EnableAutoSave();
        var completions = new CompletionProbe(workspace);

        source.Update(1, FloatingSnapshotAt(140));
        source.RaiseChanged();
        Assert.AreEqual(1, delay.Count);

        var explicitSave = await workspace.SaveLayoutAsync();
        Assert.AreEqual(LayoutSaveStatus.Saved, explicitSave.Status, explicitSave.Error);
        delay.CompleteAll();
        await Task.Delay(100);

        Assert.AreEqual(1, completions.Count);
        Assert.AreEqual(LayoutSaveStatus.Saved, completions.Results[0].Status);
    }

    [TestMethod]
    public async Task Auto_save_skips_a_dock_workspace_that_is_not_persistable()
    {
        var delay = new ManualDelay();
        var workspace = EditorWorkspace.CreateSample(_layoutPath, delay.Delay);
        var source = new FakeDockSource();
        workspace.AttachDockPersistence(source);
        workspace.EnableAutoSave();
        var completions = new CompletionProbe(workspace);

        source.CanPersist = false;
        source.BlockReason = "dock host is degraded";
        source.Update(1, FloatingSnapshotAt(160));
        source.RaiseChanged();
        Assert.AreEqual(1, delay.Count);

        delay.CompleteAll();
        var blocked = await completions.Next();
        Assert.AreEqual(LayoutSaveStatus.Skipped, blocked.Status);
        StringAssert.Contains(blocked.Error, "degraded");
        Assert.IsFalse(File.Exists(_layoutPath));

        source.CanPersist = true;
        source.Update(2, FloatingSnapshotAt(180));
        source.RaiseChanged();
        Assert.AreEqual(2, delay.Count);
        delay.CompleteAll();
        var recovered = await completions.Next();

        Assert.AreEqual(LayoutSaveStatus.Saved, recovered.Status, recovered.Error);
        var parsed = WorkspaceLayoutSerializer.Deserialize(await File.ReadAllTextAsync(_layoutPath));
        Assert.AreEqual(180, SingleFloatX(parsed.DockWorkspace!));
    }

    [TestMethod]
    public async Task Explicit_panel_close_is_saved_and_restored_without_reintroducing_the_panel()
    {
        var viewModel = new EditorShellViewModel(EditorWorkspace.CreateSample(_layoutPath));
        using (var dock = new EditorDockWorkspace(
                   viewModel,
                   panelFactory: new EditorDockPanelFactory(viewModel)))
        {
            var closed = dock.RemovePanel(DockPanelIds.Console);
            Assert.IsTrue(closed.Success, closed.Diagnostic);
            Assert.IsFalse(dock.Workspace.EnumeratePanels().Contains(DockPanelIds.Console));

            viewModel.Workspace.AttachDockPersistence(dock, dirty: true);
            var saved = await viewModel.Workspace.SaveLayoutAsync();
            Assert.AreEqual(LayoutSaveStatus.Saved, saved.Status, saved.Error);
        }

        var restorer = EditorWorkspace.CreateSample(_layoutPath);
        var loaded = await restorer.InitializeLayoutAsync();

        Assert.IsFalse(loaded.UsedFallback, loaded.Reason);
        Assert.IsNotNull(loaded.DockWorkspace);
        Assert.IsFalse(loaded.DockWorkspace!.EnumeratePanels().Contains(DockPanelIds.Console));
        Assert.AreEqual(0, loaded.DockWorkspace!.FloatingRoots.Count);

        var restorerViewModel = new EditorShellViewModel(restorer);
        using var restored = new EditorDockWorkspace(
            restorerViewModel,
            panelFactory: new EditorDockPanelFactory(restorerViewModel),
            restoreWorkspace: loaded.DockWorkspace);

        Assert.IsNull(restored.RestoreFailure);
        Assert.IsFalse(restored.Workspace.EnumeratePanels().Contains(DockPanelIds.Console));
        Assert.AreEqual(0, restored.Workspace.FloatingRoots.Count);
    }

    [TestMethod]
    public async Task Degraded_dock_hosts_block_saves_until_retry_succeeds()
    {
        var viewModel = new EditorShellViewModel(EditorWorkspace.CreateSample(_layoutPath));
        var interceptor = new FailingInterceptor();
        using var dock = new EditorDockWorkspace(
            viewModel,
            interceptor,
            new EditorDockPanelFactory(viewModel));
        var workspace = viewModel.Workspace;
        workspace.AttachDockPersistence(dock);

        var blocked = await workspace.SaveLayoutAsync();
        Assert.AreEqual(LayoutSaveStatus.Skipped, blocked.Status);
        StringAssert.Contains(blocked.Error, "main");
        Assert.IsFalse(File.Exists(_layoutPath));

        interceptor.Fail = false;
        dock.Retry();
        Assert.IsTrue(dock.Current!.IsHealthy);

        var saved = await workspace.SaveLayoutAsync();
        Assert.AreEqual(LayoutSaveStatus.Saved, saved.Status, saved.Error);
        Assert.IsTrue(File.Exists(_layoutPath));
    }

    [TestMethod]
    public async Task Unknown_panels_load_in_place_and_suppress_automatic_writes_until_an_explicit_save()
    {
        var unknownJson = CreateUnknownPanelJson();
        await File.WriteAllTextAsync(_layoutPath, unknownJson);
        var workspace = EditorWorkspace.CreateSample(_layoutPath);

        var loaded = await workspace.InitializeLayoutAsync();

        Assert.IsFalse(loaded.UsedFallback, loaded.Reason);
        Assert.AreEqual(LayoutFallbackKind.None, loaded.FallbackKind);
        Assert.AreEqual(333, loaded.Layout.HierarchyWidth, 0.001);
        Assert.IsNotNull(loaded.Layout.Dock);
        Assert.IsTrue(loaded.PersistenceSuppressed);
        Assert.IsTrue(workspace.IsPersistenceSuppressed);
        Assert.AreEqual(0, Directory.GetFiles(_directory, "layout.json.corrupt-*").Length);

        var scene = new PanelInstanceId("third.party.scene");
        var chat = new PanelInstanceId("third.party.chat");
        Assert.IsNotNull(loaded.UnresolvedPanels);
        CollectionAssert.AreEquivalent(
            new[] { scene, chat },
            loaded.UnresolvedPanels!.ToArray());
        var restored = loaded.DockWorkspace!;
        CollectionAssert.AreEqual(
            new[]
            {
                DockPanelIds.Hierarchy,
                scene,
                DockPanelIds.Inspector,
                chat,
            },
            restored.EnumeratePanels().ToArray());
        Assert.IsTrue(restored.EnumerateVisiblePanels().Contains(scene));
        Assert.IsTrue(restored.EnumeratePanels().Contains(chat));
        Assert.IsTrue(restored.EnumerateHiddenPanels().Contains(chat));
        Assert.IsTrue(restored.EnumerateVisiblePanels().Contains(DockPanelIds.Inspector));

        var source = new FakeDockSource();
        source.Update(1, DockWorkspaceSnapshotCodec.FromWorkspace(restored));
        workspace.AttachDockPersistence(source);
        var automatic = await workspace.SaveCurrentLayoutAsync(userInitiated: false);

        Assert.AreEqual(LayoutSaveStatus.Skipped, automatic.Status);
        StringAssert.Contains(automatic.Error, "cannot resolve");
        Assert.AreEqual(unknownJson, await File.ReadAllTextAsync(_layoutPath));
        Assert.IsTrue(workspace.IsPersistenceSuppressed);

        // The shutdown snapshot is an automatic write too: it must not replace
        // the unresolved file. A separate workspace proves the gate without
        // ending the first workspace's lifecycle.
        var shutdownWorkspace = EditorWorkspace.CreateSample(_layoutPath);
        await shutdownWorkspace.InitializeLayoutAsync();
        var shutdownSnapshot = shutdownWorkspace.CaptureShutdownSnapshot(out var shutdownReason);
        Assert.IsNull(shutdownSnapshot);
        StringAssert.Contains(shutdownReason, "cannot resolve");
        Assert.AreEqual(unknownJson, await File.ReadAllTextAsync(_layoutPath));

        var explicitSave = await workspace.SaveLayoutAsync();

        Assert.AreEqual(LayoutSaveStatus.Saved, explicitSave.Status, explicitSave.Error);
        Assert.IsFalse(workspace.IsPersistenceSuppressed);
        var rewritten = await File.ReadAllTextAsync(_layoutPath);
        StringAssert.Contains(rewritten, "third.party.scene");
        StringAssert.Contains(rewritten, "third.party.chat");
        StringAssert.Contains(rewritten, "\"hidden\"");

        var parsed = WorkspaceLayoutSerializer.Deserialize(rewritten);
        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.AreEqual(
            WorkspaceLayout.CurrentSchemaVersion,
            parsed.Layout.SchemaVersion);
        Assert.AreEqual(333, parsed.Layout.HierarchyWidth, 0.001);
        Assert.AreEqual(restored.Digest, parsed.DockWorkspace!.Digest);
        CollectionAssert.AreEquivalent(
            new[] { scene, chat },
            parsed.UnresolvedPanels!.ToArray());
        Assert.IsTrue(parsed.DockWorkspace.EnumerateVisiblePanels().Contains(scene));
        Assert.IsTrue(parsed.DockWorkspace.EnumerateHiddenPanels().Contains(chat));
    }

    // LW-12: an unresolved identity is safe while the file on disk already holds
    // it. A write that drops it makes its next appearance a threat again, so the
    // pause re-arms — which is what an undo of a reset layout produces.
    [TestMethod]
    public async Task An_unresolved_panel_that_reappears_pauses_writes_again()
    {
        var unknown = new PanelInstanceId("third.party.scene");
        var other = new PanelInstanceId("third.party.chat");
        var delay = new ManualDelay();
        var workspace = EditorWorkspace.CreateSample(_layoutPath, delay.Delay);
        var source = new FakeDockSource();
        workspace.AttachDockPersistence(source);
        workspace.EnableAutoSave();
        var completions = new CompletionProbe(workspace);

        source.SetUnresolved(unknown);
        source.Update(1, FloatingSnapshotAt(120));
        source.RaiseChanged();

        Assert.IsTrue(workspace.IsPersistenceSuppressed, "an unresolved panel pauses writes");
        StringAssert.Contains(workspace.PersistenceSuppressionReason, "third.party.scene");
        Assert.AreEqual(0, delay.Count, "a paused workspace schedules no autosave");
        Assert.IsFalse(File.Exists(_layoutPath));

        var explicitSave = await workspace.SaveLayoutAsync();

        Assert.AreEqual(LayoutSaveStatus.Saved, explicitSave.Status, explicitSave.Error);
        Assert.IsFalse(workspace.IsPersistenceSuppressed);
        Assert.AreEqual(120, SingleFloatX(await SavedWorkspaceAsync()));

        // The write put the file in agreement with the model, so moving the panel
        // that file already holds is an ordinary change, not a reappearance.
        source.Update(2, FloatingSnapshotAt(320));
        source.RaiseChanged();
        Assert.AreEqual(1, delay.Count);

        delay.CompleteAll();
        await WaitSavedCompletionAsync(completions, 2, "the accepted panel's autosave");

        Assert.IsFalse(workspace.IsPersistenceSuppressed, "an accepted panel does not re-pause");

        // A layout change that drops the identity and saves again leaves the file
        // without it: bringing it back must pause writes before the clean model
        // overwrites the file.
        source.SetUnresolved();
        source.Update(3, DockWorkspaceSnapshotCodec.FromWorkspace(DockWorkspace.CreateDefault()));
        source.RaiseChanged();
        Assert.AreEqual(2, delay.Count);

        delay.CompleteAll();
        await WaitSavedCompletionAsync(completions, 3, "the clean-model autosave");
        var cleanFile = await File.ReadAllTextAsync(_layoutPath);
        Assert.AreEqual(0, (await SavedWorkspaceAsync()).FloatingRoots.Count);

        source.SetUnresolved(unknown);
        source.Update(4, FloatingSnapshotAt(520));
        source.RaiseChanged();

        Assert.IsTrue(workspace.IsPersistenceSuppressed, "a reappearing panel re-pauses writes");
        StringAssert.Contains(workspace.PersistenceSuppressionReason, "third.party.scene");
        Assert.AreEqual(2, delay.Count, "the reappearance schedules nothing");
        Assert.AreEqual(cleanFile, await File.ReadAllTextAsync(_layoutPath));

        var forced = await workspace.SaveLayoutAsync();

        Assert.AreEqual(LayoutSaveStatus.Saved, forced.Status, forced.Error);
        Assert.IsFalse(workspace.IsPersistenceSuppressed);
        Assert.AreEqual(520, SingleFloatX(await SavedWorkspaceAsync()));

        // With the pause re-armed by a further identity, the exit snapshot is
        // refused as well.
        source.SetUnresolved(unknown, other);
        source.RaiseChanged();

        Assert.IsTrue(workspace.IsPersistenceSuppressed, "a new identity re-pauses writes");
        var snapshot = workspace.CaptureShutdownSnapshot(out var reason);

        Assert.IsNull(snapshot);
        StringAssert.Contains(reason, "cannot resolve");
        Assert.AreEqual(520, SingleFloatX(await SavedWorkspaceAsync()));

        async Task<DockWorkspace> SavedWorkspaceAsync()
        {
            var parsed = WorkspaceLayoutSerializer.Deserialize(await File.ReadAllTextAsync(_layoutPath));
            Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
            Assert.IsNotNull(parsed.DockWorkspace);
            return parsed.DockWorkspace!;
        }
    }

    // LM-08: the Reset/pause contract of ADR-009, asserted state transition by
    // state transition. ADR-009 ends the persistence pause only on a write the
    // user asked for, and the Shell's "Reset Layout" command bundles two
    // separate steps: ResetToDefault on the dock facade (model only — it
    // discards hidden and unresolved identities and writes nothing) and the
    // explicit save the command runs after it. Driving them apart here is what
    // keeps the gate honest about which of the two ends the pause:
    //   normal Reset: the ids leave the model and the pause stays because no
    //     write happened; the command's own explicit save then writes a clean v3
    //     and ends the pause. The bundled command through its real button is
    //     DockLayoutHistoryShellTests.LS-06 and real-process RP-5.
    //   Undo Reset: both ids reappear and re-arm the pause before the autosave
    //     scheduled by the change after the save can reach the store; the exit
    //     snapshot is refused too, asserted last because capturing one ends the
    //     layout lifecycle.
    //   explicit Save Layout: faithfully writes the reappearance back and ends
    //     the pause again.
    //   Redo and the undo after it: the pause follows the actual state
    //     transition, not the history cursor or the name of the last operation —
    //     a redo whose ids an accepted write already saw schedules its own
    //     autosave, while a later undo that resurrects ids the clean write
    //     forgot pauses writes again.
    [TestMethod]
    public async Task An_undone_reset_cancels_its_pending_autosave_and_refuses_the_exit_snapshot()
    {
        var unknownJson = CreateUnknownPanelJson();
        await File.WriteAllTextAsync(_layoutPath, unknownJson);
        var unknown = new PanelInstanceId("third.party.scene");
        var other = new PanelInstanceId("third.party.chat");
        var bottomGroup = new DockNodeId("dock.tabs.bottom");
        var delay = new ManualDelay();
        var workspace = EditorWorkspace.CreateSample(_layoutPath, delay.Delay);
        var loaded = await workspace.InitializeLayoutAsync();
        Assert.IsTrue(loaded.PersistenceSuppressed, "the seed loads as an unresolved session");

        var completions = new CompletionProbe(workspace);
        var viewModel = new EditorShellViewModel(workspace);
        using var dock = new EditorDockWorkspace(
            viewModel,
            panelFactory: new EditorDockPanelFactory(viewModel),
            restoreWorkspace: loaded.DockWorkspace);
        workspace.AttachDockPersistence(dock);
        workspace.EnableAutoSave();

        CollectionAssert.AreEquivalent(
            new[] { unknown, other },
            dock.UnresolvedPanels.ToArray());
        Assert.AreEqual(0, delay.Count, "a paused session schedules no autosave");

        var restoredDigest = dock.Workspace.Digest;
        var reset = dock.ResetToDefault();

        Assert.AreEqual(DockApplyStatus.Applied, reset.Status);
        Assert.AreEqual(0, dock.UnresolvedPanels.Count, "the model reset dropped both unknown ids");
        Assert.IsTrue(
            workspace.IsPersistenceSuppressed,
            "a reset that writes nothing is not the write that ends the pause");
        Assert.AreEqual(0, delay.Count, "and still schedules nothing");

        var lifted = await workspace.SaveLayoutAsync();

        Assert.AreEqual(LayoutSaveStatus.Saved, lifted.Status, lifted.Error);
        Assert.IsFalse(
            workspace.IsPersistenceSuppressed,
            "the explicit save step is what ends the pause");
        var cleanFile = await File.ReadAllTextAsync(_layoutPath);
        Assert.IsFalse(cleanFile.Contains("third.party", StringComparison.Ordinal));
        Assert.IsTrue(dock.CanUndoLayout, "the model reset is one undo step");

        // A tab activation advances the revision and schedules an autosave without
        // adding a history step, so the reset stays exactly one undo away.
        Assert.IsTrue(dock.ActivateTab(bottomGroup, DockPanelIds.Console).Success);
        Assert.AreEqual(1, delay.Count, "the change scheduled one autosave");
        var completionsBeforeUndo = completions.Count;

        var undone = dock.UndoLayout();

        Assert.IsTrue(undone.Success, undone.Diagnostic);
        Assert.AreEqual(restoredDigest, dock.Workspace.Digest, "the undone reset restored the seed");
        CollectionAssert.AreEquivalent(
            new[] { unknown, other },
            dock.UnresolvedPanels.ToArray(),
            "and both unknown ids back");
        Assert.IsTrue(
            workspace.IsPersistenceSuppressed,
            "Undo of the reset re-armed the pause before its autosave could fire");
        StringAssert.Contains(workspace.PersistenceSuppressionReason, "third.party.scene");
        Assert.IsTrue(dock.CanRedoLayout, "the undone reset stays redoable");

        delay.CompleteAll();
        await Task.Delay(200);

        Assert.AreEqual(
            completionsBeforeUndo,
            completions.Count,
            "the cancelled autosave never reached the store");
        Assert.AreEqual(
            cleanFile,
            await File.ReadAllTextAsync(_layoutPath),
            "and the file stayed exactly as the compliant reset left it");

        // Explicit Save Layout: the pause yields only to a write the user asked
        // for, and that write is faithful — the very reappearance an autosave
        // must not erase is what it puts back on disk.
        var forced = await workspace.SaveLayoutAsync();

        Assert.AreEqual(LayoutSaveStatus.Saved, forced.Status, forced.Error);
        Assert.IsFalse(workspace.IsPersistenceSuppressed, "the explicit save lifted it again");
        var rewritten = await File.ReadAllTextAsync(_layoutPath);
        StringAssert.Contains(rewritten, "third.party.scene");
        StringAssert.Contains(rewritten, "third.party.chat");

        // Redo: that write recorded both ids as accepted, so redoing the reset
        // is an ordinary change and is free to schedule its own autosave.
        Assert.IsTrue(dock.RedoLayout().Success);

        Assert.AreEqual(0, dock.UnresolvedPanels.Count, "the redo dropped the ids again");
        Assert.IsFalse(
            workspace.IsPersistenceSuppressed,
            "the redo leaves no unresolved identity behind, so there is nothing to pause on");
        Assert.AreEqual(2, delay.Count, "so the compliant redo schedules its own autosave");

        delay.CompleteAll();
        await WaitSavedCompletionAsync(completions, 3, "the compliant redo's autosave");

        Assert.IsFalse(
            (await File.ReadAllTextAsync(_layoutPath))
                .Contains("third.party", StringComparison.Ordinal),
            "and wrote the clean layout it was allowed to write");

        // The clean write forgot both ids, so one more undo is a fresh
        // reappearance: the pause re-arms, schedules nothing, and the exit
        // snapshot is refused too. This is the last step because capturing the
        // shutdown snapshot ends the layout lifecycle.
        Assert.IsTrue(dock.UndoLayout().Success);

        Assert.IsTrue(
            workspace.IsPersistenceSuppressed,
            "a fresh reappearance after a clean write pauses writes again");
        Assert.AreEqual(
            2,
            delay.Count,
            "and the re-paused undo adds no autosave to the queue");

        var snapshot = workspace.CaptureShutdownSnapshot(out var snapshotReason);

        Assert.IsNull(snapshot, "the exit snapshot is refused while the pause is re-armed");
        StringAssert.Contains(snapshotReason, "cannot resolve");
        Assert.AreEqual(
            cleanFile,
            await File.ReadAllTextAsync(_layoutPath),
            "and the refused snapshot left the clean file untouched");
    }

    /// <summary>
    /// Waits for the expected number of save completions and returns the newest
    /// one. Reading the completion queue in FIFO order cannot prove which write
    /// landed, because an explicit save raises a result of its own; the count is
    /// the only handle the test has on "the autosave we just released".
    /// </summary>
    private static async Task<LayoutSaveResult> WaitSavedCompletionAsync(
        CompletionProbe completions,
        int expected,
        string because)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (completions.Count < expected && DateTime.UtcNow < deadline)
            await Task.Delay(20);

        Assert.AreEqual(
            expected,
            completions.Count,
            $"{because} never reported its result (seen={completions.Count}).");
        var results = completions.Results;
        var newest = results[results.Count - 1];
        Assert.AreEqual(LayoutSaveStatus.Saved, newest.Status, newest.Error);
        return newest;
    }

    [TestMethod]
    public async Task Hidden_panels_do_not_suppress_saves_and_survive_explicit_writes()
    {
        var hidden = DockWorkspace.CreateDefault().HidePanel(DockPanelIds.Console);
        Assert.IsTrue(hidden.Success, hidden.Diagnostic);
        var seededLayout = WorkspaceLayout.Default with
        {
            HierarchyWidth = 321,
            Dock = DockWorkspaceSnapshotCodec.FromWorkspace(hidden.Workspace!),
        };
        await File.WriteAllTextAsync(_layoutPath, WorkspaceLayoutSerializer.Serialize(seededLayout));
        var workspace = EditorWorkspace.CreateSample(_layoutPath);

        var loaded = await workspace.InitializeLayoutAsync();

        Assert.IsFalse(loaded.UsedFallback, loaded.Reason);
        Assert.IsFalse(loaded.PersistenceSuppressed);
        Assert.IsFalse(workspace.IsPersistenceSuppressed);
        Assert.IsNull(loaded.UnresolvedPanels);
        Assert.IsTrue(loaded.DockWorkspace!.EnumerateHiddenPanels().Contains(DockPanelIds.Console));

        workspace.ApplyLayout(workspace.Layout with { HierarchyWidth = 322 });
        var automatic = await workspace.SaveCurrentLayoutAsync(userInitiated: false);

        Assert.AreEqual(LayoutSaveStatus.Saved, automatic.Status, automatic.Error);
        var parsed = WorkspaceLayoutSerializer.Deserialize(await File.ReadAllTextAsync(_layoutPath));
        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.AreEqual(322, parsed.Layout.HierarchyWidth, 0.001);
        Assert.AreEqual(
            hidden.Workspace!.Digest,
            parsed.DockWorkspace!.Digest);
        Assert.IsTrue(parsed.DockWorkspace.EnumerateHiddenPanels().Contains(DockPanelIds.Console));
    }

    [TestMethod]
    public async Task Schema_v2_layout_loads_as_v3_with_every_panel_visible()
    {
        await File.WriteAllTextAsync(_layoutPath, """
            {
              "schemaVersion": 2,
              "windowWidth": 1400,
              "hierarchyWidth": 310,
              "dock": {
                "main": {
                  "kind": "tabs",
                  "id": "dock.tabs.a",
                  "panels": ["editor.hierarchy", "editor.console"],
                  "activePanel": "editor.console"
                },
                "floats": []
              }
            }
            """);
        var workspace = EditorWorkspace.CreateSample(_layoutPath);

        var loaded = await workspace.InitializeLayoutAsync();

        Assert.IsFalse(loaded.UsedFallback, loaded.Reason);
        Assert.IsFalse(loaded.PersistenceSuppressed);
        Assert.AreEqual(WorkspaceLayout.CurrentSchemaVersion, loaded.Layout.SchemaVersion);
        Assert.AreEqual(310, loaded.Layout.HierarchyWidth, 0.001);
        Assert.AreEqual(0, loaded.DockWorkspace!.EnumerateHiddenPanels().Count());

        workspace.ApplyLayout(workspace.Layout with { HierarchyWidth = 315 });
        var saved = await workspace.SaveLayoutAsync();

        Assert.AreEqual(LayoutSaveStatus.Saved, saved.Status, saved.Error);
        var rewritten = await File.ReadAllTextAsync(_layoutPath);
        StringAssert.Contains(rewritten, "\"schemaVersion\": 3");
        Assert.IsFalse(rewritten.Contains("\"hidden\"", StringComparison.Ordinal));
        var parsed = WorkspaceLayoutSerializer.Deserialize(rewritten);
        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.AreEqual(0, parsed.DockWorkspace!.EnumerateHiddenPanels().Count());
    }

    [TestMethod]
    public async Task Illegal_hidden_declarations_are_quarantined_at_startup()
    {
        var seeded = WorkspaceLayoutSerializer.Serialize(
            WorkspaceLayout.Default with
            {
                HierarchyWidth = 317,
                Dock = DockWorkspaceSnapshotCodec.FromWorkspace(DockWorkspace.CreateDefault()),
            });
        await File.WriteAllTextAsync(_layoutPath, seeded.Replace(
            "\"activePanel\": \"editor.inspector\"",
            "\"hidden\": [\"editor.inspector\"], \"activePanel\": \"editor.inspector\"",
            StringComparison.Ordinal));
        var workspace = EditorWorkspace.CreateSample(_layoutPath);

        var loaded = await workspace.InitializeLayoutAsync();

        Assert.IsTrue(loaded.UsedFallback);
        Assert.AreEqual(LayoutFallbackKind.InvalidDock, loaded.FallbackKind);
        Assert.AreEqual(WorkspaceLayout.Default, loaded.Layout);
        Assert.AreEqual(DockWorkspace.CreateDefault().Digest, loaded.DockWorkspace!.Digest);
        Assert.IsFalse(File.Exists(_layoutPath));
        Assert.AreEqual(1, Directory.GetFiles(_directory, "layout.json.corrupt-*").Length);
    }

    [TestMethod]
    public async Task Oversized_file_is_quarantined_before_parsing()
    {
        var oversized = new string('x', (int)LayoutPersistenceService.MaxFileBytes + 1);
        await File.WriteAllTextAsync(_layoutPath, oversized);
        var workspace = EditorWorkspace.CreateSample(_layoutPath);

        var loaded = await workspace.InitializeLayoutAsync();

        Assert.IsTrue(loaded.UsedFallback);
        Assert.AreEqual(LayoutFallbackKind.Corrupt, loaded.FallbackKind);
        StringAssert.Contains(loaded.Reason, "1 MiB");
        Assert.IsFalse(File.Exists(_layoutPath));
        Assert.AreEqual(1, Directory.GetFiles(_directory, "layout.json.corrupt-*").Length);
        Assert.IsNotNull(loaded.DockWorkspace);
        Assert.AreEqual(DockWorkspace.CreateDefault().Digest, loaded.DockWorkspace!.Digest);
    }

    [TestMethod]
    public async Task Unreadable_layout_at_startup_is_left_untouched_and_suppresses_automatic_saves()
    {
        var seeded = WorkspaceLayoutSerializer.Serialize(
            WorkspaceLayout.Default with
            {
                HierarchyWidth = 321,
                Dock = FloatingSnapshotAt(180),
            });
        await File.WriteAllTextAsync(_layoutPath, seeded);
        var workspace = EditorWorkspace.CreateSample(_layoutPath);

        using (var locked = new FileStream(
                   _layoutPath, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
        {
            var loaded = await workspace.InitializeLayoutAsync();

            Assert.IsTrue(loaded.UsedFallback);
            Assert.AreEqual(LayoutFallbackKind.ReadFailed, loaded.FallbackKind);
            Assert.IsTrue(loaded.PersistenceSuppressed);
            Assert.AreEqual(WorkspaceLayout.Default, loaded.Layout);
            Assert.AreEqual(DockWorkspace.CreateDefault().Digest, loaded.DockWorkspace!.Digest);
            Assert.IsTrue(workspace.IsPersistenceSuppressed);
            StringAssert.Contains(workspace.PersistenceSuppressionReason, "could not be read");
            Assert.AreEqual(0, Directory.GetFiles(_directory, "layout.json.corrupt-*").Length);

            var source = new FakeDockSource();
            workspace.AttachDockPersistence(source);
            var automatic = await workspace.SaveCurrentLayoutAsync(userInitiated: false);

            Assert.AreEqual(LayoutSaveStatus.Skipped, automatic.Status);
            StringAssert.Contains(automatic.Error, "could not be read");
            Assert.IsTrue(workspace.IsPersistenceSuppressed);
        }

        // The locked file was never parsed or moved; releasing the lock must not
        // make an automatic save replace data the editor never saw.
        Assert.AreEqual(seeded, await File.ReadAllTextAsync(_layoutPath));
        Assert.AreEqual(0, Directory.GetFiles(_directory, "layout.json.corrupt-*").Length);

        var explicitSource = new FakeDockSource();
        workspace.AttachDockPersistence(explicitSource);
        var explicitSave = await workspace.SaveLayoutAsync();

        Assert.AreEqual(LayoutSaveStatus.Saved, explicitSave.Status, explicitSave.Error);
        Assert.IsFalse(workspace.IsPersistenceSuppressed);
        var parsed = WorkspaceLayoutSerializer.Deserialize(await File.ReadAllTextAsync(_layoutPath));
        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.AreEqual(WorkspaceLayout.Default.HierarchyWidth, parsed.Layout.HierarchyWidth, 0.001);
    }

    [TestMethod]
    public async Task Invalid_dock_layout_falls_back_wholly_and_is_quarantined()
    {
        var json = WorkspaceLayoutSerializer.Serialize(
            WorkspaceLayout.Default with
            {
                HierarchyWidth = 311,
                Dock = DockWorkspaceSnapshotCodec.FromWorkspace(DockWorkspace.CreateDefault()),
            });
        await File.WriteAllTextAsync(_layoutPath, json.Replace(
            "\"kind\": \"tabs\"",
            "\"kind\": \"unknown\"",
            StringComparison.Ordinal));
        var workspace = EditorWorkspace.CreateSample(_layoutPath);

        var loaded = await workspace.InitializeLayoutAsync();

        Assert.IsTrue(loaded.UsedFallback);
        Assert.AreEqual(LayoutFallbackKind.InvalidDock, loaded.FallbackKind);
        Assert.AreEqual(WorkspaceLayout.Default, loaded.Layout);
        Assert.AreEqual(DockWorkspace.CreateDefault().Digest, loaded.DockWorkspace!.Digest);
        Assert.AreEqual(1, Directory.GetFiles(_directory, "layout.json.corrupt-*").Length);
    }

    [TestMethod]
    public async Task Serialization_failure_keeps_the_previous_valid_file()
    {
        var store = new LayoutPersistenceService(_layoutPath);
        var valid = LayoutValidator.Normalize(
            WorkspaceLayout.Default with
            {
                Dock = DockWorkspaceSnapshotCodec.FromWorkspace(DockWorkspace.CreateDefault()),
            });
        Assert.AreEqual(LayoutSaveStatus.Saved, (await store.SaveAsync(valid)).Status);
        var before = await File.ReadAllTextAsync(_layoutPath);

        var invalid = valid with { Dock = new DockWorkspaceSnapshot { Main = null } };
        var result = await store.SaveAsync(invalid);

        Assert.AreEqual(LayoutSaveStatus.Failed, result.Status);
        StringAssert.Contains(result.Error, "invalid dock snapshot");
        Assert.AreEqual(before, await File.ReadAllTextAsync(_layoutPath));
        Assert.AreEqual(0, Directory.GetFiles(_directory, "*.tmp").Length);
    }

    [TestMethod]
    public async Task Exit_snapshot_is_captured_before_shutdown_and_written_afterward()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        await workspace.InitializeLayoutAsync();
        var source = new FakeDockSource();
        source.Update(3, FloatingSnapshotAt(240));
        workspace.AttachDockPersistence(source);
        workspace.EnableAutoSave();

        var snapshot = workspace.CaptureShutdownSnapshot(out var reason);

        Assert.IsNull(reason);
        Assert.IsNotNull(snapshot);
        Assert.AreEqual(1, source.CancelCalls);
        Assert.AreEqual(240, SingleFloatX(
            DockWorkspaceSnapshotCodec.ToWorkspace(snapshot!.Dock).Workspace!));

        source.CanPersist = false;
        source.BlockReason = "hosts closed";
        source.Update(4, FloatingSnapshotAt(260));
        source.RaiseChanged();
        Assert.IsTrue((await workspace.SaveLayoutAsync()).Skipped);

        source.CanPersist = true;
        var saved = await workspace.SaveShutdownSnapshotAsync(snapshot);

        Assert.AreEqual(LayoutSaveStatus.Saved, saved.Status, saved.Error);
        var parsed = WorkspaceLayoutSerializer.Deserialize(await File.ReadAllTextAsync(_layoutPath));
        Assert.AreEqual(240, SingleFloatX(parsed.DockWorkspace!));
        Assert.IsFalse(workspace.HasUnsavedDockLayout);
    }

    [TestMethod]
    public async Task Exit_cancels_a_pending_auto_save_and_writes_only_the_shutdown_snapshot()
    {
        var delay = new ManualDelay();
        var workspace = EditorWorkspace.CreateSample(_layoutPath, delay.Delay);
        var source = new FakeDockSource();
        workspace.AttachDockPersistence(source);
        workspace.EnableAutoSave();
        var completions = new CompletionProbe(workspace);

        source.Update(1, FloatingSnapshotAt(240));
        source.RaiseChanged();
        Assert.AreEqual(1, delay.Count);

        var snapshot = workspace.CaptureShutdownSnapshot(out var reason);
        Assert.IsNull(reason);
        Assert.IsNotNull(snapshot);

        source.Update(2, FloatingSnapshotAt(260));
        delay.CompleteAll();
        await Task.Delay(100);

        Assert.AreEqual(0, completions.Count);
        Assert.IsFalse(File.Exists(_layoutPath));

        var saved = await workspace.SaveShutdownSnapshotAsync(snapshot!);

        Assert.AreEqual(LayoutSaveStatus.Saved, saved.Status, saved.Error);
        var parsed = WorkspaceLayoutSerializer.Deserialize(await File.ReadAllTextAsync(_layoutPath));
        Assert.AreEqual(240, SingleFloatX(parsed.DockWorkspace!));
        Assert.IsFalse(workspace.HasUnsavedDockLayout);
    }

    [TestMethod]
    public async Task Exit_final_save_serializes_with_an_in_flight_auto_save_and_wins()
    {
        var delay = new ManualDelay();
        var workspace = EditorWorkspace.CreateSample(_layoutPath, delay.Delay);
        var source = new FakeDockSource();
        workspace.AttachDockPersistence(source);
        workspace.EnableAutoSave();
        var completions = new CompletionProbe(workspace);

        var storeWriteGate = PrivateSemaphore(workspace.LayoutStore, "_writeGate");
        await storeWriteGate.WaitAsync();
        Task<LayoutSaveResult>? finalSave = null;
        WorkspaceLayout? snapshot = null;
        try
        {
            source.Update(1, FloatingSnapshotAt(240));
            source.RaiseChanged();
            delay.CompleteAll();

            var saveGate = PrivateSemaphore(workspace, "_saveGate");
            Assert.IsTrue(
                await WaitUntilAsync(() => saveGate.CurrentCount == 0, TimeSpan.FromSeconds(5)),
                "The automatic save did not reach its write.");

            source.Update(2, FloatingSnapshotAt(260));
            snapshot = workspace.CaptureShutdownSnapshot(out var reason);
            Assert.IsNull(reason);
            Assert.IsNotNull(snapshot);

            finalSave = workspace.SaveShutdownSnapshotAsync(snapshot!);
            Assert.IsFalse(
                finalSave.IsCompleted,
                "The final save must queue behind the in-flight automatic save.");
        }
        finally
        {
            storeWriteGate.Release();
        }

        var automatic = await completions.Next();
        Assert.AreEqual(LayoutSaveStatus.Saved, automatic.Status, automatic.Error);

        var final = await finalSave!;
        Assert.AreEqual(LayoutSaveStatus.Saved, final.Status, final.Error);
        var parsed = WorkspaceLayoutSerializer.Deserialize(await File.ReadAllTextAsync(_layoutPath));
        Assert.AreEqual(
            260,
            SingleFloatX(parsed.DockWorkspace!),
            "The shutdown snapshot must be written after, and win over, the in-flight snapshot.");
    }

    [TestMethod]
    public async Task Final_save_failure_is_reported_without_overwriting_the_previous_file()
    {
        var workspace = EditorWorkspace.CreateSample(_layoutPath);
        await workspace.InitializeLayoutAsync();
        workspace.ApplyLayout(workspace.Layout with { HierarchyWidth = 280 });
        Assert.AreEqual(LayoutSaveStatus.Saved, (await workspace.SaveLayoutAsync()).Status);
        var before = await File.ReadAllTextAsync(_layoutPath);

        var snapshot = workspace.CaptureShutdownSnapshot(out _);
        Assert.IsNotNull(snapshot);
        using (var locked = new FileStream(
                   _layoutPath, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
        {
            var result = await workspace.SaveShutdownSnapshotAsync(snapshot!);
            Assert.AreEqual(LayoutSaveStatus.Failed, result.Status);
            Assert.IsFalse(string.IsNullOrWhiteSpace(result.Error));
        }

        Assert.AreEqual(before, await File.ReadAllTextAsync(_layoutPath));
        Assert.IsTrue(workspace.Log.Snapshot().Any(
            entry => entry.Message.Contains("Final layout save failed")));
        Assert.AreEqual(0, Directory.GetFiles(_directory, "*.tmp").Length);
    }

    [TestMethod]
    public async Task Load_stage_float_prepare_failure_is_merged_and_saved_as_part_of_the_candidate()
    {
        var viewModel = new EditorShellViewModel(EditorWorkspace.CreateSample(_layoutPath));
        var floated = DockWorkspace.CreateDefault().FloatPanel(
            DockPanelIds.Console,
            new DockBounds(140, 120, 520, 340));
        Assert.IsTrue(floated.Success, floated.Diagnostic);
        var candidate = floated.Workspace!;
        var revision = candidate.Revision;

        using var dock = new EditorDockWorkspace(
            viewModel,
            panelFactory: new EditorDockPanelFactory(viewModel),
            floatHostFactory: _ => throw new InvalidOperationException("injected float host failure"),
            restoreWorkspace: candidate);

        Assert.IsNull(dock.RestoreFailure);
        CollectionAssert.AreEqual(
            new[] { candidate.FloatingRoots[0].Id },
            dock.RestoreMergedFloatRoots.ToArray());
        Assert.AreEqual(0, dock.Workspace.FloatingRoots.Count);
        Assert.AreEqual(revision, dock.Workspace.Revision);

        var workspace = viewModel.Workspace;
        workspace.AttachDockPersistence(dock, dirty: true);
        var saved = await workspace.SaveLayoutAsync();

        Assert.AreEqual(LayoutSaveStatus.Saved, saved.Status, saved.Error);
        var parsed = WorkspaceLayoutSerializer.Deserialize(await File.ReadAllTextAsync(_layoutPath));
        Assert.AreEqual(0, parsed.DockWorkspace!.FloatingRoots.Count);
    }

    [TestMethod]
    public async Task Apply_failure_after_restore_degrades_the_host_without_merging_the_float()
    {
        var floated = DockWorkspace.CreateDefault().FloatPanel(
            DockPanelIds.Console,
            new DockBounds(150, 130, 540, 360));
        Assert.IsTrue(floated.Success, floated.Diagnostic);
        var candidate = floated.Workspace!;
        var floatId = candidate.FloatingRoots[0].Id;
        var registry = new DockPanelViewRegistry(new StubPanelFactory());
        var floatHost = new RestoreFakeHost(DockRootHostDescriptor.ForFloat(floatId))
        {
            ApplyResult = DockHostResult.Fail(
                DockHostError.ProjectionFailed,
                "injected apply failure"),
        };

        var result = DockWorkspaceCoordinator.Restore(
            candidate,
            new RestoreFakeHost(DockRootHostDescriptor.ForMain()),
            registry,
            _ => floatHost);

        Assert.IsTrue(result.Success, result.Failure);
        try
        {
            Assert.AreEqual(0, result.MergedFloatRoots.Count);
            Assert.AreEqual(1, result.Coordinator!.Workspace.FloatingRoots.Count);
            Assert.AreEqual(candidate.Revision, result.Coordinator.Workspace.Revision);
            Assert.IsFalse(floatHost.IsHealthy);
            Assert.IsTrue(result.Diagnostics.Any(text => text.Contains("could not apply")));
        }
        finally
        {
            result.Coordinator!.Dispose();
        }

        await Task.CompletedTask;
    }

    private static string CreateUnknownPanelJson()
    {
        var hidden = DockWorkspace.CreateDefault().HidePanel(DockPanelIds.Console);
        Assert.IsTrue(hidden.Success, hidden.Diagnostic);
        var json = WorkspaceLayoutSerializer.Serialize(
            WorkspaceLayout.Default with
            {
                HierarchyWidth = 333,
                Dock = DockWorkspaceSnapshotCodec.FromWorkspace(hidden.Workspace!),
            });
        return json
            .Replace("\"editor.scene\"", "\"third.party.scene\"", StringComparison.Ordinal)
            .Replace("\"editor.console\"", "\"third.party.chat\"", StringComparison.Ordinal);
    }

    private static DockWorkspaceSnapshot CreateFloatingSnapshot()
    {
        var floated = DockWorkspace.CreateDefault().FloatPanel(
            DockPanelIds.Console,
            new DockBounds(120, 140, 480, 320));
        Assert.IsTrue(floated.Success, floated.Diagnostic);
        return DockWorkspaceSnapshotCodec.FromWorkspace(floated.Workspace!);
    }

    private static DockWorkspaceSnapshot MoveFloat(DockWorkspaceSnapshot snapshot, double x)
        => snapshot with
        {
            Floats = snapshot.Floats
                .Select(root => root with { Bounds = root.Bounds with { X = x } })
                .ToArray(),
        };

    private static DockWorkspaceSnapshot FloatingSnapshotAt(double x)
        => MoveFloat(CreateFloatingSnapshot(), x);

    private static double SingleFloatX(DockWorkspace workspace)
    {
        Assert.AreEqual(1, workspace.FloatingRoots.Count);
        return workspace.FloatingRoots[0].RestoreBounds.X;
    }

    private static async Task<bool> WaitUntilAsync(Func<bool> condition, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (condition())
                return true;

            await Task.Delay(20);
        }

        return false;
    }

    /// <summary>
    /// The production save path has no test seam; holding its real write gate is
    /// the only way to prove the shutdown ordering with a genuinely in-flight write.
    /// </summary>
    private static SemaphoreSlim PrivateSemaphore(object target, string fieldName)
    {
        var field = target.GetType().GetField(
            fieldName,
            BindingFlags.Instance | BindingFlags.NonPublic);
        Assert.IsNotNull(field, $"'{fieldName}' was not found on {target.GetType().Name}.");
        return (SemaphoreSlim)field!.GetValue(target)!;
    }

    private sealed class CompletionProbe
    {
        private readonly object _gate = new();
        private readonly List<LayoutSaveResult> _results = [];
        private readonly Queue<LayoutSaveResult> _pending = new();
        private readonly SemaphoreSlim _signal = new(0);

        public CompletionProbe(EditorWorkspace workspace)
        {
            workspace.LayoutSaveCompleted += (_, e) =>
            {
                if (e.Shutdown)
                    return;

                lock (_gate)
                {
                    _results.Add(e.Result);
                    _pending.Enqueue(e.Result);
                }

                _signal.Release();
            };
        }

        public int Count
        {
            get
            {
                lock (_gate)
                    return _results.Count;
            }
        }

        public IReadOnlyList<LayoutSaveResult> Results
        {
            get
            {
                lock (_gate)
                    return _results.ToArray();
            }
        }

        public async Task<LayoutSaveResult> Next()
        {
            if (!await _signal.WaitAsync(TimeSpan.FromSeconds(10)))
                throw new TimeoutException("No layout save completion was observed.");

            lock (_gate)
                return _pending.Dequeue();
        }
    }

    private sealed class ManualDelay
    {
        private readonly object _gate = new();
        private readonly List<Request> _requests = [];

        public int Count
        {
            get
            {
                lock (_gate)
                    return _requests.Count;
            }
        }

        public Task Delay(TimeSpan _, CancellationToken cancellationToken)
        {
            var completion = new TaskCompletionSource(
                TaskCreationOptions.RunContinuationsAsynchronously);
            lock (_gate)
                _requests.Add(new Request(completion));

            cancellationToken.Register(() => completion.TrySetCanceled(cancellationToken));
            return completion.Task;
        }

        public void CompleteAll()
        {
            Request[] requests;
            lock (_gate)
                requests = _requests.ToArray();

            foreach (var request in requests)
                request.Completion.TrySetResult();
        }

        private sealed record Request(TaskCompletionSource Completion);
    }

    private sealed class FakeDockSource : IDockPersistenceSource, IDockUnresolvedPanelSource
    {
        private readonly List<PanelInstanceId> _unresolved = [];

        public long Revision { get; private set; }

        public bool CanPersist { get; set; } = true;

        public string? BlockReason { get; set; }

        public int CancelCalls { get; private set; }

        public DockWorkspaceSnapshot Snapshot { get; private set; } =
            DockWorkspaceSnapshotCodec.FromWorkspace(DockWorkspace.CreateDefault());

        public IReadOnlyList<PanelInstanceId> UnresolvedPanels => _unresolved;

        public event EventHandler? PersistenceStateChanged;

        public void SetUnresolved(params PanelInstanceId[] panels)
        {
            _unresolved.Clear();
            _unresolved.AddRange(panels);
        }

        public void Update(long revision, DockWorkspaceSnapshot snapshot)
        {
            Revision = revision;
            Snapshot = snapshot;
        }

        public void RaiseChanged()
            => PersistenceStateChanged?.Invoke(this, EventArgs.Empty);

        public DockPersistenceCapture Capture()
            => CanPersist
                ? DockPersistenceCapture.Ready(Revision, Snapshot)
                : DockPersistenceCapture.Blocked(BlockReason ?? "blocked", Revision);

        public void CancelPendingInteraction() => CancelCalls++;
    }

    private sealed class FailingInterceptor : IDockNodeApplyInterceptor
    {
        public bool Fail { get; set; } = true;

        public void OnApplying(DockNodeId node, DockApplyToken token)
        {
            if (Fail)
                throw new InvalidOperationException($"injected visual failure at '{node}'.");
        }
    }

    private sealed class StubPanelFactory : IDockPanelViewFactory
    {
        public bool CanCreate(PanelInstanceId instance) => true;

        public IDockPanelView Create(PanelInstanceId instance) => new StubPanelView(instance);
    }

    private sealed class StubPanelView(PanelInstanceId instance) : IDockPanelView
    {
        public string Title => instance.ToString();

        public Avalonia.Controls.Control Content { get; } = new Avalonia.Controls.Border();

        public void Dispose()
        {
        }
    }

    private sealed class RestoreFakeHost(DockRootHostDescriptor descriptor) : IDockRootHost
    {
        private long _generation;
        private string? _digest;
        private bool _projectionHealthy;

        public DockRootHostId Id { get; } = descriptor.Id;

        public DockRootKind Kind { get; } = descriptor.Kind;

        public bool IsVisible { get; private set; }

        public bool IsHealthy => _projectionHealthy && IsVisible;

        public DockApplyResult? Current { get; private set; }

        public DockHostResult? ApplyResult { get; set; }

        public DockHostResult Prepare(DockTree root) => DockHostResult.Ok();

        public DockHostResult Show()
        {
            IsVisible = true;
            return DockHostResult.Ok();
        }

        public DockHostResult Apply(DockTree root)
        {
            if (ApplyResult is { } failure)
            {
                _projectionHealthy = false;
                Current = null;
                return failure;
            }

            _generation++;
            _digest = root.Digest;
            _projectionHealthy = true;
            Current = new DockApplyResult(
                DockApplyStatus.Applied,
                _generation,
                _digest,
                _digest,
                Array.Empty<PanelInstanceId>(),
                Diagnostic: null);
            return DockHostResult.Ok();
        }

        public DockHostResult Retry() => DockHostResult.Ok();

        public DockHostResult Close()
        {
            IsVisible = false;
            return DockHostResult.Ok();
        }

        public bool TryGetCurrentToken(out DockApplyToken token)
        {
            if (_projectionHealthy && _digest is not null)
            {
                token = new DockApplyToken(Id, _generation, _digest);
                return true;
            }

            token = default;
            return false;
        }

        public Avalonia.Controls.Control? FindNode(DockNodeId id) => null;

        public IDockPanelView? FindPanel(PanelInstanceId instance) => null;

        public void Dispose()
        {
            IsVisible = false;
            _projectionHealthy = false;
        }
    }
}
