using Nova.UI.Core.Docking;
using Nova.UI.Core.Layout;

namespace Nova.UI.Core.Tests;

[TestClass]
public sealed class DockWorkspaceSnapshotTests
{
    private static readonly DockBounds FirstBounds = new(120d, 90d, 480d, 320d);
    private static readonly DockBounds SecondBounds = new(640d, 420d, 620d, 380d);
    private static readonly DockBounds MaximizedBounds = new(-8d, -8d, 1936d, 1056d);

    // D7-B: the complete multi-root workspace round-trips byte-for-byte,
    // preserves identities/order/active tabs/return targets and keeps the W2
    // digest independent of Revision.
    [TestMethod]
    public void D7b_round_trip_is_byte_deterministic_and_preserves_multi_root_state()
    {
        var workspace = CreateComplexWorkspace();
        Assert.AreEqual(4L, workspace.Revision);

        var layout = LayoutWithDock(workspace);
        var json = WorkspaceLayoutSerializer.Serialize(layout);
        Assert.AreEqual(json, WorkspaceLayoutSerializer.Serialize(layout));

        var parsed = WorkspaceLayoutSerializer.Deserialize(json);
        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.AreEqual(LayoutFallbackKind.None, parsed.FallbackKind);
        Assert.IsNotNull(parsed.DockWorkspace);
        Assert.AreEqual(1440d, parsed.Layout.WindowWidth, 0.001);
        Assert.IsFalse(parsed.Layout.ShowInspector);
        Assert.AreEqual("nova.light", parsed.Layout.ThemeId);
        Assert.IsNotNull(parsed.Layout.Dock);

        var restored = parsed.DockWorkspace!;
        Assert.AreEqual(0L, restored.Revision);
        Assert.AreEqual(workspace.Digest, restored.Digest);
        Assert.AreEqual(workspace.MainRoot.Digest, restored.MainRoot.Digest);
        Assert.AreEqual(2, restored.FloatingRoots.Count);
        Assert.AreEqual("dock.float.1", restored.FloatingRoots[0].Id.ToString());
        Assert.AreEqual("dock.float.2", restored.FloatingRoots[1].Id.ToString());

        var first = restored.FloatingRoots[0];
        Assert.IsTrue(first.Maximized);
        Assert.AreEqual(FirstBounds, first.Bounds);
        Assert.AreEqual(FirstBounds, first.RestoreBounds);
        Assert.AreEqual(
            new DockReturnTarget(new DockNodeId("dock.tabs.bottom"), 1),
            first.ReturnTarget);
        AssertPanels(
            new[] { DockPanelIds.Console, DockPanelIds.Scene },
            first.Root.EnumeratePanels());
        Assert.IsInstanceOfType(first.Root.Root, typeof(DockSplitNode));
        var firstGroup = ((DockSplitNode)first.Root.Root).First;
        Assert.IsInstanceOfType(firstGroup, typeof(DockTabGroupNode));
        Assert.AreEqual(DockPanelIds.Console, ((DockTabGroupNode)firstGroup!).ActivePanel);

        var second = restored.FloatingRoots[1];
        Assert.IsFalse(second.Maximized);
        Assert.AreEqual(SecondBounds, second.Bounds);
        Assert.AreEqual(SecondBounds, second.RestoreBounds);
        AssertPanels(new[] { DockPanelIds.Inspector }, second.Root.EnumeratePanels());
        AssertPanels(new[] { DockPanelIds.Hierarchy }, restored.MainRoot.EnumeratePanels());

        Assert.AreEqual(json, WorkspaceLayoutSerializer.Serialize(parsed.Layout));
    }

    // D7-B: canonical serialization orders floats by id and ignores transient
    // maximized geometry.
    [TestMethod]
    public void D7b_snapshot_float_order_is_canonical()
    {
        var workspace = CreateComplexWorkspace();
        var layout = LayoutWithDock(workspace);
        var snapshot = layout.Dock!;
        var reversed = snapshot with { Floats = snapshot.Floats.Reverse().ToArray() };

        var canonicalJson = WorkspaceLayoutSerializer.Serialize(layout);
        var reversedJson = WorkspaceLayoutSerializer.Serialize(layout with { Dock = reversed });
        Assert.AreEqual(canonicalJson, reversedJson);

        var result = DockWorkspaceSnapshotCodec.ToWorkspace(reversed);
        Assert.IsTrue(result.IsValid, result.Diagnostic);
        CollectionAssert.AreEqual(
            new[] { "dock.float.1", "dock.float.2" },
            result.Workspace!.FloatingRoots.Select(root => root.Id.ToString()).ToArray());
    }

