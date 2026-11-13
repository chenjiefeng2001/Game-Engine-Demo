using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Automation;
using AvaloniaEditor.Native;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EditorAvalonia.IntegrationTests;

[TestClass]
public sealed class B6MeshEditorUITests
{
    private const string HostTitle = "Avalonia Editor — M1IMPORT-HOST";
    private static readonly TimeSpan UiTimeout = TimeSpan.FromSeconds(60);

    [TestMethod]
    public void Mesh_asset_can_be_added_saved_and_reloaded_after_editor_restart()
    {
        try { _ = AutomationElement.RootElement; }
        catch (Exception ex)
        {
            Assert.Inconclusive("UI Automation is unavailable (interactive desktop required): " + ex.Message);
        }

        string repo = LocateRepoRoot();
        string editor = LocateEditorExe(repo);
        string scratch = Path.Combine(Path.GetTempPath(),
            "engine_b6ui_" + Guid.NewGuid().ToString("N"));
        Process? process = null;
        AutomationElement? main = null;
        bool passed = false;
        try
        {
            Directory.CreateDirectory(Path.Combine(scratch, "assets", "gp01"));
            Directory.CreateDirectory(Path.Combine(scratch, "assets", "models"));
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "manifest.json"),
                "{\"version\":1,\"assets\":[]}");
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "Main.scene"),
                "{\"format\":\"engine.scene\",\"version\":1,\"entities\":[]}");
            File.WriteAllText(Path.Combine(scratch, "assets", "models", "b6_quad.obj"),
                BuildQuadObj());
            File.WriteAllText(Path.Combine(scratch, "assets", "models", "b6_quad.mtl"),
                "newmtl MatB6\nKd 1 1 1\n");

            string guid = ImportMesh(repo, scratch);
            Assert.AreEqual(32, guid.Length);

            process = Launch(editor, repo, scratch);
            main = WaitForMainWindow(process, UiTimeout);

            SelectTab(main, "Asset Browser");
            SelectMeshAsset(main);
            ClickWhenEnabled(main, "AddMeshToSceneBtn");
            WaitForInspectorValue(main, "MeshGuidValue", guid, UiTimeout);
            WaitForInspectorValue(main, "MeshStatusValue", "Ready", UiTimeout);
            WaitForViewportNonClear(main, minimum: 20000, UiTimeout);

            SetValue(main, "PosX", "2");
            SetValue(main, "RotZ", "45");
            SetValue(main, "ScaleX", "2");
            ClickWhenEnabled(main, "ApplyTransformBtn");
            WaitForValue(main, "PosX", "2.0");
            ClickWhenEnabled(main, "UndoSceneBtn");
            WaitForValue(main, "PosX", "0.0");
            ClickWhenEnabled(main, "RedoSceneBtn");
            WaitForValue(main, "PosX", "2.0");

            SelectAssetBySuffix(main, ".material.json");
            WaitForValue(main, "MaterialBaseColorR", "1");
            SetValue(main, "MaterialBaseColorR", "0.25");
            ClickWhenEnabled(main, "SaveMaterialBtn");
            WaitForValue(main, "MaterialBaseColorR", "0.25");

            SelectMeshAsset(main);
            ClickWhenEnabled(main, "ShowDependenciesBtn");
            WaitUntil(() =>
            {
                var dep = FindByAutomationId(main, "DependencyList");
                return dep is not null && Descendants(dep, ControlType.ListItem)
                    .Any(i => SafeName(i).Contains(".material.json", StringComparison.OrdinalIgnoreCase));
            }, UiTimeout, "dependency view shows material");

            ClickWhenEnabled(main, "SaveProjectBtn");
            Thread.Sleep(1000);

            CloseProcess(ref process);
            process = Launch(editor, repo, scratch);
            main = WaitForMainWindow(process, UiTimeout);
            SelectFirstHierarchyItem(main);
            WaitForInspectorValue(main, "MeshGuidValue", guid, UiTimeout);
            WaitForInspectorValue(main, "MeshStatusValue", "Ready", UiTimeout);
            WaitForValue(main, "PosX", "2.0");
            WaitForValue(main, "RotZ", "45.0");
            WaitForValue(main, "ScaleX", "2.0");
            WaitForViewportNonClear(main, minimum: 20000, UiTimeout);

            var editorHandle = new IntPtr(main!.Current.NativeWindowHandle);
            ShowWindow(editorHandle, SwRestore);
            keybd_event(VkMenu, 0, 0, UIntPtr.Zero);
            keybd_event(VkMenu, 0, KeyEventKeyUp, UIntPtr.Zero);
            _ = SetForegroundWindow(editorHandle);
            Thread.Sleep(300);
            ClickWhenEnabled(main, "GizmoTranslateBtn");
            string hints = WaitForGizmoHints(main);
            var parts = hints.Split(';');
            Assert.AreEqual("visible", parts[0]);
            double ox = double.Parse(parts[1], System.Globalization.CultureInfo.InvariantCulture);
            double oy = double.Parse(parts[2], System.Globalization.CultureInfo.InvariantCulture);
            double ex = double.Parse(parts[3], System.Globalization.CultureInfo.InvariantCulture);
            double ey = double.Parse(parts[4], System.Globalization.CultureInfo.InvariantCulture);
            double vx = ex - ox, vy = ey - oy;
            double vlen = Math.Sqrt(vx * vx + vy * vy);
            Assert.IsTrue(vlen > 8, "gizmo axis must be visible");
            SendMouse(editorHandle, WmMouseMove, ox + vx * 0.20, oy + vy * 0.20, false);
            SendMouse(editorHandle, WmLeftButtonDown, ox + vx * 0.20, oy + vy * 0.20, true);
            Thread.Sleep(80);
            SendMouse(editorHandle, WmMouseMove, ox + vx * 0.50, oy + vy * 0.50, true);
            Thread.Sleep(80);
            SendMouse(editorHandle, WmMouseMove, ox + vx * 0.80, oy + vy * 0.80, true);
            Thread.Sleep(80);
            SendMouse(editorHandle, WmLeftButtonUp, ox + vx * 0.80, oy + vy * 0.80, false);
            WaitUntil(() => ReadValue(main!, "PosX") != "2.0", UiTimeout,
                      "gizmo drag changes PosX");
            ClickWhenEnabled(main, "UndoSceneBtn");
            WaitForValue(main, "PosX", "2.0");
            DragGizmoAxis(main, editorHandle, "GizmoRotateBtn");
            WaitUntil(() => ReadValue(main!, "RotX") != "0.0", UiTimeout,
                      "rotate gizmo changes RotX");
            ClickWhenEnabled(main, "UndoSceneBtn");
            WaitForValue(main, "RotX", "0.0");

            DragGizmoAxis(main, editorHandle, "GizmoScaleBtn");
            WaitUntil(() => ReadValue(main!, "ScaleX") != "2.0", UiTimeout,
                      "scale gizmo changes ScaleX");
            ClickWhenEnabled(main, "UndoSceneBtn");
            WaitForValue(main, "ScaleX", "2.0");

            SelectTab(main, "Asset Browser");
            SelectAssetBySuffix(main, ".material.json");
            WaitForValue(main, "MaterialBaseColorR", "0.25");

            Console.WriteLine("[B6UI] PASS scratch=" + scratch);
            passed = true;
        }
        finally
        {
            CloseProcess(ref process);
            if (passed) TryDelete(scratch);
            else Console.WriteLine("[B6UI] SCRATCH PRESERVED: " + scratch);
        }
    }

    [TestMethod]
    public void Hierarchy_reparent_keeps_scene_and_undo()
    {
        try { _ = AutomationElement.RootElement; }
        catch (Exception ex)
        {
            Assert.Inconclusive("UI Automation is unavailable (interactive desktop required): " + ex.Message);
        }

        string repo = LocateRepoRoot();
        string editor = LocateEditorExe(repo);
        string scratch = Path.Combine(Path.GetTempPath(),
            "engine_g2ui_" + Guid.NewGuid().ToString("N"));
        Process? process = null;
        bool passed = false;
        try
        {
            Directory.CreateDirectory(Path.Combine(scratch, "assets", "gp01"));
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "manifest.json"),
                "{\"version\":1,\"assets\":[]}");
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "Main.scene"),
                "{\"format\":\"engine.scene\",\"version\":1,\"entities\":[]}");

            process = Launch(editor, repo, scratch);
            AutomationElement main = WaitForMainWindow(process, UiTimeout);
            ClickWhenEnabled(main, "HierarchyCreateEntityBtn");
            var tree0 = WaitFor(() => FindByAutomationId(main, "HierarchyList"), UiTimeout, "HierarchyList");
            WaitUntil(() => Descendants(tree0, ControlType.ListItem).Count > 0, UiTimeout, "first hierarchy item");
            Thread.Sleep(500);
            ClickWhenEnabled(main, "HierarchyCreateChildBtn");
            SetValue(main, "PosX", "3");
            ClickWhenEnabled(main, "ApplyTransformBtn");
            WaitForValue(main, "PosX", "3.0");
            ClickWhenEnabled(main, "UndoSceneBtn");
            WaitForValue(main, "PosX", "0.0");
            ClickWhenEnabled(main, "RedoSceneBtn");
            WaitForValue(main, "PosX", "3.0");

            var scenePath = Path.Combine(scratch, "assets", "gp01", "Main.scene");
            ClickWhenEnabled(main, "SaveProjectBtn");
            Thread.Sleep(300);
            var initial = ReadSceneParents(scenePath);
            ulong originalParent = initial["Entity_2"];
            Assert.AreNotEqual(0UL, originalParent,
                "child must be parented in the Scene hierarchy");

            ClickWhenEnabled(main, "HierarchyOutdentBtn");
            ClickWhenEnabled(main, "SaveProjectBtn");
            Thread.Sleep(300);
            var outdented = ReadSceneParents(scenePath);
            Assert.AreEqual(0UL, outdented["Entity_2"],
                "Outdent must reparent the selected entity to root");

            ClickWhenEnabled(main, "UndoSceneBtn");
            ClickWhenEnabled(main, "SaveProjectBtn");
            Thread.Sleep(300);
            var afterUndo = ReadSceneParents(scenePath);
            Assert.AreEqual(originalParent, afterUndo["Entity_2"],
                "undo must restore the previous parent");
            ClickWhenEnabled(main, "RedoSceneBtn");
            ClickWhenEnabled(main, "SaveProjectBtn");
            Thread.Sleep(300);
            var afterRedo = ReadSceneParents(scenePath);
            Assert.AreEqual(0UL, afterRedo["Entity_2"],
                "redo must restore the reparented hierarchy");

            passed = true;
        }
        finally
        {
            CloseProcess(ref process);
            if (passed) TryDelete(scratch);
            else Console.WriteLine("[G2UI] SCRATCH PRESERVED: " + scratch);
        }
    }

    [TestMethod]
    public void Hierarchy_duplicate_is_one_transaction_and_saves_new_ids()
    {
        try { _ = AutomationElement.RootElement; }
        catch (Exception ex)
        {
            Assert.Inconclusive("UI Automation is unavailable (interactive desktop required): " + ex.Message);
        }

        string repo = LocateRepoRoot();
        string editor = LocateEditorExe(repo);
        string scratch = Path.Combine(Path.GetTempPath(),
            "engine_g3ui_" + Guid.NewGuid().ToString("N"));
        Process? process = null;
        bool passed = false;
        try
        {
            Directory.CreateDirectory(Path.Combine(scratch, "assets", "gp01"));
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "manifest.json"),
                "{\"version\":1,\"assets\":[]}");
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "Main.scene"),
                "{\"format\":\"engine.scene\",\"version\":1,\"entities\":[]}");

            process = Launch(editor, repo, scratch);
            AutomationElement main = WaitForMainWindow(process, UiTimeout);
            ClickWhenEnabled(main, "HierarchyCreateEntityBtn");
            var list = WaitFor(() => FindByAutomationId(main, "HierarchyList"),
                UiTimeout, "HierarchyList");
            WaitUntil(() => Descendants(list, ControlType.ListItem).Count >= 1,
                UiTimeout, "source hierarchy row");

            ClickWhenEnabled(main, "HierarchyDuplicateEntityBtn");
            WaitUntil(() => Descendants(list, ControlType.ListItem).Count >= 2,
                UiTimeout, "duplicate hierarchy row");
            var scenePath = Path.Combine(scratch, "assets", "gp01", "Main.scene");
            ClickWhenEnabled(main, "SaveProjectBtn");
            Thread.Sleep(300);
            var duplicated = ReadSceneEntityIds(scenePath);
            Assert.AreEqual(2, duplicated.Count, "duplicate must save two roots");
            CollectionAssert.AreEquivalent(
                new[] { "Entity", "Entity_Copy" }, duplicated.Keys.ToArray());
            Assert.AreNotEqual(duplicated["Entity"], duplicated["Entity_Copy"],
                "duplicate must allocate a new Scene Entity ID");

            ClickWhenEnabled(main, "UndoSceneBtn");
            ClickWhenEnabled(main, "SaveProjectBtn");
            Thread.Sleep(300);
            Assert.AreEqual(1, ReadSceneEntityIds(scenePath).Count,
                "one Undo must remove the complete duplicate transaction");

            ClickWhenEnabled(main, "RedoSceneBtn");
            ClickWhenEnabled(main, "SaveProjectBtn");
            Thread.Sleep(300);
            var redone = ReadSceneEntityIds(scenePath);
            Assert.AreEqual(2, redone.Count);
            Assert.AreEqual(duplicated["Entity_Copy"], redone["Entity_Copy"],
                "redo must preserve the duplicate Scene Entity ID");

            passed = true;
        }
        finally
        {
            CloseProcess(ref process);
            if (passed) TryDelete(scratch);
            else Console.WriteLine("[G3UI] SCRATCH PRESERVED: " + scratch);
        }
    }

    private static Dictionary<string, ulong> ReadSceneEntityIds(string path)
    {
        using var doc = System.Text.Json.JsonDocument.Parse(File.ReadAllText(path));
        var result = new Dictionary<string, ulong>();
        foreach (var e in doc.RootElement.GetProperty("entities").EnumerateArray())
        {
            string name = e.GetProperty("name").GetString() ?? "";
            result[name] = e.GetProperty("id").GetUInt64();
        }
        return result;
    }

    [TestMethod]
    public void Prefab_create_publishes_asset_and_shows_dependency()
    {
        try { _ = AutomationElement.RootElement; }
        catch (Exception ex)
        {
            Assert.Inconclusive("UI Automation is unavailable (interactive desktop required): " + ex.Message);
        }

        string repo = LocateRepoRoot();
        string editor = LocateEditorExe(repo);
        string scratch = Path.Combine(Path.GetTempPath(),
            "engine_g4ui_" + Guid.NewGuid().ToString("N"));
        Process? process = null;
        bool passed = false;
        try
        {
            Directory.CreateDirectory(Path.Combine(scratch, "assets", "gp01"));
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "manifest.json"),
                "{\"version\":1,\"assets\":[]}");
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "Main.scene"),
                "{\"format\":\"engine.scene\",\"version\":1,\"entities\":[{\"name\":\"PrefabMeshRoot\",\"components\":[{\"type\":\"Mesh\",\"data\":{\"meshGuid\":\"ffffffffffffffffffffffffffffffff\"}}]}]}");

            process = Launch(editor, repo, scratch);
            AutomationElement main = WaitForMainWindow(process, UiTimeout);
            SelectFirstHierarchyItem(main);
            SetValue(main, "PrefabNameBox", "UiPrefab");
            ClickWhenEnabled(main, "CreatePrefabBtn");
            SelectTab(main, "Asset Browser");
            var assets = WaitFor(() => FindByAutomationId(main, "AssetList"),
                UiTimeout, "AssetList");
            WaitUntil(() => Descendants(assets, ControlType.ListItem)
                .Any(i => ItemText(i).Contains("assets/prefabs/UiPrefab.",
                    StringComparison.OrdinalIgnoreCase)), UiTimeout, "Prefab asset row");
            var item = Descendants(assets, ControlType.ListItem)
                .First(i => ItemText(i).Contains("assets/prefabs/UiPrefab.",
                    StringComparison.OrdinalIgnoreCase));
            string text = ItemText(item);
            StringAssert.Contains(text, "Prefab");
            StringAssert.Contains(text, "Available");
            StringAssert.Contains(text, "assets/prefabs/UiPrefab.");
            Assert.IsTrue(Regex.IsMatch(text, "[0-9a-f]{32}"), "Prefab GUID missing: " + text);
            ((SelectionItemPattern)item.GetCurrentPattern(SelectionItemPattern.Pattern)).Select();
            Thread.Sleep(300);
            var hierarchyAfter = FindByAutomationId(main, "HierarchyList");
            Assert.IsNotNull(hierarchyAfter);
            Assert.AreEqual(1, Descendants(hierarchyAfter!, ControlType.ListItem).Count,
                "Create Prefab must not replace or delete the source entity");
            ClickWhenEnabled(main, "ShowDependenciesBtn");
            WaitUntil(() =>
            {
                var deps = FindByAutomationId(main, "DependencyList");
                return deps is not null && Descendants(deps, ControlType.ListItem)
                    .Any(i => SafeName(i).Contains("Mesh") &&
                              SafeName(i).Contains("UNRESOLVED"));
            }, UiTimeout, "Prefab missing Mesh dependency is visible");


            passed = true;
        }
        finally
        {
            CloseProcess(ref process);
            if (passed) TryDelete(scratch);
            else Console.WriteLine("[G4UI] SCRATCH PRESERVED: " + scratch);
        }
    }

    [TestMethod]
    public void Prefab_create_failure_leaves_scene_and_reports_error()
    {
        try { _ = AutomationElement.RootElement; }
        catch (Exception ex)
        {
            Assert.Inconclusive("UI Automation is unavailable (interactive desktop required): " + ex.Message);
        }

        string repo = LocateRepoRoot();
        string editor = LocateEditorExe(repo);
        string scratch = Path.Combine(Path.GetTempPath(),
            "engine_g4ui_fail_" + Guid.NewGuid().ToString("N"));
        Process? process = null;
        bool passed = false;
        try
        {
            const string scriptGuid = "abcdefabcdefabcdefabcdefabcdefab";
            Directory.CreateDirectory(Path.Combine(scratch, "assets", "gp01"));
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "test.lua"),
                "return true\n");
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "manifest.json"),
                "{\"version\":1,\"assets\":[{\"guid\":\"" + scriptGuid +
                "\",\"path\":\"assets/gp01/test.lua\",\"type\":\"Script\"}]}");
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "Main.scene"),
                "{\"format\":\"engine.scene\",\"version\":1,\"entities\":[{\"name\":\"ScriptedRoot\",\"script\":\"" +
                scriptGuid + "\"}]}");

            process = Launch(editor, repo, scratch);
            AutomationElement main = WaitForMainWindow(process, UiTimeout);
            SelectFirstHierarchyItem(main);
            SetValue(main, "PrefabNameBox", "MustFail");
            ClickWhenEnabled(main, "CreatePrefabBtn");
            WaitUntil(() =>
            {
                var diagnostic = FindByAutomationId(main, "PrefabDiagnostic");
                return diagnostic is not null &&
                    SafeName(diagnostic).Contains("legacy", StringComparison.OrdinalIgnoreCase);
            }, UiTimeout, "Prefab legacy binding failure diagnostic");
            var hierarchy = FindByAutomationId(main, "HierarchyList");
            Assert.IsNotNull(hierarchy);
            Assert.AreEqual(1, Descendants(hierarchy!, ControlType.ListItem).Count,
                "failed Create Prefab must leave the Scene hierarchy unchanged");

            passed = true;
        }
        finally
        {
            CloseProcess(ref process);
            if (passed) TryDelete(scratch);
            else Console.WriteLine("[G4UI FAIL] SCRATCH PRESERVED: " + scratch);
        }
    }

    [TestMethod]
    public void Prefab_publish_failure_leaves_scene_unchanged()
    {
        try { _ = AutomationElement.RootElement; }
        catch (Exception ex)
        {
            Assert.Inconclusive("UI Automation is unavailable (interactive desktop required): " + ex.Message);
        }

        string repo = LocateRepoRoot();
        string editor = LocateEditorExe(repo);
        string scratch = Path.Combine(Path.GetTempPath(),
            "engine_g4ui_publishfail_" + Guid.NewGuid().ToString("N"));
        Process? process = null;
        bool passed = false;
        try
        {
            Directory.CreateDirectory(Path.Combine(scratch, "assets", "gp01"));
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "manifest.json"),
                "{\"version\":1,\"assets\":[]}");
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "Main.scene"),
                "{\"format\":\"engine.scene\",\"version\":1,\"entities\":[]}");
            // Force RevisionStore candidate creation to fail without invalidating open.
            Directory.CreateDirectory(Path.Combine(scratch, ".engine-content"));
            File.WriteAllText(Path.Combine(scratch, ".engine-content", "revisions"),
                "not a directory");

            process = Launch(editor, repo, scratch);
            AutomationElement main = WaitForMainWindow(process, UiTimeout);
            ClickWhenEnabled(main, "HierarchyCreateEntityBtn");
            SelectFirstHierarchyItem(main);
            SetValue(main, "PrefabNameBox", "PublishMustFail");
            ClickWhenEnabled(main, "CreatePrefabBtn");
            WaitUntil(() =>
            {
                var diagnostic = FindByAutomationId(main, "PrefabDiagnostic");
                return diagnostic is not null && SafeName(diagnostic).Length > 0;
            }, UiTimeout, "Prefab publish failure diagnostic");
            var hierarchy = FindByAutomationId(main, "HierarchyList");
            Assert.IsNotNull(hierarchy);
            Assert.AreEqual(1, Descendants(hierarchy!, ControlType.ListItem).Count,
                "publish failure must leave the Scene hierarchy unchanged");

            passed = true;
        }
        finally
        {
            CloseProcess(ref process);
            if (passed) TryDelete(scratch);
            else Console.WriteLine("[G4UI PUBLISH FAIL] SCRATCH PRESERVED: " + scratch);
        }
    }
    [TestMethod]
    public void Prefab_instance_inspector_tracks_override_metadata()
    {
        try { _ = AutomationElement.RootElement; }
        catch (Exception ex)
        {
            Assert.Inconclusive("UI Automation is unavailable (interactive desktop required): " + ex.Message);
        }

        string repo = LocateRepoRoot();
        string editor = LocateEditorExe(repo);
        string scratch = Path.Combine(Path.GetTempPath(),
            "engine_g6ui_" + Guid.NewGuid().ToString("N"));
        Process? process = null;
        bool passed = false;
        try
        {
            Directory.CreateDirectory(Path.Combine(scratch, "assets", "gp01"));
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "manifest.json"),
                "{\"version\":1,\"assets\":[]}");
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "Main.scene"),
                "{\"format\":\"engine.scene\",\"version\":1,\"entities\":[]}");
            PreparePrefabInstanceProject(scratch);

            process = Launch(editor, repo, scratch);
            AutomationElement main = WaitForMainWindow(process, UiTimeout);
            SelectFirstHierarchyItem(main);
            WaitUntil(() =>
            {
                var status = FindByAutomationId(main, "PrefabInstanceStatus");
                return status is not null && SafeName(status).Contains("Prefab");
            }, UiTimeout, "Prefab instance inspector status");
            SetValue(main, "PosX", "4");
            ClickWhenEnabled(main, "ApplyTransformBtn");
            WaitUntil(() =>
            {
                var status = FindByAutomationId(main, "PrefabInstanceStatus");
                var fields = FindByAutomationId(main, "PrefabOverrideFields");
                return status is not null && SafeName(status).Contains("overrides") &&
                       fields is not null && SafeName(fields).Contains("Transform.position.x");
            }, UiTimeout, "Inspector update creates a Transform override");

            ClickWhenEnabled(main, "UndoSceneBtn");
            WaitUntil(() =>
            {
                var fields = FindByAutomationId(main, "PrefabOverrideFields");
                return fields is not null && !SafeName(fields).Contains("Transform.position.x");
            }, UiTimeout, "Undo removes the Inspector override metadata");


            SetValue(main, "PosX", "6");
            ClickWhenEnabled(main, "ApplyTransformBtn");
            WaitUntil(() =>
            {
                var fields = FindByAutomationId(main, "PrefabOverrideFields");
                return fields is not null && SafeName(fields).Contains("Transform.position.x");
            }, UiTimeout, "second override for field revert");
            ClickWhenEnabled(main, "RevertOverrideBtn");
            WaitUntil(() =>
            {
                var status = FindByAutomationId(main, "PrefabInstanceStatus");
                var fields = FindByAutomationId(main, "PrefabOverrideFields");
                return status is not null && SafeName(status).Contains("clean") &&
                       (fields is null || !SafeName(fields).Contains("Transform.position.x"));
            }, UiTimeout, "field revert clears override");

            SetValue(main, "PosX", "7");
            ClickWhenEnabled(main, "ApplyTransformBtn");
            WaitUntil(() =>
            {
                var fields = FindByAutomationId(main, "PrefabOverrideFields");
                return fields is not null && SafeName(fields).Contains("Transform.position.x");
            }, UiTimeout, "override before instance revert");
            ClickWhenEnabled(main, "RevertInstanceBtn");
            WaitUntil(() =>
            {
                var status = FindByAutomationId(main, "PrefabInstanceStatus");
                return status is not null && SafeName(status).Contains("clean");
            }, UiTimeout, "instance revert clears overrides");

            SetValue(main, "PosX", "8");
            ClickWhenEnabled(main, "ApplyTransformBtn");
            WaitUntil(() =>
            {
                var fields = FindByAutomationId(main, "PrefabOverrideFields");
                return fields is not null && SafeName(fields).Contains("Transform.position.x");
            }, UiTimeout, "override before Apply");
            ClickWhenEnabled(main, "ApplyPrefabBtn");
            WaitUntil(() =>
            {
                var status = FindByAutomationId(main, "PrefabInstanceStatus");
                var fields = FindByAutomationId(main, "PrefabOverrideFields");
                return status is not null && SafeName(status).Contains("clean") &&
                       (fields is null || !SafeName(fields).Contains("Transform.position.x"));
            }, UiTimeout, "Apply clears overrides and updates source");
            passed = true;
        }
        finally
        {
            CloseProcess(ref process);
            if (passed) TryDelete(scratch);
            else Console.WriteLine("[G6UI] SCRATCH PRESERVED: " + scratch);
        }
    }

    [TestMethod]
    public void Prefab_reconciliation_status_preview_and_apply()
    {
        try { _ = AutomationElement.RootElement; }
        catch (Exception ex)
        {
            Assert.Inconclusive("UI Automation is unavailable (interactive desktop required): " + ex.Message);
        }

        string repo = LocateRepoRoot();
        string editor = LocateEditorExe(repo);
        string scratch = Path.Combine(Path.GetTempPath(),
            "engine_g7ui_" + Guid.NewGuid().ToString("N"));
        Process? process = null;
        bool passed = false;
        try
        {
            Directory.CreateDirectory(Path.Combine(scratch, "assets", "gp01"));
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "manifest.json"),
                "{\"version\":1,\"assets\":[]}");
            File.WriteAllText(Path.Combine(scratch, "assets", "gp01", "Main.scene"),
                "{\"format\":\"engine.scene\",\"version\":1,\"entities\":[]}");
            PrepareReconciliationProject(scratch, conflict: true);

            process = Launch(editor, repo, scratch);
            AutomationElement main = WaitForMainWindow(process, UiTimeout);
            SelectFirstHierarchyItem(main);
            WaitUntil(() =>
            {
                var status = FindByAutomationId(main, "PrefabReconciliationStatus");
                return status is not null && SafeName(status).Contains("Out of date");
            }, UiTimeout, "Prefab reconciliation out-of-date status");

            ClickWhenEnabled(main, "PreviewReconciliationBtn");
            WaitUntil(() =>
            {
                var conflicts = FindByAutomationId(main, "PrefabConflictList");
                return conflicts is not null && Descendants(conflicts, ControlType.ListItem).Count > 0;
            }, UiTimeout, "Prefab reconciliation conflict list");
            Assert.IsTrue(Descendants(FindByAutomationId(main, "PrefabConflictList"),
                ControlType.ListItem).Any(i => ItemText(i).Contains("field:")),
                "stable field conflict ID must be visible");

            ClickWhenEnabled(main, "TakeSourceConflictBtn");
            WaitUntil(() =>
            {
                var list = FindByAutomationId(main, "PrefabConflictList");
                return list is null || Descendants(list, ControlType.ListItem).Count == 0;
            }, UiTimeout, "Take Source clears the conflict");
            ClickWhenEnabled(main, "ApplyReconciliationBtn");
            WaitUntil(() =>
            {
                var status = FindByAutomationId(main, "PrefabReconciliationStatus");
                return status is not null && SafeName(status).Contains("Up to date");
            }, UiTimeout, "Prefab reconciliation applied");
            WaitForValue(main, "PosX", "5.0");
            passed = true;
        }
        finally
        {
            CloseProcess(ref process);
            if (passed) TryDelete(scratch);
            else Console.WriteLine("[G7UI] SCRATCH PRESERVED: " + scratch);
        }
    }
    private static void PrepareReconciliationProject(string scratch, bool conflict)
    {
        Assert.AreEqual(1, EditorBridgeApi.EditorBridge_RegisterMainThread());
        IntPtr session = EditorBridgeApi.EditorSession_Create();
        Assert.AreNotEqual(IntPtr.Zero, session);
        try
        {
            Assert.IsTrue(EditorBridgeApi.EditorSession_OpenProjectRooted(session,
                scratch, "assets/gp01/manifest.json", "assets/gp01/Main.scene"));
            int source = EditorBridgeApi.EditorSession_CreateEntity(session, "ReconciliationSource");
            Assert.IsTrue(source >= 0, "create source failed");
            ulong sourceId = EditorBridgeApi.EditorSession_GetEntitySceneId(session, source);
            int asset = EditorBridgeApi.EditorSession_CreatePrefabFromEntity(
                session, sourceId, "ReconciliationPrefab");
            Assert.IsTrue(asset >= 0, "create prefab failed");
            Assert.AreEqual(0, EditorBridgeApi.EditorSession_InstantiatePrefab(
                session, asset, 0, out ulong firstRoot));
            Assert.AreEqual(0, EditorBridgeApi.EditorSession_InstantiatePrefab(
                session, asset, 0, out ulong secondRoot));
            SetTransform(session, firstRoot, 5f);
            Assert.IsTrue(EditorBridgeApi.EditorSession_SaveProject(session));
            Assert.AreEqual(0, EditorBridgeApi.EditorSession_ApplyPrefabOverrides(
                session, firstRoot), "apply first instance failed");
            if (conflict) SetTransform(session, secondRoot, 3f);
            Assert.AreEqual(0, EditorBridgeApi.EditorSession_DeleteEntityById(session, firstRoot));
            Assert.AreEqual(0, EditorBridgeApi.EditorSession_DeleteEntityById(session, sourceId));
            Assert.IsTrue(EditorBridgeApi.EditorSession_SaveProject(session));
        }
        finally
        {
            EditorBridgeApi.EditorSession_Destroy(session);
        }
    }

    private static void SetTransform(IntPtr session, ulong entityId, float x)
    {
        IntPtr pos = Marshal.AllocHGlobal(12);
        IntPtr euler = Marshal.AllocHGlobal(12);
        IntPtr scale = Marshal.AllocHGlobal(12);
        try
        {
            Marshal.Copy(new[] { x, 0f, 0f }, 0, pos, 3);
            Marshal.Copy(new[] { 0f, 0f, 0f }, 0, euler, 3);
            Marshal.Copy(new[] { 1f, 1f, 1f }, 0, scale, 3);
            Assert.AreEqual(0, EditorBridgeApi.EditorSession_SetEntityTransformById(
                session, entityId, pos, euler, scale));
        }
        finally
        {
            Marshal.FreeHGlobal(pos);
            Marshal.FreeHGlobal(euler);
            Marshal.FreeHGlobal(scale);
        }
    }
    private static void PreparePrefabInstanceProject(string scratch)
    {
        Assert.AreEqual(1, EditorBridgeApi.EditorBridge_RegisterMainThread());
        IntPtr session = EditorBridgeApi.EditorSession_Create();
        Assert.AreNotEqual(IntPtr.Zero, session);
        try
        {
            Assert.IsTrue(EditorBridgeApi.EditorSession_OpenProjectRooted(session,
                scratch, "assets/gp01/manifest.json", "assets/gp01/Main.scene"));
            int source = EditorBridgeApi.EditorSession_CreateEntity(session, "PrefabSource");
            Assert.IsTrue(source >= 0, "create source failed");
            ulong sourceId = EditorBridgeApi.EditorSession_GetEntitySceneId(session, source);
            Assert.AreNotEqual(0UL, sourceId);
            int asset = EditorBridgeApi.EditorSession_CreatePrefabFromEntity(
                session, sourceId, "InspectorPrefab");
            Assert.IsTrue(asset >= 0, "create prefab failed");
            Assert.AreEqual(0, EditorBridgeApi.EditorSession_InstantiatePrefab(
                session, asset, 0, out ulong instanceRoot));
            Assert.AreNotEqual(0UL, instanceRoot);
            Assert.AreEqual(0, EditorBridgeApi.EditorSession_DeleteEntityById(
                session, sourceId));
            Assert.IsTrue(EditorBridgeApi.EditorSession_SaveProject(session));
        }
        finally
        {
            EditorBridgeApi.EditorSession_Destroy(session);
        }
    }

    private static string ItemText(AutomationElement item)
    {
        var text = string.Join(" ", Descendants(item, ControlType.Text)
            .Select(SafeName).Where(s => s.Length > 0));
        return text.Length > 0 ? text : SafeName(item);
    }

    private static Dictionary<string, ulong> ReadSceneParents(string path)
    {
        using var doc = System.Text.Json.JsonDocument.Parse(File.ReadAllText(path));
        var result = new Dictionary<string, ulong>();
        var entities = doc.RootElement.GetProperty("entities");
        foreach (var e in entities.EnumerateArray())
        {
            string name = e.GetProperty("name").GetString() ?? "";
            ulong parent = e.TryGetProperty("parent", out var p)
                ? p.GetUInt64() : 0;
            result[name] = parent;
        }
        return result;
    }

    private static string BuildQuadObj()
    {
        // Camera-facing quad, tuned for the default EditorCamera (focus 0,
        // distance 10, yaw 45, pitch -30, FOV 60).
        double[] right = { -0.70710678, 0.0, 0.70710678 };
        double[] up = { 0.35355339, 0.86602540, 0.35355339 };
        double[] normal = { -0.61237244, 0.5, -0.61237244 };
        var sb = new System.Text.StringBuilder();
        foreach (var (u, v) in new[] { (-4.0, -4.0), (4.0, -4.0), (4.0, 4.0), (-4.0, 4.0) })
        {
            double x = right[0] * u + up[0] * v;
            double y = right[1] * u + up[1] * v;
            double z = right[2] * u + up[2] * v;
            sb.AppendLine(System.FormattableString.Invariant($"v {x} {y} {z}"));
        }
        foreach (var (u, v) in new[] { (0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0) })
            sb.AppendLine(System.FormattableString.Invariant($"vt {u} {v}"));
        for (int i = 0; i < 4; ++i)
            sb.AppendLine(System.FormattableString.Invariant(
                $"vn {normal[0]} {normal[1]} {normal[2]}"));
        sb.AppendLine("f 1/1/1 2/2/2 3/3/3");
        sb.AppendLine("f 1/1/1 3/3/3 4/4/4");
        return "mtllib b6_quad.mtl\nusemtl MatB6\n" + sb;
    }

    private static string ImportMesh(string repo, string root)
    {
        Assert.AreEqual(1, EditorBridgeApi.EditorBridge_RegisterMainThread());
        IntPtr session = EditorBridgeApi.EditorSession_Create();
        Assert.AreNotEqual(IntPtr.Zero, session);
        try
        {
            Assert.IsTrue(EditorBridgeApi.EditorSession_OpenProjectRooted(session, root,
                "assets/gp01/manifest.json", "assets/gp01/Main.scene"));
            int index = EditorBridgeApi.EditorSession_ImportAsset(session,
                "assets/models/b6_quad.obj", 2);
            Assert.IsTrue(index >= 0, "import returned " + index);
            IntPtr buf = Marshal.AllocHGlobal(128);
            try
            {
                int len = EditorBridgeApi.EditorSession_GetAssetGuid(session, index, buf, 128);
                Assert.IsTrue(len > 0, "GUID length " + len);
                return Marshal.PtrToStringUTF8(buf) ?? "";
            }
            finally { Marshal.FreeHGlobal(buf); }
        }
        finally { EditorBridgeApi.EditorSession_Destroy(session); }
    }

    private static void SelectTab(AutomationElement root, string name)
    {
        var tab = Descendants(root, ControlType.TabItem)
            .FirstOrDefault(t => SafeName(t) == name);
        if (tab is null) throw new InvalidOperationException("tab not found: " + name);
        ((SelectionItemPattern)tab.GetCurrentPattern(SelectionItemPattern.Pattern)).Select();
        Thread.Sleep(250);
    }

    private static void SelectAssetBySuffix(AutomationElement root, string suffix)
    {
        AutomationElement list = WaitFor(() => FindByAutomationId(root, "AssetList"),
            UiTimeout, "AssetList");
        WaitUntil(() => Descendants(list, ControlType.ListItem)
            .Any(i => SafeName(i).Contains(suffix, StringComparison.OrdinalIgnoreCase)),
            UiTimeout, "asset row " + suffix);
        var item = Descendants(list, ControlType.ListItem)
            .First(i => SafeName(i).Contains(suffix, StringComparison.OrdinalIgnoreCase));
        ((SelectionItemPattern)item.GetCurrentPattern(SelectionItemPattern.Pattern)).Select();
        Thread.Sleep(300);
    }

    private static void SetValue(AutomationElement root, string id, string value)
    {
        AutomationElement element = WaitFor(() => FindByAutomationId(root, id),
            UiTimeout, id);
        ((ValuePattern)element.GetCurrentPattern(ValuePattern.Pattern)).SetValue(value);
        Thread.Sleep(200);
    }

    private static string ReadValue(AutomationElement root, string id)
    {
        var element = FindByAutomationId(root, id);
        if (element is null) return "";
        try
        {
            return ((ValuePattern)element.GetCurrentPattern(ValuePattern.Pattern))
                .Current.Value;
        }
        catch
        {
            return element.Current.Name;
        }
    }

    private static void DragGizmoAxis(AutomationElement main, IntPtr hwnd,
                                      string buttonId)
    {
        ClickWhenEnabled(main, buttonId);
        string hints = WaitForGizmoHints(main);
        var parts = hints.Split(';');
        double ox = double.Parse(parts[1], System.Globalization.CultureInfo.InvariantCulture);
        double oy = double.Parse(parts[2], System.Globalization.CultureInfo.InvariantCulture);
        double ex = double.Parse(parts[3], System.Globalization.CultureInfo.InvariantCulture);
        double ey = double.Parse(parts[4], System.Globalization.CultureInfo.InvariantCulture);
        double vx = ex - ox, vy = ey - oy;
        SendMouse(hwnd, WmMouseMove, ox + vx * 0.20, oy + vy * 0.20, false);
        SendMouse(hwnd, WmLeftButtonDown, ox + vx * 0.20, oy + vy * 0.20, true);
        Thread.Sleep(80);
        SendMouse(hwnd, WmMouseMove, ox + vx * 0.80, oy + vy * 0.80, true);
        Thread.Sleep(80);
        SendMouse(hwnd, WmLeftButtonUp, ox + vx * 0.80, oy + vy * 0.80, false);
    }
    private static string WaitForGizmoHints(AutomationElement root)
    {
        string hints = "";
        WaitUntil(() =>
        {
            if (FindByAutomationId(root, "GizmoScreenHints") is null) return false;
            hints = ReadValue(root, "GizmoScreenHints");
            return hints.StartsWith("visible;", StringComparison.Ordinal);
        }, UiTimeout, "gizmo screen hints");
        return hints;
    }

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr handle);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr handle, int command);

    [DllImport("user32.dll")]
    private static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);

    private const int SwRestore = 9;
    private const byte VkMenu = 0x12;
    private const uint KeyEventKeyUp = 0x0002;
    private const uint WmMouseMove = 0x0200;
    private const uint WmLeftButtonDown = 0x0201;
    private const uint WmLeftButtonUp = 0x0202;

    [StructLayout(LayoutKind.Sequential)]
    private struct NativePoint { public int X; public int Y; }

    private static void SendMouse(IntPtr hwnd, uint message, double screenX,
                                  double screenY, bool leftDown)
    {
        var point = new NativePoint { X = (int)screenX, Y = (int)screenY };
        ScreenToClient(hwnd, ref point);
        IntPtr lParam = (IntPtr)((point.Y << 16) | (point.X & 0xFFFF));
        SendMessage(hwnd, message, (IntPtr)(leftDown ? 1 : 0), lParam);
    }

    [DllImport("user32.dll")]
    private static extern bool ScreenToClient(IntPtr hwnd, ref NativePoint point);

    [DllImport("user32.dll")]
    private static extern IntPtr SendMessage(IntPtr hwnd, uint message,
                                             IntPtr wParam, IntPtr lParam);

    private static void WaitForValue(AutomationElement root, string id, string expected)
    {        WaitUntil(() =>
        {
            var element = FindByAutomationId(root, id);
            if (element is null) return false;
            try
            {
                return ((ValuePattern)element.GetCurrentPattern(ValuePattern.Pattern))
                    .Current.Value == expected;
            }
            catch (Exception ex) when (ex is InvalidOperationException
                                        or ElementNotAvailableException or COMException)
            {
                return SafeName(element) == expected;
            }
        }, UiTimeout, id + " value == " + expected);
    }

    private static void SelectMeshAsset(AutomationElement root)
    {
        AutomationElement list = WaitFor(() => FindByAutomationId(root, "AssetList"),
            UiTimeout, "AssetList");
        WaitUntil(() =>
        {
            var items = Descendants(list, ControlType.ListItem);
            return items.Any(i => SafeName(i).Contains(".mesh", StringComparison.OrdinalIgnoreCase))
                && items.Any(i => SafeName(i).Contains(".material.json", StringComparison.OrdinalIgnoreCase));
        }, UiTimeout, "AssetBrowser Mesh + Material rows");
        var item = Descendants(list, ControlType.ListItem)
            .First(i => SafeName(i).Contains(".mesh", StringComparison.OrdinalIgnoreCase));
        ((SelectionItemPattern)item.GetCurrentPattern(SelectionItemPattern.Pattern)).Select();
        Thread.Sleep(300);
    }
    private static void SelectFirstHierarchyItem(AutomationElement root)
    {
        AutomationElement list = WaitFor(() => FindByAutomationId(root, "HierarchyList"),
            UiTimeout, "HierarchyList");
        WaitUntil(() => Descendants(list, ControlType.ListItem).Count > 0,
            UiTimeout, "hierarchy items");
        var item = Descendants(list, ControlType.ListItem)[0];
        ((SelectionItemPattern)item.GetCurrentPattern(SelectionItemPattern.Pattern)).Select();
        Thread.Sleep(300);
    }
    private static void WaitForInspectorValue(AutomationElement root, string id,
                                              string expected, TimeSpan timeout)
    {
        WaitUntil(() =>
        {
            var element = FindByAutomationId(root, id);
            return element is not null && SafeName(element) == expected;
        }, timeout, id + " == " + expected);
    }

    private static void WaitForViewportNonClear(AutomationElement root, int minimum,
                                                TimeSpan timeout)
    {
        WaitUntil(() =>
        {
            var element = FindByAutomationId(root, "ViewportFrameStats");
            if (element is null) return false;
            var match = Regex.Match(SafeName(element), @"nonClear=(\d+)");
            return match.Success && int.Parse(match.Groups[1].Value) >= minimum;
        }, timeout, "viewport production FBO nonClear pixels >= " + minimum);
    }

    private static void ClickWhenEnabled(AutomationElement root, string automationId)
    {
        AutomationElement button = WaitFor(() =>
        {
            var e = FindByAutomationId(root, automationId);
            return e is not null && e.Current.IsEnabled ? e : null;
        }, UiTimeout, automationId + " enabled");
        ((InvokePattern)button.GetCurrentPattern(InvokePattern.Pattern)).Invoke();
        Thread.Sleep(300);
    }

    private static string LocateRepoRoot()
    {
        string? env = Environment.GetEnvironmentVariable("B6UI_REPO_ROOT");
        if (!string.IsNullOrWhiteSpace(env) &&
            File.Exists(Path.Combine(env, "assets", "gp01", "manifest.json")))
            return env;
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (; dir is not null; dir = dir.Parent)
            if (File.Exists(Path.Combine(dir.FullName, "editor_avalonia", "AvaloniaEditor.csproj")))
                return dir.FullName;
        throw new InvalidOperationException("repo root not found");
    }

    private static string LocateEditorExe(string repo)
    {
        string? env = Environment.GetEnvironmentVariable("B6UI_EDITOR_EXE");
        if (!string.IsNullOrWhiteSpace(env) && File.Exists(env)) return env;
        string config = Environment.GetEnvironmentVariable("B6UI_CONFIG") ?? "Debug";
        string candidate = Path.Combine(repo, "editor_avalonia", "bin", config,
            "net9.0", "AvaloniaEditor.exe");
        if (File.Exists(candidate)) return candidate;
        throw new FileNotFoundException("AvaloniaEditor.exe not found", candidate);
    }

    private static Process Launch(string editor, string repo, string scratch)
    {
        var psi = new ProcessStartInfo(editor)
        {
            UseShellExecute = false,
            WorkingDirectory = repo,
        };
        psi.ArgumentList.Add("--m1import-host");
        psi.ArgumentList.Add(scratch);
        return Process.Start(psi)
               ?? throw new InvalidOperationException("AvaloniaEditor process did not start");
    }

    private static AutomationElement WaitForMainWindow(Process process, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (process.HasExited)
                throw new InvalidOperationException("AvaloniaEditor exited, exit=" + process.ExitCode);
            foreach (AutomationElement window in TopLevelWindows())
            {
                try
                {
                    if (window.Current.ProcessId == process.Id && window.Current.Name == HostTitle)
                        return window;
                }
                catch (Exception ex) when (ex is ElementNotAvailableException or COMException) { }
            }
            Thread.Sleep(200);
        }
        throw new TimeoutException("AvaloniaEditor main window did not reach '" + HostTitle + "'");
    }

    private static void CloseProcess(ref Process? process)
    {
        if (process is null) return;
        try
        {
            if (!process.HasExited)
            {
                process.CloseMainWindow();
                if (!process.WaitForExit(5000)) process.Kill(entireProcessTree: true);
            }
        }
        catch { try { process.Kill(entireProcessTree: true); } catch { } }
        finally { process.Dispose(); process = null; }
    }

    private static IEnumerable<AutomationElement> TopLevelWindows()
    {
        AutomationElementCollection? windows = null;
        try { windows = AutomationElement.RootElement.FindAll(TreeScope.Children, Condition.TrueCondition); }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException) { }
        if (windows is null) yield break;
        foreach (AutomationElement element in windows) yield return element;
    }

    private static IReadOnlyList<AutomationElement> Descendants(AutomationElement root, ControlType type)
    {
        var result = new List<AutomationElement>();
        try
        {
            var all = root.FindAll(TreeScope.Descendants,
                new PropertyCondition(AutomationElement.ControlTypeProperty, type));
            foreach (AutomationElement element in all) result.Add(element);
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException) { }
        return result;
    }

    private static AutomationElement? FindByAutomationId(AutomationElement root, string id)
    {
        try
        {
            return root.FindFirst(TreeScope.Descendants,
                new PropertyCondition(AutomationElement.AutomationIdProperty, id));
        }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException) { return null; }
    }

    private static string SafeName(AutomationElement element)
    {
        try { return element.Current.Name ?? ""; }
        catch (Exception ex) when (ex is ElementNotAvailableException or COMException) { return ""; }
    }

    private static void WaitUntil(Func<bool> condition, TimeSpan timeout, string what)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (condition()) return;
            Thread.Sleep(150);
        }
        throw new TimeoutException("timeout waiting for " + what);
    }

    private static T WaitFor<T>(Func<T?> factory, TimeSpan timeout, string what) where T : class
    {
        T? result = null;
        WaitUntil(() => (result = factory()) is not null, timeout, what);
        return result!;
    }

    private static void TryDelete(string path)
    {
        try { Directory.Delete(path, true); } catch { }
    }
}
