using System;
using System.IO;
using System.Linq;
using AvaloniaEditor.ViewModels;

namespace AvaloniaEditor.Services;

/// <summary>
/// AV-G2 Editor Workflow Golden Gate（Phase 2，可重复）：
///   冷启动 → Open GP01 → select Player → edit Transform → rename →
///   assign sprite → create new → select new → edit → script edit →
///   save → close → reopen → verify all state persisted.
/// 全程走 ViewModel → Session ABI → Engine 单向路径；C++/Engine 零改动。
/// Acceptance: 所有断言 GREEN + 会话重开状态一致。
/// </summary>
public static class Phase2Gate
{
    public static int Run(MainViewModel vm, Action<string> log)
    {
        var root = MainWindow.FindRepoRoot();
        if (root is null) return Fail(log, "repo root not found");
        var sw = System.Diagnostics.Stopwatch.StartNew();

        try
        {
            // scratch 工程准备（不污染 assets/gp01 真源）。
            // 布局与 C++ 契约测试一致：<dst>/assets/gp01/<内容>，CWD=<dst>，
            // 使 manifest 内相对路径 assets/gp01/... 精确落到副本。
            var src = Path.Combine(root.FullName, "assets", "gp01");
            var dst = Path.Combine(root.FullName, "editor_avalonia", "spike_gp02");
            if (Directory.Exists(dst)) Directory.Delete(dst, true);
            var dstGp01 = Path.Combine(dst, "assets", "gp01");
            Directory.CreateDirectory(dstGp01);
            CopyDir(src, dstGp01);
            string manifest = "assets/gp01/manifest.json";
            string scene = "assets/gp01/Main.scene";

            // 与 C++ 契约测试一致：manifest 内相对路径 assets/gp01/... 按进程
            // CWD 解析（GP01 运行时语义），宿主把 CWD 定向到 scratch 工程根。
            var prevCwd = Directory.GetCurrentDirectory();
            Directory.SetCurrentDirectory(dst);
            try
            {

            // ── 1. Open（冷启动 → 数据入 UI）──
            Check(vm.OpenProject(manifest, scene), "open GP01");
            Check(vm.Entities.Count == 10, $"hierarchy 10 entities (got {vm.Entities.Count})");
            Check(vm.Assets.Count >= 33, $"assets >=33 (got {vm.Assets.Count})");
            Check(!vm.IsDirty, "clean after open");

            // ── 2. Select Player → edit Transform ──
            Check(vm.SelectByName("Player"), "select Player");
            float zBefore = Parse(vm.PosZ);
            vm.PosZ = (zBefore + 5f).ToString("0.0####");
            vm.ApplyInspectorPosition();
            Check(Math.Abs(Parse(vm.PosZ) - (zBefore + 5f)) < 1e-3, "transform edit applied");
            Check(vm.IsDirty, "dirty after edit");

            // ── 3. Rename Player → Hero（选择保持）──
            Check(vm.RenameSelected("Hero"), "rename Player -> Hero");
            Check(vm.SelectedEntity is not null && vm.SelectedEntity.Name == "Hero",
                  "selection kept + shows new name after rename");

            // ── 4. Assign sprite 到选中实体 ──
            var texAsset = vm.Assets.FirstOrDefault(a => a.TypeName == "Texture");
            Check(texAsset is not null, "texture asset present");
            vm.SelectedAsset = texAsset;
            Check(vm.AssignSpriteToSelected(), "assign sprite to Hero");
            Check(vm.SelectedSprite.Length > 0 && vm.SelectedSprite != "(none)",
                  $"inspector shows assigned sprite ('{vm.SelectedSprite}')");
            Check(vm.IsDirty, "dirty after assign");

            // ── 5. Create new entity → select → edit → rename ──
            int beforeCreate = vm.Entities.Count;
            vm.RenameInput = "NewProp";
            Check(vm.CreateEntityFromVm("NewProp"), "create entity via VM");
            Check(vm.Entities.Count == beforeCreate + 1, "hierarchy +1 after create");
            var created = vm.Entities.LastOrDefault();
            Check(created is not null && created.Name == "NewProp", "new entity visible");
            vm.SelectedEntity = created;
            vm.PosX = "2.5";
            vm.PosZ = "-3.25";
            vm.ApplyInspectorPosition();
            Check(Math.Abs(Parse(vm.PosX) - 2.5f) < 1e-3 && Math.Abs(Parse(vm.PosZ) + 3.25f) < 1e-3,
                  "new entity transform edit applied");

            // ── 6. Script edit（读 game.lua → 追加注释 → 保存）──
            var scriptAsset = vm.Assets.FirstOrDefault(a => a.TypeName == "Script");
            Check(scriptAsset is not null, "script asset present");
            Check(vm.OpenScript(scriptAsset.Index), "open game.lua in script editor");
            int origLen = vm.ScriptText.Length;
            Check(origLen > 100, $"script loaded non-empty ({origLen} chars)");
            vm.ScriptText += "\n-- audit: AV-G2 script edit via UI\n";
            vm.MarkScriptEdited();
            Check(vm.ScriptDirty, "script editor dirty after edit");
            Check(vm.SaveScript(), "script save");
            Check(!vm.ScriptDirty, "script editor clean after save");

            // ── 7. Save project → clean ──
            Check(vm.SaveProject(), "save project");
            Check(!vm.IsDirty, "clean after save");

            // ── 8. Close → reopen → verify all state ──
            vm.Dispose();
            var vm2 = new MainViewModel();
            try
            {
                Check(vm2.OpenProject(manifest, scene), "reopen project");
                Check(vm2.Entities.Count == 11, $"reopen hierarchy 11 entities (got {vm2.Entities.Count})");
                Check(vm2.SelectByName("Hero"), "re-select renamed Hero");
                Check(Math.Abs(Parse(vm2.PosZ) - (zBefore + 5f)) < 1e-3,
                      $"persisted Hero.z == {vm2.PosZ} (edit survived)");
                Check(vm2.SelectedSprite.Length > 0 && vm2.SelectedSprite != "(none)",
                      "persisted sprite binding");
                Check(vm2.SelectByName("NewProp"), "re-select created NewProp");
                Check(Math.Abs(Parse(vm2.PosX) - 2.5f) < 1e-3 && Math.Abs(Parse(vm2.PosZ) + 3.25f) < 1e-3,
                      "persisted NewProp transform");
                Check(!vm2.IsDirty, "reopen clean");

                // 脚本持久化校验（读回应含 audit 行）
                var scriptIdx2 = vm2.Assets.FirstOrDefault(a => a.TypeName == "Script")?.Index ?? -1;
                Check(scriptIdx2 >= 0, "script asset index after reopen");
                vm2.OpenScript(scriptIdx2);
                Check(vm2.ScriptText.Contains("-- audit: AV-G2 script edit via UI"),
                      "persisted script edit");
                log($"[PASS] persisted script audit line found");
            }
            finally { vm2.Dispose(); }

                log($"[PHASE2 GATE] ALL GREEN in {sw.ElapsedMilliseconds} ms");
                return 0;
            }
            finally { Directory.SetCurrentDirectory(prevCwd); }
        }
        catch (Exception ex)
        {
            return Fail(log, "exception: " + ex.Message);
        }

        void Check(bool cond, string what)
        {
            if (!cond) throw new InvalidOperationException("assert failed: " + what);
            log("[PASS] assert: " + what);
        }
        static int Fail(Action<string> l, string msg) { l("[PHASE2 GATE] FAIL: " + msg); return 1; }
    }

    private static float Parse(string s)
        => float.TryParse(s, out var v) ? v : throw new InvalidOperationException($"bad float '{s}'");

    private static void CopyDir(string src, string dst)
    {
        Directory.CreateDirectory(dst);
        foreach (var f in Directory.GetFiles(src))
            File.Copy(f, Path.Combine(dst, Path.GetFileName(f)), true);
        foreach (var d in Directory.GetDirectories(src))
            CopyDir(d, Path.Combine(dst, Path.GetFileName(d)));
    }
}