    // D7-B/D8-C: a v1 file migrates to the current schema with shell fields
    // preserved and a default dock, without being treated as a fallback.
    [TestMethod]
    public void D7b_schema_v1_migrates_shell_fields_and_uses_default_dock()
    {
        const string json = """
            {
              "schemaVersion": 1,
              "windowWidth": 1500,
              "windowHeight": 950,
              "hierarchyWidth": 320,
              "inspectorWidth": 380,
              "consoleHeight": 210,
              "showHierarchy": true,
              "showInspector": false,
              "showConsole": true,
              "themeId": "nova.light"
            }
            """;

        var parsed = WorkspaceLayoutSerializer.Deserialize(json);

        Assert.IsFalse(parsed.UsedFallback);
        Assert.AreEqual(LayoutFallbackKind.None, parsed.FallbackKind);
        Assert.IsFalse(parsed.ShouldQuarantine);
        Assert.IsNull(parsed.Layout.Dock);
        Assert.AreEqual(WorkspaceLayout.CurrentSchemaVersion, parsed.Layout.SchemaVersion);
        Assert.AreEqual(1500d, parsed.Layout.WindowWidth, 0.001);
        Assert.AreEqual(320d, parsed.Layout.HierarchyWidth, 0.001);
        Assert.IsFalse(parsed.Layout.ShowInspector);
        Assert.AreEqual("nova.light", parsed.Layout.ThemeId);
        Assert.IsNotNull(parsed.DockWorkspace);
        Assert.AreEqual(DockWorkspace.CreateDefault().Digest, parsed.DockWorkspace!.Digest);
    }

    // D8-C: a v2 file without a dock section remains valid.
    [TestMethod]
    public void D7b_schema_v2_without_dock_uses_default_workspace()
    {
        const string json = """
            {
              "schemaVersion": 2,
              "windowWidth": 1500,
              "themeId": "nova.light"
            }
            """;

        var parsed = WorkspaceLayoutSerializer.Deserialize(json);

        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.AreEqual(LayoutFallbackKind.None, parsed.FallbackKind);
        Assert.IsNull(parsed.Layout.Dock);
        Assert.AreEqual(WorkspaceLayout.CurrentSchemaVersion, parsed.Layout.SchemaVersion);
        Assert.AreEqual(1500d, parsed.Layout.WindowWidth, 0.001);
        Assert.AreEqual("nova.light", parsed.Layout.ThemeId);
        Assert.IsNotNull(parsed.DockWorkspace);
        Assert.AreEqual(DockWorkspace.CreateDefault().Digest, parsed.DockWorkspace!.Digest);
    }

    // D8-C: a v2 dock snapshot migrates to schema v3 with every panel visible.
    [TestMethod]
    public void D8c_schema_v2_dock_snapshot_migrates_to_all_visible()
    {
        const string json = """
            {
              "schemaVersion": 2,
              "windowWidth": 1400,
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
            """;

        var parsed = WorkspaceLayoutSerializer.Deserialize(json);

        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.AreEqual(LayoutFallbackKind.None, parsed.FallbackKind);
        Assert.IsFalse(parsed.ShouldQuarantine);
        Assert.AreEqual(WorkspaceLayout.CurrentSchemaVersion, parsed.Layout.SchemaVersion);
        Assert.IsNotNull(parsed.DockWorkspace);
        Assert.AreEqual(0, parsed.DockWorkspace!.EnumerateHiddenPanels().Count());

        var group = (DockTabGroupNode)parsed.DockWorkspace.MainRoot.Root;
        AssertPanels(
            new[] { DockPanelIds.Hierarchy, DockPanelIds.Console },
            group.Panels);
        Assert.AreEqual(DockPanelIds.Console, group.ActivePanel);
        Assert.AreEqual(0, group.HiddenPanels.Count);

        Assert.IsFalse(
            WorkspaceLayoutSerializer.Serialize(parsed.Layout).Contains("\"hidden\"", StringComparison.Ordinal));
    }

