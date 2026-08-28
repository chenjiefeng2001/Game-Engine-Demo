using System;
using System.IO;
using System.Linq;
using AvaloniaEditor.ViewModels;

namespace AvaloniaEditor.Services;

/// <summary>
/// P3-A Hierarchy Gate（可重复）：搜索过滤 + 选择状态契约 + 实体回路。
///   open(10) → search filter → select → 过滤藏选→清空恢复 →
///   create(under filter) → rename → delete → save → reload 全恢复。
/// 全程 ViewModel → Session ABI 单向路径；C++/Engine 零改动。
/// </summary>
public static class Phase3Gate
{
    public static int Run(MainViewModel vm, Action<string> log)
    {
        var root = MainWindow.FindRepoRoot();
        if (root is null) return Fail(log, "repo root not found");
        var sw = System.Diagnostics.Stopwatch.StartNew();

        try
        {
            // scratch 工程（布局与 AV-G2 一致：<dst>/assets/gp01/<内容>，CWD=<dst>）
            var src = Path.Combine(root.FullName, "assets", "gp01");
            var dst = Path.Combine(root.FullName, "editor_avalonia", "spike_gp03");
            if (Directory.Exists(dst)) Directory.Delete(dst, true);
            var dstGp01 = Path.Combine(dst, "assets", "gp01");
            Directory.CreateDirectory(dstGp01);
            CopyDir(src, dstGp01);
            string manifest = "assets/gp01/manifest.json";
            string scene = "assets/gp01/Main.scene";

            var prevCwd = Directory.GetCurrentDirectory();
            Directory.SetCurrentDirectory(dst);
            try
            {

            // ── 1. Open ──
            Check(vm.OpenProject(manifest, scene), "open GP01");
            Check(vm.Entities.Count == 10, $"hierarchy 10 entities (got {vm.Entities.Count})");
            Check(vm.FilteredEntities.Count == 10, "no filter -> full list");

            // ── 2. Search filter ──
            vm.SearchText = "Pad";
            Check(vm.FilteredEntities.Count == 4,
                  $"search 'Pad' -> 4 (got {vm.FilteredEntities.Count})");
            Check(vm.FilteredEntities.All(e => e.Name.Contains("Pad")),
                  "filtered list all match");
            Check(vm.HasFilter, "HasFilter true");

            // ── 3. Selection survives filter hide + clear ──
            Check(vm.SelectByName("Pad_N"), "select Pad_N (visible under filter)");
            vm.SearchText = "zzz";   // 把选中项藏掉
            Check(vm.FilteredEntities.Count == 0, "search 'zzz' -> empty");
            vm.SearchText = "";
            Check(vm.FilteredEntities.Count == 10, "clear filter -> full list");
            Check(vm.SelectedEntity is not null && vm.SelectedEntity.Name == "Pad_N",
                  "selection restored after filter round-trip");

            // ── 4. Create under active filter ──
            vm.SearchText = "HeroKnight";
            Check(vm.FilteredEntities.Count == 0, "no match before create");
            Check(vm.CreateEntityFromVm("HeroKnight"), "create HeroKnight under filter");
            Check(vm.Entities.Count == 11, "master list 11 after create");
            Check(vm.FilteredEntities.Count == 1 &&
                  vm.FilteredEntities[0].Name == "HeroKnight",
                  "created entity visible under matching filter");

            // ── 5. Rename（过滤随重命名实时变化）──
            vm.SearchText = "";
            Check(vm.SelectByName("HeroKnight"), "select created");
            Check(vm.RenameSelected("HeroLord"), "rename HeroKnight -> HeroLord");
            Check(vm.SelectedEntity is not null && vm.SelectedEntity.Name == "HeroLord",
                  "selection kept after rename");
            vm.SearchText = "Hero";
            Check(vm.FilteredEntities.Count == 1 && vm.FilteredEntities[0].Name == "HeroLord",
                  "filter reflects renamed entity");

            // ── 6. Delete（选中项删除 → 选择清空）──
            vm.SearchText = "";
            Check(vm.SelectByName("HeroLord"), "re-select before delete");
            Check(vm.DeleteSelected(), "delete HeroLord");
            Check(vm.Entities.Count == 10, "master list back to 10");
            Check(vm.SelectedEntity is null, "selection cleared after delete");

            // ── 7. Save → reload → 全恢复 ──
            Check(vm.SaveProject(), "save project");
            vm.Dispose();
            var vm2 = new MainViewModel();
            try
            {
                Check(vm2.OpenProject(manifest, scene), "reopen project");
                Check(vm2.Entities.Count == 10, $"reopen 10 entities (got {vm2.Entities.Count})");
                Check(vm2.FilteredEntities.Count == 10, "reopen filtered list full");
                Check(vm2.SelectByName("Pad_N"), "re-select Pad_N");
                vm2.SearchText = "Pad";
                Check(vm2.FilteredEntities.Count == 4, "reopen search filter works");
                vm2.SearchText = "";
                Check(vm2.SelectedEntity is not null && vm2.SelectedEntity.Name == "Pad_N",
                      "selection kept across reload + filter round-trip");
                Check(!vm2.IsDirty, "reopen clean");
            }
            finally { vm2.Dispose(); }

                log($"[P3-A GATE] ALL GREEN in {sw.ElapsedMilliseconds} ms");
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
        static int Fail(Action<string> l, string msg) { l("[P3-A GATE] FAIL: " + msg); return 1; }
    }

    private static void CopyDir(string src, string dst)
    {
        Directory.CreateDirectory(dst);
        foreach (var f in Directory.GetFiles(src))
            File.Copy(f, Path.Combine(dst, Path.GetFileName(f)), true);
        foreach (var d in Directory.GetDirectories(src))
            CopyDir(d, Path.Combine(dst, Path.GetFileName(d)));
    }
}
