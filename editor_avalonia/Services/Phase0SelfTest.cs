using System;
using System.Diagnostics;
using System.IO;

namespace AvaloniaEditor.Services;

/// <summary>
/// AV-001 Phase 0 验收门（可重复执行）：
///   Avalonia 宿主进程 → P/Invoke → C ABI → C++ EditorSession → Engine
/// 序列：拷贝 GP01 到 scratch → Open → 计数断言 → CreateEntity → Save
///       → 重开会话验证持久化 → exit code 报告。
/// </summary>
public static class Phase0SelfTest
{
    public static int Run(EditorHostService host)
    {
        var root = MainWindow.FindRepoRoot();
        if (root is null) return Fail("repo root not found");
        var sw = Stopwatch.StartNew();

        try
        {
            // ── 准备 scratch 工程（绝不污染 assets/gp01 真源）──
            var src = Path.Combine(root.FullName, "assets", "gp01");
            var dstDir = Path.Combine(root.FullName, "editor_avalonia", "spike_gp01");
            if (Directory.Exists(dstDir)) Directory.Delete(dstDir, true);
            CopyDir(src, dstDir);
            string manifest = Path.Combine(dstDir, "manifest.json");
            string scene = Path.Combine(dstDir, "Main.scene");

            // ── Round 1：打开 + 断言基线 ──
            if (!host.OpenProject(manifest, scene)) return Fail("open failed: " + host.GetLastError());
            var (entities, assets) = host.GetCounts();
            Check(entities == 10, $"objects==10 (got {entities})");
            Check(assets >= 33, $"assets>=33 (got {assets})");
            Log($"[PASS] open: objects={entities} assets={assets}");

            // ── Round 2：创建实体（自动命名）+ 事件已由宿主转发 ──
            int idx = host.CreateEntity(null);
            Check(idx == 10, $"new entity index==10 (got {idx})");
            var name = host.GetEntityName(idx);
            Check(name == "Entity", $"auto-name=='Entity' (got '{name}')");
            Log($"[PASS] create: idx={idx} name={name}");

            // ── Round 3：保存 → 重开 → 持久化验证 ──
            if (!host.SaveProject()) return Fail("save failed: " + host.GetLastError());
            Log("[PASS] save");
            host.Dispose();

            var host2 = new EditorHostService();
            try
            {
                host2.CreateSession();
                if (!host2.OpenProject(manifest, scene)) return Fail("reopen failed: " + host2.GetLastError());
                int reopened = host2.GetCounts().entities;
                Check(reopened == 11, $"persisted count==11 (got {reopened})");
                Log($"[PASS] reopen: persisted objects={reopened}");
            }
            finally { host2.Dispose(); }

            Log($"[PHASE0 GATE] ALL GREEN in {sw.ElapsedMilliseconds} ms");
            return 0;
        }
        catch (Exception ex)
        {
            return Fail("exception: " + ex.Message);
        }

        static void Check(bool cond, string what)
        {
            if (!cond) throw new InvalidOperationException("assert failed: " + what);
            Log("[PASS] assert: " + what);
        }
        static int Fail(string msg) { Log("[PHASE0 GATE] FAIL: " + msg); return 1; }
    }

    internal static void Log(string line)
    {
        Console.WriteLine(line);
        try
        {
            var path = Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..",
                "editor_avalonia", "selftest.log");
            File.AppendAllText(Path.GetFullPath(path),
                $"{DateTime.Now:HH:mm:ss.fff} {line}{Environment.NewLine}");
        }
        catch { /* 日志失败不影响判定 */ }
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