    // D8-C: hidden declarations are a v3 concept; a v2 file carrying them is
    // rejected instead of being silently reinterpreted.
    [TestMethod]
    public void D8c_schema_v2_hidden_declaration_is_rejected_and_quarantined()
    {
        var json = LayoutJson(TabsJson(
            "dock.tabs.a",
            "editor.hierarchy",
            hidden: """["editor.hierarchy"]"""))
            .Replace("\"schemaVersion\": 3", "\"schemaVersion\": 2", StringComparison.Ordinal);

        var parsed = WorkspaceLayoutSerializer.Deserialize(json);

        Assert.IsTrue(parsed.UsedFallback);
        Assert.AreEqual(LayoutFallbackKind.InvalidDock, parsed.FallbackKind);
        Assert.IsTrue(parsed.ShouldQuarantine);
        Assert.AreEqual(WorkspaceLayout.Default, parsed.Layout);
        StringAssert.Contains(parsed.Reason, "hidden");
    }

    // D8-C: unknown panel identities load in place instead of falling back the
    // whole dock; the shell and every known panel are preserved and the file is
    // not quarantined.
    [TestMethod]
    public void D8c_unknown_panel_loads_in_place_without_fallback()
    {
        var json = WorkspaceLayoutSerializer.Serialize(WorkspaceLayout.Default with
        {
            WindowWidth = 1500,
            WindowHeight = 950,
            ShowInspector = false,
            ThemeId = "nova.light",
            Dock = DockWorkspaceSnapshotCodec.FromWorkspace(DockWorkspace.CreateDefault()),
        });
        json = json.Replace(
            "\"editor.hierarchy\"",
            "\"editor.future.hierarchy\"",
            StringComparison.Ordinal);

        var parsed = WorkspaceLayoutSerializer.Deserialize(json);

        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.AreEqual(LayoutFallbackKind.None, parsed.FallbackKind);
        Assert.IsFalse(parsed.ShouldQuarantine);
        Assert.IsNotNull(parsed.Layout.Dock);
        Assert.AreEqual(1500d, parsed.Layout.WindowWidth, 0.001);
        Assert.AreEqual(950d, parsed.Layout.WindowHeight, 0.001);
        Assert.IsFalse(parsed.Layout.ShowInspector);
        Assert.AreEqual("nova.light", parsed.Layout.ThemeId);
        Assert.IsNotNull(parsed.DockWorkspace);
        AssertPanels(
            new[]
            {
                new PanelInstanceId("editor.future.hierarchy"),
                DockPanelIds.Scene,
                DockPanelIds.Inspector,
                DockPanelIds.Console,
            },
            parsed.DockWorkspace!.EnumeratePanels());
        Assert.IsNotNull(parsed.UnresolvedPanels);
        CollectionAssert.AreEqual(
            new[] { new PanelInstanceId("editor.future.hierarchy") },
            parsed.UnresolvedPanels!.ToArray());
        Assert.IsNotNull(parsed.Reason);
        StringAssert.Contains(parsed.Reason, "editor.future.hierarchy");

        // Faithful write-back: the unknown identity is neither replaced nor
        // dropped on the next serialization.
        var rewritten = WorkspaceLayoutSerializer.Serialize(parsed.Layout);
        StringAssert.Contains(rewritten, "editor.future.hierarchy");
        Assert.IsFalse(
            WorkspaceLayoutSerializer.Deserialize(rewritten).UsedFallback);
    }

    // D8-C: hidden membership is written as an ordered subset of panels,
    // survives a round trip and keeps the workspace digest stable.
    [TestMethod]
    public void D8c_hidden_panels_round_trip_and_preserve_digest()
    {
        var workspace = RequireSuccess(
            DockWorkspace.CreateDefault().HidePanel(DockPanelIds.Console));
        var layout = WorkspaceLayout.Default with
        {
            Dock = DockWorkspaceSnapshotCodec.FromWorkspace(workspace),
        };

        var json = WorkspaceLayoutSerializer.Serialize(layout);
        StringAssert.Contains(json, "\"hidden\"");
        Assert.AreEqual(json, WorkspaceLayoutSerializer.Serialize(layout));

        var parsed = WorkspaceLayoutSerializer.Deserialize(json);

        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.IsNull(parsed.UnresolvedPanels);
        Assert.IsNotNull(parsed.DockWorkspace);
        Assert.AreEqual(workspace.Digest, parsed.DockWorkspace!.Digest);
        Assert.AreEqual(workspace.Digest, parsed.Layout.Dock is { } dock
            ? DockWorkspaceSnapshotCodec.ToWorkspace(dock).Workspace!.Digest
            : null);
        AssertPanels(
            new[] { DockPanelIds.Console },
            parsed.DockWorkspace.EnumerateHiddenPanels());
        AssertPanels(
            new[] { DockPanelIds.Hierarchy, DockPanelIds.Scene, DockPanelIds.Inspector },
            parsed.DockWorkspace.EnumerateVisiblePanels());

        var bottom = (DockTabGroupNode)parsed.DockWorkspace.MainRoot
            .FindGroup(new DockNodeId("dock.tabs.bottom"))!;
        AssertPanels(
            new[] { DockPanelIds.Inspector, DockPanelIds.Console },
            bottom.Panels);
        AssertPanels(new[] { DockPanelIds.Console }, bottom.HiddenPanels);
        Assert.AreEqual(DockPanelIds.Inspector, bottom.ActivePanel);
        Assert.AreEqual(json, WorkspaceLayoutSerializer.Serialize(parsed.Layout));
    }

