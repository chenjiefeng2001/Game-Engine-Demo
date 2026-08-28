using System;
using System.IO;
using AvaloniaEditor.ViewModels;

namespace AvaloniaEditor.Services;

/// <summary>
/// Phase 1 Golden Gate 1（可重复）：
///   打开 GP01 → UI(VM) 显示 10 实体/33 资产 → 选择 Player →
///   Inspector 显示 Transform → 修改 Z → Apply → Save →
///   重开会话 → 状态一致。
/// 全程走 View→VM→Session→Engine 单向路径（P1-C），不触碰引擎内部对象。
/// </summary>
public static class Phase1Gate
{
    public static int Run(MainViewModel vm, Action<string> log)
    {
        var root = MainWindow.FindRepoRoot();
        if (root is null) return Fail(log, "repo root not found");
        var sw = System.Diagnostics.Stopwatch.StartNew();

        try
        {
            // scratch 工程准备（不污染 assets/gp01 真源）
            var src = Path.Combine(root.FullName, "assets", "gp01");
            var dst = Path.Combine(root.FullName, "editor_avalonia", "spike_gp01");
            if (Directory.Exists(dst)) Directory.Delete(dst, true);
            CopyDir(src, dst);
            string manifest = Path.Combine(dst, "manifest.json");
            string scene = Path.Combine(dst, "Main.scene");

            // ── Step 1: Open Project → UI 显示 ──
            if (!vm.OpenProject(manifest, scene))
                return Fail(log, "open failed");
            Check(vm.Entities.Count == 10, $"hierarchy shows 10 entities (got {vm.Entities.Count})");
            Check(vm.Assets.Count >= 33, $"asset browser shows >=33 assets (got {vm.Assets.Count})");

            // ── Step 2: 选择 Player → Inspector 显示 Transform ──
            Check(vm.SelectByName("Player"), "select Player by name");
            Check(vm.SelectedName == "Player", $"inspector shows name (got '{vm.SelectedName}')");
            float zBefore = Parse(vm.PosZ);

            // ── Step 3: 修改 Transform → Apply（编辑）──
            vm.PosZ = (zBefore + 5f).ToString("0.0####");
            vm.ApplyInspectorPosition();
            var now = vm.PosZ;
            Check(Math.Abs(Parse(now) - (zBefore + 5f)) < 1e-3,
                  $"inspector reflects edit (z {zBefore} -> {now})");

            // ── Step 4: Save ──
            if (!vm.SaveProject()) return Fail(log, "save failed");

            // ── Step 5: 关闭 → 重开 → 一致性 ──
            vm.Dispose();
            var vm2 = new MainViewModel();
            try
            {
                if (!vm2.OpenProject(manifest, scene))
                    return Fail(log, "reopen failed");
                Check(vm2.SelectByName("Player"), "re-select Player after reopen");
                Check(Math.Abs(Parse(vm2.PosZ) - (zBefore + 5f)) < 1e-3,
                      $"persisted transform matches edit (z=={vm2.PosZ})");
                log($"[PASS] persisted Player.z == {vm2.PosZ}");
            }
            finally { vm2.Dispose(); }

            log($"[PHASE1 GATE] ALL GREEN in {sw.ElapsedMilliseconds} ms");
            return 0;
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
        static int Fail(Action<string> l, string msg) { l("[PHASE1 GATE] FAIL: " + msg); return 1; }
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