    // D8-C: hidden entries are canonicalized to Panels order without dropping
    // any declared identity.
    [TestMethod]
    public void D8c_hidden_order_is_canonical_without_losing_identity()
    {
        var alpha = new PanelInstanceId("editor.alpha");
        var beta = new PanelInstanceId("editor.beta");
        var gamma = new PanelInstanceId("editor.gamma");
        var tree = DockTree.Create(new DockTabGroupNode(
            new DockNodeId("dock.tabs.a"),
            new[] { alpha, beta, gamma },
            beta,
            new[] { gamma, alpha }));
        var workspace = DockWorkspace.Create(tree);
        var layout = WorkspaceLayout.Default with
        {
            Dock = DockWorkspaceSnapshotCodec.FromWorkspace(workspace),
        };

        var json = WorkspaceLayoutSerializer.Serialize(layout);
        var parsed = WorkspaceLayoutSerializer.Deserialize(json);

        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.IsNotNull(parsed.DockWorkspace);
        Assert.AreEqual(workspace.Digest, parsed.DockWorkspace!.Digest);
        var group = (DockTabGroupNode)parsed.DockWorkspace.MainRoot.Root;
        AssertPanels(new[] { alpha, gamma }, group.HiddenPanels);
        AssertPanels(new[] { alpha, beta, gamma }, group.Panels);
        Assert.AreEqual(json, WorkspaceLayoutSerializer.Serialize(parsed.Layout));
    }

    // D8-C: unknown and hidden identities survive together; neither the opaque
    // panel nor the hidden state is rewritten to a built-in or removed.
    [TestMethod]
    public void D8c_unknown_hidden_panel_round_trip()
    {
        var future = new PanelInstanceId("editor.future.panel");
        var tree = DockTree.Create(new DockTabGroupNode(
            new DockNodeId("dock.tabs.a"),
            new[] { DockPanelIds.Scene, future },
            DockPanelIds.Scene,
            new[] { future }));
        var workspace = DockWorkspace.Create(tree);
        var layout = WorkspaceLayout.Default with
        {
            Dock = DockWorkspaceSnapshotCodec.FromWorkspace(workspace),
        };

        var json = WorkspaceLayoutSerializer.Serialize(layout);
        StringAssert.Contains(json, "editor.future.panel");

        var parsed = WorkspaceLayoutSerializer.Deserialize(json);

        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.AreEqual(LayoutFallbackKind.None, parsed.FallbackKind);
        Assert.IsFalse(parsed.ShouldQuarantine);
        CollectionAssert.AreEqual(
            new[] { future },
            parsed.UnresolvedPanels!.ToArray());
        Assert.AreEqual(workspace.Digest, parsed.DockWorkspace!.Digest);
        AssertPanels(new[] { future }, parsed.DockWorkspace.EnumerateHiddenPanels());
        var group = (DockTabGroupNode)parsed.DockWorkspace.MainRoot.Root;
        AssertPanels(new[] { DockPanelIds.Scene, future }, group.Panels);
        Assert.AreEqual(DockPanelIds.Scene, group.ActivePanel);
        Assert.AreEqual(json, WorkspaceLayoutSerializer.Serialize(parsed.Layout));
    }

    // D8-C: hidden state inside a floating root round-trips with the same
    // identity, order and W3 digest.
    [TestMethod]
    public void D8c_hidden_panel_in_float_root_round_trip()
    {
        var floated = RequireSuccess(
            DockWorkspace.CreateDefault().FloatPanel(DockPanelIds.Console, FirstBounds));
        var floatGroupId = RequireGroup(floated.FloatingRoots[0].Root.Root).Id;
        var grouped = RequireSuccess(floated.MovePanel(
            DockPanelIds.Inspector,
            new DockTarget.Center(floatGroupId)));
        var workspace = RequireSuccess(grouped.HidePanel(DockPanelIds.Console));
        Assert.AreEqual(1, workspace.FloatingRoots.Count);

        var layout = LayoutWithDock(workspace);
        var json = WorkspaceLayoutSerializer.Serialize(layout);
        var parsed = WorkspaceLayoutSerializer.Deserialize(json);

        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.IsNull(parsed.UnresolvedPanels);
        Assert.AreEqual(workspace.Digest, parsed.DockWorkspace!.Digest);
        Assert.AreEqual(1, parsed.DockWorkspace.FloatingRoots.Count);

        var floatRoot = parsed.DockWorkspace.FloatingRoots[0];
        var group = (DockTabGroupNode)floatRoot.Root.Root;
        AssertPanels(
            new[] { DockPanelIds.Console, DockPanelIds.Inspector },
            group.Panels);
        AssertPanels(new[] { DockPanelIds.Console }, group.HiddenPanels);
        Assert.AreEqual(DockPanelIds.Inspector, group.ActivePanel);
        AssertPanels(
            new[] { DockPanelIds.Hierarchy, DockPanelIds.Scene },
            parsed.DockWorkspace.MainRoot.EnumeratePanels());

        Assert.AreEqual(
            workspace.FloatingRoots[0].ReturnTarget,
            floatRoot.ReturnTarget);
        Assert.AreEqual(json, WorkspaceLayoutSerializer.Serialize(parsed.Layout));
    }

    // D8-C: illegal hidden declarations reject the whole layout; the codec
    // never repairs a hidden active panel or an all-hidden group.
    [TestMethod]
    public void D8c_illegal_hidden_declarations_fall_back_whole_layout()
    {
        var twoPanels = """["editor.hierarchy","editor.console"]""";

        // Hidden entry is not a member of the group.
        AssertInvalidDock(LayoutJson(
            """{"kind":"tabs","id":"dock.tabs.a","panels":["editor.hierarchy"],"activePanel":"editor.hierarchy","hidden":["editor.console"]}"""));

        // Duplicate hidden declaration.
        AssertInvalidDock(LayoutJson(
            $$"""{"kind":"tabs","id":"dock.tabs.a","panels":{{twoPanels}},"activePanel":"editor.hierarchy","hidden":["editor.console","editor.console"]}"""));

        // The active panel is hidden.
        AssertInvalidDock(LayoutJson(
            $$"""{"kind":"tabs","id":"dock.tabs.a","panels":{{twoPanels}},"activePanel":"editor.console","hidden":["editor.console"]}"""));

        // Every panel is hidden, leaving no visible tab.
        AssertInvalidDock(LayoutJson(
            $$"""{"kind":"tabs","id":"dock.tabs.a","panels":{{twoPanels}},"activePanel":"editor.hierarchy","hidden":{{twoPanels}}}"""));

        // Split nodes do not accept tab fields, including hidden.
        AssertInvalidDock(LayoutJson(
            """{"kind":"split","id":"dock.main","orientation":"horizontal","firstWeight":0.5,"secondWeight":0.5,"hidden":[]}"""));

        // A hidden entry must not be used to smuggle a panel into the group.
        AssertInvalidDock(LayoutJson(
            """{"kind":"tabs","id":"dock.tabs.a","panels":["editor.hierarchy"],"activePanel":"editor.hierarchy","hidden":["editor.scene"]}"""));
    }

    // D7-B: structurally invalid dock data falls back the whole layout and is
    // quarantine-eligible; no silent repair is attempted.
    [TestMethod]
    public void D7b_invalid_dock_data_falls_back_whole_layout_and_quarantines()
    {
        var hierarchy = TabsJson("dock.tabs.a", "editor.hierarchy");
        var scene = TabsJson("dock.tabs.b", "editor.scene");

        AssertInvalidDock(LayoutJson(
            """{"kind":"tabs","id":"dock.tabs.solo","panels":[],"activePanel":null}"""));

        AssertInvalidDock(LayoutJson(
            """{"kind":"grid","id":"dock.grid"}"""));

        AssertInvalidDock(LayoutJson(SplitJson("diagonal", "0.5", "0.5", hierarchy, scene)));
        AssertInvalidDock(LayoutJson(SplitJson("horizontal", "0.5", "0.6", hierarchy, scene)));

        AssertInvalidDock(LayoutJson(
            """{"kind":"tabs","id":"dock.tabs.a","panels":["editor.hierarchy","editor.hierarchy"],"activePanel":"editor.hierarchy"}"""));

        AssertInvalidDock(LayoutJson(
            """{"kind":"tabs","id":"dock.tabs.a","panels":["editor.hierarchy"],"activePanel":"editor.scene"}"""));

        AssertInvalidDock(LayoutJson(
            hierarchy,
            "[" + FloatJson(
                "dock.float.1",
                "dock.tabs.a",
                "editor.console",
                returnTarget: """{"groupId":"dock.tabs.a","index":0}""") + "]"));

        AssertInvalidDock(LayoutJson(
            hierarchy,
            "[" + FloatJson(
                "dock.float.1",
                "dock.float.1.tabs.1",
                "editor.hierarchy") + "]"));

        AssertInvalidDock(LayoutJson(
            hierarchy,
            "[" + FloatJson(
                "dock.float.1",
                "dock.float.1.tabs.1",
                "editor.console",
                bounds: """{"x":10,"y":20,"width":0,"height":200}""") + "]"));

        AssertInvalidDock(LayoutJson(
            hierarchy,
            "[" + FloatJson(
                "dock.float.1",
                "dock.float.1.tabs.1",
                "editor.console",
                returnTarget: """{"groupId":"dock.tabs.a","index":-1}""") + "]"));

        AssertInvalidDock(LayoutJson(
            hierarchy,
            """
            [{"id":"dock.float.1","bounds":{"x":10,"y":20,"width":300,"height":200},"maximized":false,"root":{"kind":"tabs","id":"dock.float.1.tabs.1","panels":["editor.console"],"activePanel":"editor.console"}}]
            """));

        AssertInvalidDock("""{"schemaVersion":2,"dock":{"floats":[]}}""");
    }

    // D7-B: corrupt files and unsupported schemas remain quarantine-eligible.
    [TestMethod]
    public void D7b_corrupt_and_unsupported_schema_kinds_are_distinct()
    {
        var empty = WorkspaceLayoutSerializer.Deserialize("   ");
        Assert.AreEqual(LayoutFallbackKind.Corrupt, empty.FallbackKind);
        Assert.IsTrue(empty.UsedFallback);
        Assert.IsTrue(empty.ShouldQuarantine);

        var corrupt = WorkspaceLayoutSerializer.Deserialize("{ not json");
        Assert.AreEqual(LayoutFallbackKind.Corrupt, corrupt.FallbackKind);
        Assert.IsTrue(corrupt.ShouldQuarantine);
        Assert.AreEqual(WorkspaceLayout.Default, corrupt.Layout);

        var future = WorkspaceLayoutSerializer.Serialize(WorkspaceLayout.Default)
            .Replace("\"schemaVersion\": 3", "\"schemaVersion\": 99", StringComparison.Ordinal);
        var unsupported = WorkspaceLayoutSerializer.Deserialize(future);
        Assert.AreEqual(LayoutFallbackKind.UnsupportedSchema, unsupported.FallbackKind);
        Assert.IsTrue(unsupported.ShouldQuarantine);
        Assert.AreEqual(WorkspaceLayout.Default, unsupported.Layout);
    }

    // D7-B: limits and cross-root identity collisions are rejected by the
    // candidate builder, not repaired.
    [TestMethod]
    public void D7b_codec_rejects_limits_and_cross_root_identity_collisions()
    {
        var main = DockWorkspaceSnapshotCodec.FromWorkspace(DockWorkspace.CreateDefault()).Main;
        Assert.IsNotNull(main);

        var tooManyFloats = Enumerable.Range(1, 17)
            .Select(index => new DockFloatRootSnapshot
            {
                Id = $"dock.float.{index}",
                Bounds = BoundsSnapshot(10d, 20d),
                ReturnTarget = new DockReturnTargetSnapshot
                {
                    GroupId = "dock.tabs.left",
                    Index = 0,
                },
                Root = TabsSnapshot($"dock.float.{index}.tabs.1", $"editor.float.{index}"),
            })
            .ToArray();
        var floatLimit = DockWorkspaceSnapshotCodec.ToWorkspace(new DockWorkspaceSnapshot
        {
            Main = main,
            Floats = tooManyFloats,
        });
        Assert.IsFalse(floatLimit.IsValid);
        StringAssert.Contains(floatLimit.Diagnostic, "float-limit");

        var panels = Enumerable.Range(0, 257).Select(index => $"editor.panel.{index}").ToArray();
        var panelLimit = DockWorkspaceSnapshotCodec.ToWorkspace(new DockWorkspaceSnapshot
        {
            Main = new DockNodeSnapshot
            {
                Kind = "tabs",
                Id = "dock.tabs.many",
                Panels = panels,
                ActivePanel = panels[0],
            },
        });
        Assert.IsFalse(panelLimit.IsValid);
        StringAssert.Contains(panelLimit.Diagnostic, "panel-limit");

        var duplicateNode = DockWorkspaceSnapshotCodec.ToWorkspace(new DockWorkspaceSnapshot
        {
            Main = TabsSnapshot("dock.tabs.shared", "editor.hierarchy"),
            Floats = new[]
            {
                new DockFloatRootSnapshot
                {
                    Id = "dock.float.1",
                    Bounds = BoundsSnapshot(10d, 20d),
                    ReturnTarget = new DockReturnTargetSnapshot
                    {
                        GroupId = "dock.tabs.shared",
                        Index = 0,
                    },
                    Root = TabsSnapshot("dock.tabs.shared", "editor.console"),
                },
            },
        });
        Assert.IsFalse(duplicateNode.IsValid);
        StringAssert.Contains(duplicateNode.Diagnostic, "duplicate-node-id");

        var duplicatePanel = DockWorkspaceSnapshotCodec.ToWorkspace(new DockWorkspaceSnapshot
        {
            Main = TabsSnapshot("dock.tabs.main", "editor.hierarchy"),
            Floats = new[]
            {
                new DockFloatRootSnapshot
                {
                    Id = "dock.float.1",
                    Bounds = BoundsSnapshot(10d, 20d),
                    ReturnTarget = new DockReturnTargetSnapshot
                    {
                        GroupId = "dock.tabs.main",
                        Index = 0,
                    },
                    Root = TabsSnapshot("dock.float.1.tabs.1", "editor.hierarchy"),
                },
            },
        });
        Assert.IsFalse(duplicatePanel.IsValid);
        StringAssert.Contains(duplicatePanel.Diagnostic, "duplicate-panel");

        var nonFiniteWeight = DockWorkspaceSnapshotCodec.ToWorkspace(new DockWorkspaceSnapshot
        {
            Main = new DockNodeSnapshot
            {
                Kind = "split",
                Id = "dock.main",
                Orientation = "horizontal",
                FirstWeight = double.NaN,
                SecondWeight = 0.5d,
                First = TabsSnapshot("dock.tabs.a", "editor.hierarchy"),
                Second = TabsSnapshot("dock.tabs.b", "editor.scene"),
            },
        });
        Assert.IsFalse(nonFiniteWeight.IsValid);
    }

    // D8-C: serialization refuses structurally invalid snapshots but writes
    // unknown identities back verbatim.
    [TestMethod]
    public void D8c_serializer_rejects_invalid_and_writes_unknown_dock_snapshots()
    {
        Assert.ThrowsException<DockModelException>(() => WorkspaceLayoutSerializer.Serialize(
            WorkspaceLayout.Default with
            {
                Dock = new DockWorkspaceSnapshot { Main = null },
            }));

        var unknownPanel = new PanelInstanceId("editor.future");
        var unknownTree = DockTree.Create(new DockTabGroupNode(
            new DockNodeId("dock.tabs.future"),
            new[] { unknownPanel },
            unknownPanel));
        var unknownWorkspace = DockWorkspace.Create(unknownTree);
        var unknownLayout = WorkspaceLayout.Default with
        {
            Dock = DockWorkspaceSnapshotCodec.FromWorkspace(unknownWorkspace),
        };

        var json = WorkspaceLayoutSerializer.Serialize(unknownLayout);
        StringAssert.Contains(json, "editor.future");

        var parsed = WorkspaceLayoutSerializer.Deserialize(json);
        Assert.IsFalse(parsed.UsedFallback, parsed.Reason);
        Assert.AreEqual(unknownWorkspace.Digest, parsed.DockWorkspace!.Digest);
        CollectionAssert.AreEqual(
            new[] { unknownPanel },
            parsed.UnresolvedPanels!.ToArray());
    }

    private static DockWorkspace CreateComplexWorkspace()
    {
        var workspace = RequireSuccess(
            DockWorkspace.CreateDefault().FloatPanel(DockPanelIds.Console, FirstBounds));
        var consoleGroupId = RequireGroup(workspace.FloatingRoots[0].Root.Root).Id;

        workspace = RequireSuccess(workspace.MovePanel(
            DockPanelIds.Scene,
            new DockTarget.Edge(consoleGroupId, DockEdge.Right)));
        workspace = RequireSuccess(
            workspace.FloatPanel(DockPanelIds.Inspector, SecondBounds));
        workspace = RequireSuccess(workspace.UpdateFloatBounds(
            workspace.FloatingRoots[0].Id,
            MaximizedBounds,
            maximized: true));
        return workspace;
    }

    private static WorkspaceLayout LayoutWithDock(DockWorkspace workspace)
        => WorkspaceLayout.Default with
        {
            WindowWidth = 1440,
            WindowHeight = 900,
            ShowInspector = false,
            ThemeId = "nova.light",
            Dock = DockWorkspaceSnapshotCodec.FromWorkspace(workspace),
        };

    private static void AssertInvalidDock(string json)
    {
        var parsed = WorkspaceLayoutSerializer.Deserialize(json);

        Assert.IsTrue(parsed.UsedFallback, $"Expected a fallback for: {json}");
        Assert.AreEqual(LayoutFallbackKind.InvalidDock, parsed.FallbackKind, json);
        Assert.IsTrue(parsed.ShouldQuarantine, json);
        Assert.AreEqual(WorkspaceLayout.Default, parsed.Layout, json);
        Assert.IsNotNull(parsed.DockWorkspace, json);
        Assert.AreEqual(
            DockWorkspace.CreateDefault().Digest,
            parsed.DockWorkspace!.Digest,
            json);
        Assert.IsNotNull(parsed.Reason, json);
    }

    private static string LayoutJson(string main, string floats = "[]")
        => $$"""
            {
              "schemaVersion": 3,
              "windowWidth": 1400,
              "windowHeight": 900,
              "hierarchyWidth": 240,
              "inspectorWidth": 300,
              "consoleHeight": 180,
              "showHierarchy": true,
              "showInspector": true,
              "showConsole": true,
              "themeId": "nova.dark",
              "dock": {
                "main": {{main}},
                "floats": {{floats}}
              }
            }
            """;

    private static string TabsJson(string id, string panel, string? hidden = null)
        => hidden is null
            ? $$"""{"kind":"tabs","id":"{{id}}","panels":["{{panel}}"],"activePanel":"{{panel}}"}"""
            : $$"""{"kind":"tabs","id":"{{id}}","panels":["{{panel}}"],"hidden":{{hidden}},"activePanel":"{{panel}}"}""";

    private static string SplitJson(
        string orientation,
        string firstWeight,
        string secondWeight,
        string first,
        string second)
        => $$"""{"kind":"split","id":"dock.main","orientation":"{{orientation}}","firstWeight":{{firstWeight}},"secondWeight":{{secondWeight}},"first":{{first}},"second":{{second}}}""";

    private static string FloatJson(
        string id,
        string rootId,
        string panel,
        string bounds = """{"x":10,"y":20,"width":300,"height":200}""",
        string returnTarget = """{"groupId":"dock.tabs.a","index":0}""")
        => $$"""
            {
              "id": "{{id}}",
              "bounds": {{bounds}},
              "maximized": false,
              "returnTarget": {{returnTarget}},
              "root": {{TabsJson(rootId, panel)}}
            }
            """;

    private static DockBoundsSnapshot BoundsSnapshot(double x, double y)
        => new() { X = x, Y = y, Width = 300d, Height = 200d };

    private static DockNodeSnapshot TabsSnapshot(string id, string panel)
        => new()
        {
            Kind = "tabs",
            Id = id,
            Panels = new[] { panel },
            ActivePanel = panel,
        };

    private static DockWorkspace RequireSuccess(DockWorkspaceResult result)
    {
        Assert.IsTrue(result.Success, result.Diagnostic);
        Assert.IsNotNull(result.Workspace);
        return result.Workspace!;
    }

    private static DockTabGroupNode RequireGroup(DockNode? node)
    {
        Assert.IsInstanceOfType(node, typeof(DockTabGroupNode));
        return (DockTabGroupNode)node!;
    }

    private static void AssertPanels(
        IEnumerable<PanelInstanceId> expected,
        IEnumerable<PanelInstanceId> actual)
    {
        var expectedText = string.Join(",", expected.Select(panel => panel.ToString()));
        var actualText = string.Join(",", actual.Select(panel => panel.ToString()));
        Assert.AreEqual(expectedText, actualText);
    }
}
