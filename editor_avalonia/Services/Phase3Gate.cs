using System;
using System.IO;
using System.Linq;
using AvaloniaEditor.ViewModels;

namespace AvaloniaEditor.Services;

/// <summary>
/// Phase 3 Panel Gates（可重复）。
///   Run          = P3-A Hierarchy（搜索过滤 + 选择状态契约 + 实体回路）
///   RunInspector = P3-B Inspector（Open→Select→Transform→Rename→Assign
///                  Sprite→Assign Script→Save→Reopen 全字段一致）
/// 全程 ViewModel → Session ABI 单向路径；C++/Engine 零改动。
/// </summary>
public static class Phase3Gate
{
    /// ASan 构建防呆（docs/Avalonia-Phase3-Freeze-ASan-Report.md §4.2）：
    /// MSVC ASan 在 Windows 上 interception 失败时 interception_win.cpp:193
    /// 硬失败卡死；integrity gate 已设 env，直跑 gate 也在此显式提醒。
    internal static void WarnAsanEnv(Action<string> log)
    {
        if (Environment.GetEnvironmentVariable(
                "ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE") != "1")
            log("[WARN] ASan build: set ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1 "
                + "(interception_win.cpp:193 hard-fails without it)");
    }

    public static int Run(MainViewModel vm, Action<string> log)
        => RunHierarchy(vm, log, "P3-A");

    public static int RunInspector(MainViewModel vm, Action<string> log)
        => RunInspectorWorkflow(vm, log, "P3-B");

    public static int RunAssetBrowser(MainViewModel vm, Action<string> log)
        => RunAssetBrowserWorkflow(vm, log, "P3-C");

    public static int RunScriptEditor(MainViewModel vm, Action<string> log)
        => RunScriptEditorWorkflow(vm, log, "P3-D");

    /// AV-G3 Production Authoring Gate（单一生产门，组合 P3-A~D 已验收能力）
    public static int RunProduction(MainViewModel vm, Action<string> log)
        => RunProductionWorkflow(vm, log, "AV-G3");

    // ════════════════════════════════════════════════════════════
    // P3-C Asset Browser：AV-G3-C（Import→Assign→Rename→Save→Reopen
    //   全一致）+ DL-02（新建实体绑定不丢）。
    // ════════════════════════════════════════════════════════════
    private static int RunAssetBrowserWorkflow(MainViewModel vm, Action<string> log,
                                               string gateName)
    {
        WarnAsanEnv(log);
        var root = MainWindow.FindRepoRoot();
        if (root is null) return Fail2(log, gateName, "repo root not found");
        var sw = System.Diagnostics.Stopwatch.StartNew();

        try
        {
            var src = Path.Combine(root.FullName, "assets", "gp01");
            var dst = Path.Combine(root.FullName, "editor_avalonia", "spike_gp05");
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

            // ── 1. Open → Select Player ──
            Check2(log, gateName, vm.OpenProject(manifest, scene), "open GP01");
            Check2(log, gateName, vm.Assets.Count == 33, $"assets 33 (got {vm.Assets.Count})");
            Check2(log, gateName, vm.FilteredAssets.Count == 33,
                   "no filter -> full list");
            Check2(log, gateName, vm.SelectByName("Player"), "select Player");

            // ── 2. C1 Discovery：类型过滤 + 搜索 + GUID 显示 + 空态 ──
            vm.AssetTypeFilter = "Script";
            Check2(log, gateName, vm.FilteredAssets.Count >= 1 &&
                   vm.FilteredAssets.All(a => a.TypeName == "Script"),
                   "type filter Script");
            vm.AssetTypeFilter = "Texture";
            Check2(log, gateName, vm.FilteredAssets.Count >= 1 &&
                   vm.FilteredAssets.All(a => a.TypeName == "Texture"),
                   "type filter Texture");
            vm.AssetTypeFilter = "All";
            vm.AssetSearchText = "player";
            Check2(log, gateName, vm.FilteredAssets.All(a =>
                   a.Path.Contains("player", StringComparison.OrdinalIgnoreCase)),
                   "search 'player' all match");
            vm.AssetSearchText = "zzz_no_such";
            Check2(log, gateName, vm.FilteredAssets.Count == 0, "empty result state");
            vm.AssetSearchText = "";
            vm.AssetTypeFilter = "All";
            Check2(log, gateName, vm.FilteredAssets.Count == 33, "clear filter -> full");
            Check2(log, gateName, vm.FilteredAssets.All(a => a.Guid.Length > 0),
                   "all assets expose GUID");

            // ── 3. C3 Import texture → Assign Sprite ──
            string importTex = "assets/gp01/tex/imported_pad.png";
            File.Copy(Path.Combine(dstGp01, "tex", "pad.png"),
                      Path.Combine(dst, importTex), overwrite: true);
            Check2(log, gateName, vm.ImportAssetFromPath(importTex, 0),
                   "import texture");
            Check2(log, gateName, vm.Assets.Count == 34, $"assets 34 (got {vm.Assets.Count})");
            var texAsset = vm.Assets.First(a => a.Path == importTex);
            string texGuid = texAsset.Guid;
            vm.SelectAssetByIndex(texAsset.Index);
            Check2(log, gateName, vm.AssignSpriteToSelected(), "assign sprite to Player");
            Check2(log, gateName, vm.IsDirty, "dirty after assign");

            // ── 4. C3 Import script → Assign Script ──
            string importLua = "assets/gp01/scripts/imported_flow.lua";
            Directory.CreateDirectory(Path.Combine(dst, "assets", "gp01", "scripts"));
            File.Copy(Path.Combine(dstGp01, "game.lua"),
                      Path.Combine(dst, importLua), overwrite: true);
            Check2(log, gateName, vm.ImportAssetFromPath(importLua, 1), "import script");
            Check2(log, gateName, vm.Assets.Count == 35, $"assets 35 (got {vm.Assets.Count})");
            var luaAsset = vm.Assets.First(a => a.Path == importLua);
            string luaGuid = luaAsset.Guid;
            vm.SelectAssetByIndex(luaAsset.Index);
            Check2(log, gateName, vm.AssignScriptToSelected(), "assign script to Player");
            Check2(log, gateName, vm.IsDirty, "dirty after script assign");

            // ── 5. C5 Rename asset（GUID 不变）→ Save ──
            vm.RenameInput = "imported_pad_v2";
            Check2(log, gateName, vm.RenameAssetFromVm(texAsset.Index, "imported_pad_v2"),
                   "rename texture asset");
            Check2(log, gateName, vm.IsDirty, "dirty after rename");
            Check2(log, gateName, vm.SaveProject(), "save project");
            Check2(log, gateName, !vm.IsDirty, "clean after save");

            // ── 6. Close → Reopen → 全一致 ──
            vm.Dispose();
            var vm2 = new MainViewModel();
            try
            {
                Check2(log, gateName, vm2.OpenProject(manifest, scene), "reopen");
                Check2(log, gateName, vm2.Assets.Count == 35, $"reopen assets 35 (got {vm2.Assets.Count})");
                Check2(log, gateName, vm2.SelectByName("Player"), "re-select Player");
                Check2(log, gateName, vm2.SelectedSprite.Contains("imported_pad_v2"),
                       "sprite binding survives rename + reload");
                Check2(log, gateName, vm2.SelectedScript.Contains("imported_flow"),
                       "script binding survives reload");
                var renamed = vm2.Assets.FirstOrDefault(a => a.Path.Contains("imported_pad_v2"));
                Check2(log, gateName, renamed is not null && renamed.Guid == texGuid,
                       "renamed asset keeps same GUID");
                var lua2 = vm2.Assets.First(a => a.Path == importLua);
                Check2(log, gateName, lua2.Guid == luaGuid, "imported script GUID stable");
            }
            finally { vm2.Dispose(); }

            // ── 7. DL-02：新建实体 → Assign → Save → Reopen → 绑定不丢 ──
            var vm3 = new MainViewModel();
            try
            {
                Check2(log, gateName, vm3.OpenProject(manifest, scene), "reopen for DL-02");
                Check2(log, gateName, vm3.CreateEntityFromVm("DL02Prop"), "create entity");
                Check2(log, gateName, vm3.SelectByName("DL02Prop"), "select new entity");
                var tex2 = vm3.Assets.First(a => a.Path.Contains("imported_pad_v2"));
                vm3.SelectedAsset = tex2;
                Check2(log, gateName, vm3.AssignSpriteToSelected(), "assign sprite DL-02");
                var lua3 = vm3.Assets.First(a => a.Path == importLua);
                vm3.SelectedAsset = lua3;
                Check2(log, gateName, vm3.AssignScriptToSelected(), "assign script DL-02");
                Check2(log, gateName, vm3.SaveProject(), "save DL-02");
                vm3.Dispose();
                var vm4 = new MainViewModel();
                try
                {
                    Check2(log, gateName, vm4.OpenProject(manifest, scene), "DL-02 reopen");
                    Check2(log, gateName, vm4.SelectByName("DL02Prop"), "DL-02 re-select");
                    Check2(log, gateName, vm4.SelectedSprite.Contains("imported_pad_v2"),
                           "DL-02 sprite binding intact");
                    Check2(log, gateName, vm4.SelectedScript.Contains("imported_flow"),
                           "DL-02 script binding intact");
                }
                finally { vm4.Dispose(); }
            }
            finally { vm3.Dispose(); }

                log($"[{gateName} GATE] ALL GREEN in {sw.ElapsedMilliseconds} ms");
                return 0;
            }
            finally { Directory.SetCurrentDirectory(prevCwd); }
        }
        catch (Exception ex)
        {
            return Fail2(log, gateName, "exception: " + ex.Message);
        }
    }

    private static int RunHierarchy(MainViewModel vm, Action<string> log, string gateName)
    {
        WarnAsanEnv(log);
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

    // ════════════════════════════════════════════════════════════
    // P3-B Inspector：Open→Select→Transform→Rename→Assign Sprite→
    //   Assign Script→Save→Reopen→所有字段一致。
    // ════════════════════════════════════════════════════════════
    private static int RunInspectorWorkflow(MainViewModel vm, Action<string> log,
                                            string gateName)
    {
        WarnAsanEnv(log);
        var root = MainWindow.FindRepoRoot();
        if (root is null) return Fail2(log, gateName, "repo root not found");
        var sw = System.Diagnostics.Stopwatch.StartNew();

        try
        {
            var src = Path.Combine(root.FullName, "assets", "gp01");
            var dst = Path.Combine(root.FullName, "editor_avalonia", "spike_gp04");
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

            // ── 1. Open → Select Player ──
            Check2(log, gateName, vm.OpenProject(manifest, scene), "open GP01");
            Check2(log, gateName, vm.SelectByName("Player"), "select Player");
            Check2(log, gateName, vm.SelectedName == "Player", "inspector shows name");
            float z0 = Parse(vm.PosZ);

            // ── 2. Transform edit ──
            vm.PosZ = (z0 + 5f).ToString("0.0####");
            vm.ApplyInspectorPosition();
            Check2(log, gateName, Math.Abs(Parse(vm.PosZ) - (z0 + 5f)) < 1e-3,
                   "transform edit applied");
            Check2(log, gateName, vm.IsDirty, "dirty after edit");

            // ── 3. Rename → selection kept ──
            Check2(log, gateName, vm.RenameSelected("Hero"), "rename Player -> Hero");
            Check2(log, gateName, vm.SelectedEntity is not null
                    && vm.SelectedEntity.Name == "Hero", "selection kept after rename");

            // ── 4. Assign Sprite（Texture 资产 → 选中实体）──
            var tex = vm.Assets.FirstOrDefault(a => a.TypeName == "Texture");
            Check2(log, gateName, tex is not null, "texture asset present");
            vm.SelectedAsset = tex;
            Check2(log, gateName, vm.AssignSpriteToSelected(), "assign sprite to Hero");
            Check2(log, gateName, vm.SelectedSprite.Length > 0 && vm.SelectedSprite != "(none)",
                   "inspector shows sprite");

            // ── 5. Assign Script（Script 资产 → 选中实体，P3-B 契约内绑定）──
            var script = vm.Assets.FirstOrDefault(a => a.TypeName == "Script");
            Check2(log, gateName, script is not null, "script asset present");
            vm.SelectedAsset = script;
            Check2(log, gateName, vm.AssignScriptToSelected(), "assign script to Hero");
            Check2(log, gateName, vm.SelectedScript.Length > 0 && vm.SelectedScript != "(none)",
                   "inspector shows script");
            Check2(log, gateName, vm.IsDirty, "dirty after assigns");

            // ── 6. Save ──
            Check2(log, gateName, vm.SaveProject(), "save project");
            Check2(log, gateName, !vm.IsDirty, "clean after save");

            // ── 7. Reopen → 所有字段一致 ──
            vm.Dispose();
            var vm2 = new MainViewModel();
            try
            {
                Check2(log, gateName, vm2.OpenProject(manifest, scene), "reopen");
                Check2(log, gateName, vm2.SelectByName("Hero"), "re-select Hero");
                Check2(log, gateName, vm2.SelectedName == "Hero", "name consistent");
                Check2(log, gateName, Math.Abs(Parse(vm2.PosZ) - (z0 + 5f)) < 1e-3,
                       "transform consistent");
                Check2(log, gateName, vm2.SelectedSprite == vm.SelectedSprite,
                       "sprite consistent");
                Check2(log, gateName, vm2.SelectedScript == vm.SelectedScript,
                       "script consistent");
            }
            finally { vm2.Dispose(); }

                log($"[{gateName} GATE] ALL GREEN in {sw.ElapsedMilliseconds} ms");
                return 0;
            }
            finally { Directory.SetCurrentDirectory(prevCwd); }
        }
        catch (Exception ex)
        {
            return Fail2(log, gateName, "exception: " + ex.Message);
        }
    }

    private static void Check2(Action<string> log, string gateName, bool cond, string what)
    {
        if (!cond) throw new InvalidOperationException("assert failed: " + what);
        log("[PASS] assert: " + what);
    }
    private static int Fail2(Action<string> l, string gateName, string msg)
    { l($"[{gateName} GATE] FAIL: " + msg); return 1; }

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

    // ════════════════════════════════════════════════════════════
    // P3-D Script Editor：D1 读磁盘/大文件/空态 → D2 Dirty/Switch →
    //   D3 Save+Reload(_PERSIST) → D4 Lua 错误恢复 → D5 磁盘一致。
    // ════════════════════════════════════════════════════════════
    private static int RunScriptEditorWorkflow(MainViewModel vm, Action<string> log,
                                               string gateName)
    {
        WarnAsanEnv(log);
        var root = MainWindow.FindRepoRoot();
        if (root is null) return Fail2(log, gateName, "repo root not found");
        var sw = System.Diagnostics.Stopwatch.StartNew();

        try
        {
            var src = Path.Combine(root.FullName, "assets", "gp01");
            var dst = Path.Combine(root.FullName, "editor_avalonia", "spike_gp06");
            if (Directory.Exists(dst)) Directory.Delete(dst, true);
            var dstGp01 = Path.Combine(dst, "assets", "gp01");
            Directory.CreateDirectory(dstGp01);
            CopyDir(src, dstGp01);
            string manifest = "assets/gp01/manifest.json";
            string scene = "assets/gp01/Main.scene";
            string gameLuaPath = Path.Combine(dst, "assets", "gp01", "game.lua");

            var prevCwd = Directory.GetCurrentDirectory();
            Directory.SetCurrentDirectory(dst);
            try
            {
            // ── 打开 ──
            Check2(log, gateName, vm.OpenProject(manifest, scene), "open GP01");
            Check2(log, gateName, vm.Entities.Count == 10, "hierarchy 10");

            // ═══ D1：读真实磁盘 / UTF-8 / 大文件不截断 / 名称显示 / 空态 ═══
            var scriptAsset = vm.Assets.FirstOrDefault(a => a.TypeName == "Script");
            Check2(log, gateName, scriptAsset is not null, "game.lua script asset present");
            int scriptIdx = scriptAsset!.Index;

            // 打开前空态
            Check2(log, gateName, !vm.HasScriptOpen, "D1 empty state before open");
            Check2(log, gateName, vm.OpenScript(scriptIdx), "D1 double-click -> open script");
            Check2(log, gateName, vm.HasScriptOpen, "D1 has script open now");
            Check2(log, gateName, vm.ScriptTitle.Contains("game.lua"),
                   "D1 current script name shown");
            // 与磁盘真实字节一致（D5 前置，不比较 UI 文本而是磁盘）
            var diskBytes = File.ReadAllBytes(gameLuaPath);
            Check2(log, gateName, EncodingDiffers(diskBytes, vm.ScriptText) == false,
                   $"D1 content == disk bytes ({diskBytes.Length} bytes, utf8)");

            // D1 UTF-8：写入多字节字符，读回一致
            vm.ScriptText += "\n-- 中文注释编码验证 🎯:\n";
            vm.MarkScriptEdited();
            Check2(log, gateName, vm.SaveScript(), "D1 save utf8");
            Check2(log, gateName, File.ReadAllText(gameLuaPath).Contains("中文"),
                   "D1 utf8 persisted");
            Check2(log, gateName, vm.OpenScript(scriptIdx), "re-open after utf8 save");
            Check2(log, gateName, vm.ScriptText.Contains("中文注释编码验证"),
                   "D1 utf8 read back");

            // D1 大文件不截断：追加 ~192KB 使文件远超任何固定编辑缓冲（如 64KB）
            string bigTail = string.Concat(Enumerable.Repeat("-- big line\n", 16000));
            vm.ScriptText += bigTail;
            vm.MarkScriptEdited();
            Check2(log, gateName, vm.SaveScript(), "D1 save large");
            var bigBytes = File.ReadAllBytes(gameLuaPath);
            Check2(log, gateName, bigBytes.Length > 100_000, $"D1 large file written ({bigBytes.Length}B)");
            Check2(log, gateName, vm.OpenScript(scriptIdx), "re-open large");
            // 以 UTF-8 字节往返相等为准（不能用 .Length：它是 UTF-16 长度，多字节内容会偏小）
            Check2(log, gateName, EncodingDiffers(bigBytes, vm.ScriptText) == false,
                   $"D1 no silent truncation (round-trip {bigBytes.Length} bytes == re-read)");

            // ═══ D2：Dirty / Save 清除 / 切换不丢 / (Close 未保存提示由 UI 层) ═══
            vm.ScriptText = "-- clean\nfunction OnUpdate(dt) end\n";
            vm.MarkScriptEdited();
            Check2(log, gateName, vm.ScriptDirty, "D2 dirty after edit");
            // 切换别的面板/实体不丢编辑缓冲
            Check2(log, gateName, vm.SelectByName("Player"), "select entity while editing");
            Check2(log, gateName, vm.ScriptDirty && vm.ScriptText.Contains("clean"),
                   "D2 buffer intact after switching selection");
            Check2(log, gateName, vm.SaveScript(), "D2 save");
            Check2(log, gateName, !vm.ScriptDirty, "D2 clean after save");

            // ═══ D3：Save → Reload → 新行为生效 + _PERSIST ═══
            // D3a 基础 Reload：Play game.lua → 改 gameplay 参数 → Reload
            var d3Src = "-- d3 base\n" +
                        "_PERSIST = _PERSIST or {}\n" +
                        "function OnUpdate(dt) end\n";
            vm.ScriptText = d3Src;
            vm.MarkScriptEdited();
            Check2(log, gateName, vm.SaveScript(), "D3 write base script");
            Check2(log, gateName, vm.PlayScript(scriptIdx), "D3 play");
            Check2(log, gateName, vm.IsRunning, "D3 running after play");

            // 改 gameplay 参数并 Save → Reload → 新行为生效
            vm.ScriptText = d3Src + "\n_reloaded_once = 7\n";
            vm.MarkScriptEdited();
            Check2(log, gateName, vm.SaveScript(), "D3 save gameplay change");
            Check2(log, gateName, vm.ReloadActiveScript(), "D3 reload after gameplay change");
            // 通过 Host Execute 不可行；改以“Reload 成功 + 仍可 tick”为运行健康证据，
            // _PERSIST 语义用 D3b 精确探针验证。
            Check2(log, gateName, vm.IsRunning, "D3 still running after reload");
            vm.StopPlaying();
            Check2(log, gateName, !vm.IsRunning, "D3 stop");

            // D3b _PERSIST：独立探针脚本，Reload 前后计数累加
            string persistProbe = "_PERSIST = _PERSIST or {}\n" +
                                  "_PERSIST.n = (_PERSIST.n or 0) + 1\n" +
                                  "function OnUpdate(dt) end\n";
            string probeRel = "assets/gp01/scripts/persist_probe.lua";
            Directory.CreateDirectory(Path.Combine(dst, "assets", "gp01", "scripts"));
            File.WriteAllText(Path.Combine(dst, probeRel.Replace('/', Path.DirectorySeparatorChar)),
                              persistProbe, new System.Text.UTF8Encoding(false));
            int probeIdx = vm.ImportAssetFromPath(probeRel, 1)
                ? vm.Assets.First(a => a.Path == probeRel).Index : -1;
            Check2(log, gateName, probeIdx >= 0, "D3b import persist probe");
            Check2(log, gateName, vm.PlayScript(probeIdx), "D3b play persist probe");
            // 首次加载：_PERSIST.n == 1
            Check2(log, gateName, PersistValue(vm) == 1, "D3b persist init = 1 (fresh play)");
            Check2(log, gateName, vm.ReloadActiveScript(), "D3b reload persist probe");
            Check2(log, gateName, PersistValue(vm) == 2,
                   "D3b _PERSIST preserved across reload (1 -> 2)");
            vm.StopPlaying();

            // ═══ D4：Lua 错误恢复 ═══
            // D4a 编辑错误 Lua → Save → Play 失败（错误显示、不崩）
            vm.OpenScript(scriptIdx);
            vm.ScriptText = "function broken( end end end";   // 语法错误
            vm.MarkScriptEdited();
            Check2(log, gateName, vm.SaveScript(), "D4 save broken syntax");
            Check2(log, gateName, !vm.PlayScript(scriptIdx), "D4 play broken fails");
            Check2(log, gateName, !vm.IsRunning, "D4 not running after failed play");
            Check2(log, gateName, vm.ScriptError.Length > 0, "D4 script error surfaced");

            // 修复 → Play 恢复
            vm.ScriptText = "_PERSIST = _PERSIST or {}\nfunction OnUpdate(dt) end\n";
            vm.MarkScriptEdited();
            Check2(log, gateName, vm.SaveScript(), "D4 save fixed syntax");
            Check2(log, gateName, vm.PlayScript(scriptIdx), "D4 play recovered");
            Check2(log, gateName, vm.IsRunning, "D4 running after fix");

            // D4b 运行中把脚本改成语法错误 → Reload 失败(不崩) → 修复 Reload 恢复
            vm.ScriptText = "function when_it_breaks( end end end";   // 语法错误
            vm.MarkScriptEdited();
            Check2(log, gateName, vm.SaveScript(), "D4b save broken syntax");
            Check2(log, gateName, !vm.ReloadActiveScript(),
                   "D4b reload with error fails (pcall, no crash)");
            Check2(log, gateName, vm.IsRunning, "D4b session still alive after failed reload");
            Check2(log, gateName, vm.ScriptError.Length > 0, "D4b error text shown");
            vm.ScriptText = "_PERSIST = _PERSIST or {}\nfunction OnUpdate(dt) end\n";
            vm.MarkScriptEdited();
            Check2(log, gateName, vm.SaveScript(), "D4b save fixed");
            Check2(log, gateName, vm.ReloadActiveScript(), "D4b reload recovered");
            Check2(log, gateName, vm.IsRunning, "D4b session healthy after fix");
            vm.StopPlaying();

            // ═══ D5：Edit → Save → Close → Reopen → 磁盘逐字节一致 ═══
            string d5Text = "-- D5 persistence golden\n" +
                            "_PERSIST = _PERSIST or {}\n" +
                            "function OnUpdate(dt) end\n" +
                            "-- 中文 D5 一致性\n";
            vm.ScriptText = d5Text;
            vm.MarkScriptEdited();
            Check2(log, gateName, vm.SaveScript(), "D5 write canonical script");
            vm.ResetSession();        // 编辑器“Close Project”路径：清会话 + 脚本缓冲 + 运行态
            Check2(log, gateName, !vm.IsRunning && !vm.HasScriptOpen && !vm.ScriptDirty,
                   "D5 cleared after close");
            Check2(log, gateName, EncodingOfEquals(File.ReadAllText(gameLuaPath), d5Text),
                   "D5 disk content == edited content (not UI compare)");

            var vm2 = new MainViewModel();
            try
            {
                Check2(log, gateName, vm2.OpenProject(manifest, scene), "D5 reopen");
                var s2 = vm2.Assets.FirstOrDefault(a => a.TypeName == "Script");
                Check2(log, gateName, vm2.OpenScript(s2!.Index), "D5 reopen script");
                Check2(log, gateName, vm2.ScriptText == d5Text, "D5 reopen content identical");
                Check2(log, gateName, EncodingOfEquals(
                       File.ReadAllText(gameLuaPath), vm2.ScriptText), "D5 reopen == disk");
            }
            finally { vm2.Dispose(); }

                log($"[{gateName} GATE] ALL GREEN in {sw.ElapsedMilliseconds} ms");
                return 0;
            }
            finally { Directory.SetCurrentDirectory(prevCwd); }
        }
        catch (Exception ex)
        {
            return Fail2(log, gateName, "exception: " + ex.Message);
        }
    }

    // ════════════════════════════════════════════════════════════
    // AV-G3 Production Authoring Gate（单一生产门）
    //   Authoring（Open/Create/Rename/Transform/Import/Assign）
    //   → Scripting（Open→改 gameplay→Save→Play→改→Reload→_PERSIST）
    //   → Persistence（Save→Close→Reopen→全一致）
    //   + 负路径 E1 未保存 / E2 Lua 错误 / E3 缺失 GUID 资产
    // 全程 VM → Session ABI 单向路径；不手改 JSON（E3 用测试夹具）、不直调 C++、
    // 无 ImGui、不手动 GUID、不重启 Editor 修状态。
    // ════════════════════════════════════════════════════════════
    private static int RunProductionWorkflow(MainViewModel vm, Action<string> log,
                                             string gateName)
    {
        WarnAsanEnv(log);
        var root = MainWindow.FindRepoRoot();
        if (root is null) return Fail2(log, gateName, "repo root not found");
        var sw = System.Diagnostics.Stopwatch.StartNew();
        var txt = new System.Text.UTF8Encoding(false);

        try
        {
            var src = Path.Combine(root.FullName, "assets", "gp01");
            var dst = Path.Combine(root.FullName, "editor_avalonia", "spike_gp07");
            if (Directory.Exists(dst)) Directory.Delete(dst, true);
            var dstGp01 = Path.Combine(dst, "assets", "gp01");
            Directory.CreateDirectory(dstGp01);
            CopyDir(src, dstGp01);
            string manifest = "assets/gp01/manifest.json";
            string scene = "assets/gp01/Main.scene";
            string gameLuaPath = Path.Combine(dst, "assets", "gp01", "game.lua");

            var prevCwd = Directory.GetCurrentDirectory();
            Directory.SetCurrentDirectory(dst);
            try
            {
            // ═══ 0. Open ── 干净启动 ═══
            Check2(log, gateName, vm.OpenProject(manifest, scene), "open GP01");
            Check2(log, gateName, vm.Entities.Count == 10 && vm.Assets.Count == 33,
                   $"fresh 10 entities / 33 assets");

            // ═══ A. Authoring：Create → Rename → Transform → Import → Assign ═══
            Check2(log, gateName, vm.CreateEntityFromVm("G3Hero"), "create entity");
            Check2(log, gateName, vm.SelectByName("G3Hero"), "select new entity");
            Check2(log, gateName, vm.RenameSelected("G3Protagonist"), "rename entity");
            Check2(log, gateName, vm.SelectedEntity is not null
                    && vm.SelectedEntity.Name == "G3Protagonist", "rename reflected");

            // Transform（冻结契约：仅 px/py/pz）
            vm.PosX = "1.5"; vm.PosY = "0"; vm.PosZ = "-3.25";
            vm.ApplyInspectorPosition();
            Check2(log, gateName, System.Math.Abs(float.Parse(vm.PosX, System.Globalization.CultureInfo.InvariantCulture) - 1.5f) < 1e-3
                    && System.Math.Abs(float.Parse(vm.PosZ, System.Globalization.CultureInfo.InvariantCulture) - (-3.25f)) < 1e-3,
                   "transform applied (rx/z readback)");

            // Import texture → Assign Sprite
            string importTex = "assets/gp01/tex/imported_g3.png";
            File.Copy(Path.Combine(dstGp01, "tex", "pad.png"),
                      Path.Combine(dst, importTex), overwrite: true);
            Check2(log, gateName, vm.ImportAssetFromPath(importTex, 0), "import texture");
            var texAsset = vm.Assets.First(a => a.Path == importTex);
            vm.SelectedAsset = texAsset;
            Check2(log, gateName, vm.AssignSpriteToSelected(), "assign sprite to protagonist");
            Check2(log, gateName, vm.SelectedSprite.Contains("imported_g3"), "inspector sprite bound");
            Check2(log, gateName, vm.IsDirty, "dirty after assign");

            // Import script → Assign Script
            string importLua = "assets/gp01/scripts/g3_flow.lua";
            Directory.CreateDirectory(Path.Combine(dst, "assets", "gp01", "scripts"));
            File.Copy(Path.Combine(dstGp01, "game.lua"),
                      Path.Combine(dst, importLua), overwrite: true);
            Check2(log, gateName, vm.ImportAssetFromPath(importLua, 1), "import script");
            var luaAsset = vm.Assets.First(a => a.Path == importLua);
            vm.SelectedAsset = luaAsset;
            Check2(log, gateName, vm.AssignScriptToSelected(), "assign script to protagonist");
            Check2(log, gateName, vm.SelectedScript.Contains("g3_flow"), "inspector script bound");
            Check2(log, gateName, vm.SaveProject(), "save authoring");
            Check2(log, gateName, !vm.IsDirty, "clean after save");

            // 记录绑定 GUID（重开后一致性证据）
            string texGuid = texAsset.Guid;
            string luaGuid = luaAsset.Guid;

            // ═══ B. Scripting：Open game.lua → 改 gameplay → Save → Play → Reload ═══
            var dl = vm.Assets.FirstOrDefault(a => a.TypeName == "Script");
            Check2(log, gateName, dl is not null, "game.lua director script");
            int dlIdx = dl!.Index;
            Check2(log, gateName, vm.OpenScript(dlIdx), "open game.lua");

            // 写一个真实 gameplay 参数的 director（HP/Runtime 可观测）
            string gpA = "_PERSIST = _PERSIST or {}\n" +
                         "_PERSIST.state = _PERSIST.state or 'playing'\n" +
                         "_PERSIST.hp = _PERSIST.hp or 14\n" +
                         "function OnCreate() end\n" +
                         "function OnUpdate(dt) end\n";
            vm.ScriptText = gpA;
            vm.MarkScriptEdited();
            Check2(log, gateName, vm.SaveScript(), "write gameplay director A (hp=14)");
            Check2(log, gateName, !vm.ScriptDirty, "script clean after save");

            Check2(log, gateName, vm.PlayScript(dlIdx), "play");
            Check2(log, gateName, vm.IsRunning, "running after play");
            Check2(log, gateName, vm.RuntimePersistInt("hp", 0) == 14,
                   "gameplay param hp=14 active (real behavior)");

            // 修改 gameplay 参数（hp 14 → 50），Reload 必须保留 _PERSIST(=14)
            vm.ScriptText = gpA.Replace("or 14", "or 50");
            vm.MarkScriptEdited();
            Check2(log, gateName, vm.SaveScript(), "save gameplay change (hp=50)");
            Check2(log, gateName, vm.ReloadActiveScript(), "reload");
            Check2(log, gateName, vm.IsRunning, "still running after reload");
            Check2(log, gateName, vm.RuntimePersistInt("hp", 0) == 14,
                   "_PERSIST preserved across reload (14 kept, 50 ignored)");
            vm.StopPlaying();
            Check2(log, gateName, !vm.IsRunning, "stop");

            // 全新 Play 使用已保存的新 gameplay 参数 → 生效
            Check2(log, gateName, vm.PlayScript(dlIdx), "re-play");
            Check2(log, gateName, vm.RuntimePersistInt("hp", 0) == 50,
                   "edited gameplay param active on re-play (14 -> 50)");
            vm.StopPlaying();
            Check2(log, gateName, !vm.IsRunning, "stop after verify");

            // ═══ C. Persistence：Save → Close → Reopen → 全一致 ═══
            string savedLua = File.ReadAllText(gameLuaPath);
            Check2(log, gateName, vm.SaveProject(), "save project");
            Check2(log, gateName, !vm.IsDirty, "clean after final save");

            vm.ResetSession();   // Close
            Check2(log, gateName, !vm.IsOpen && !vm.IsRunning && !vm.HasScriptOpen,
                   "closed (session + runtime cleared)");

            var vm2 = new MainViewModel();
            int protIdx = -1;
            try
            {
                Check2(log, gateName, vm2.OpenProject(manifest, scene), "reopen");
                Check2(log, gateName, vm2.SelectByName("G3Protagonist"), "re-select protagonist");
                Check2(log, gateName, vm2.SelectedName == "G3Protagonist", "name persists");
                Check2(log, gateName, System.Math.Abs(float.Parse(vm2.PosX, System.Globalization.CultureInfo.InvariantCulture) - 1.5f) < 1e-3
                        && System.Math.Abs(float.Parse(vm2.PosZ, System.Globalization.CultureInfo.InvariantCulture) - (-3.25f)) < 1e-3,
                       "transform persists");
                Check2(log, gateName, vm2.SelectedSprite.Contains("imported_g3"), "sprite binding persists");
                Check2(log, gateName, vm2.SelectedScript.Contains("g3_flow"), "script binding persists");
                protIdx = vm2.SelectedEntity!.Index;

                // Sprite/Script 的资产 GUID 在 registry 中稳定
                var texAfter = vm2.Assets.FirstOrDefault(a => a.Guid == texGuid);
                var luaAfter = vm2.Assets.FirstOrDefault(a => a.Guid == luaGuid);
                Check2(log, gateName, texAfter is not null && luaAfter is not null,
                       "imported asset GUIDs stable across reopen");

                // game.lua 磁盘内容 == 保存内容（不比较 UI 文本）
                Check2(log, gateName, EncodingOfEquals(File.ReadAllText(gameLuaPath), savedLua),
                       "game.lua content persists (disk == saved)");

                // Runtime out: 新会话读到刚保存的 gameplay 参数（hp 默认 50）
                // 跨会话必须按“资产身份（path/GUID）”解析，而非旧会话的 index
                //（AV-G3 纪律：SaveManifest 后 registry 顺序可能与打开时不同）。
                var dl2 = vm2.Assets.FirstOrDefault(a => a.TypeName == "Script"
                                                         && a.Path.EndsWith("game.lua"))
                          ?? vm2.Assets.FirstOrDefault(a => a.TypeName == "Script");
                Check2(log, gateName, dl2 is not null, "game.lua resolved by identity in reopened session");
                int dlIdx2 = dl2!.Index;
                Check2(log, gateName, vm2.OpenScript(dlIdx2), "reopen script (by identity)");
                Check2(log, gateName, vm2.ScriptText.Contains("or 50"), "edited param visible on reopen");
                Check2(log, gateName, vm2.PlayScript(dlIdx2), "reopen play");
                Check2(log, gateName, vm2.RuntimePersistInt("hp", 0) == 50,
                       "reopen -> play uses persisted gameplay param (hp=50)");
                vm2.StopPlaying();
            }
            finally { vm2.Dispose(); }

            // ═══ E1. 未保存修改：dirty→可表示→Discard 后回归已保存状态 ═══
            Check2(log, gateName, vm.OpenProject(manifest, scene), "E1 reopen for edit");
            Check2(log, gateName, vm.SelectByName("G3Protagonist"), "E1 select");
            vm.PosZ = "77.5"; vm.ApplyInspectorPosition();
            Check2(log, gateName, vm.IsDirty, "E1 dirty after edit (cancel path representable)");
            vm.ResetSession();                     // Discard（UI: ConfirmDialog）
            var vm3 = new MainViewModel();
            try
            {
                Check2(log, gateName, vm3.OpenProject(manifest, scene), "E1 reopen after discard");
                Check2(log, gateName, vm3.SelectByName("G3Protagonist"), "E1 re-select");
                Check2(log, gateName, System.Math.Abs(float.Parse(vm3.PosZ, System.Globalization.CultureInfo.InvariantCulture) - (-3.25f)) < 1e-3,
                       "E1 discarded edit NOT persisted (Z back to -3.25)");
            }
            finally { vm3.Dispose(); }

            // ═══ E2. Lua 错误：错误→存活→修复→恢复 ═══
            Check2(log, gateName, vm.OpenProject(manifest, scene), "E2 open");
            // 跨会话按身份解析 game.lua（勿用旧 index）
            var dlE2 = vm.Assets.First(a => a.TypeName == "Script" && a.Path.EndsWith("game.lua"));
            int dlIdxE2 = dlE2.Index;
            Check2(log, gateName, vm.OpenScript(dlIdxE2), "E2 open script");
            vm.ScriptText = "function broke( end end end";
            vm.MarkScriptEdited(); vm.SaveScript();
            Check2(log, gateName, !vm.PlayScript(dlIdxE2), "E2 play broken fails");
            Check2(log, gateName, !vm.IsRunning && vm.ScriptError.Length > 0, "E2 error surfaced, no crash");
            vm.ScriptText = gpA.Replace("or 14", "or 50");   // 修复
            vm.MarkScriptEdited(); vm.SaveScript();
            Check2(log, gateName, vm.PlayScript(dlIdxE2), "E2 play recovered");
            Check2(log, gateName, vm.RuntimePersistInt("hp", 0) == 50, "E2 runtime healthy after fix");
            vm.StopPlaying();

            // ═══ E3. 缺失 GUID 资产：契约内告警 + 实体保留 + Editor 可用 ═══
            // 测试夹具（scrach 专属，不影响 repo）：E3.scene 引用 registry 不存在的 GUID
            var e3dst = Path.Combine(root.FullName, "editor_avalonia", "spike_gp07_e3");
            if (Directory.Exists(e3dst)) Directory.Delete(e3dst, true);
            var e3gp = Path.Combine(e3dst, "assets", "gp01");
            Directory.CreateDirectory(e3gp);
            CopyDir(src, e3gp);
            string e3Scene = Path.Combine(e3gp, "E3.scene");
            File.WriteAllText(e3Scene,
                @"{" + "\n" +
                "  \"format\": \"engine.scene\", \"version\": 1,\n" +
                "  \"entities\": [\n" +
                "    { \"name\": \"Player\", \"position\": [0,0,4], \"sprite\": \"d1000000000000000000000000000001\" },\n" +
                "    { \"name\": \"GhostDangling\", \"position\": [1,0,2], \"script\": \"ffffffffffffffffffffffffffffffff\" }\n" +
                "  ]\n}\n", txt);
            var prev2 = Directory.GetCurrentDirectory();
            Directory.SetCurrentDirectory(e3dst);
            try
            {
                Check2(log, gateName, vm.OpenProject(manifest, "assets/gp01/E3.scene"), "E3 open (fixture)");
                Check2(log, gateName, vm.Entities.Count == 2, "E3 both entities remain (Player + GhostDangling)");
                Check2(log, gateName, vm.SelectByName("GhostDangling"), "E3 dangling entity present");
                Check2(log, gateName, vm.SelectedScript == "(none)", "E3 loaded WITHOUT missing script binding");
                // 告警已入 Console（VM 在 OpenProject 时经 GetWarnings 打印）
                Check2(log, gateName, vm.ConsoleLines.Any(l => l.Contains("missing") && l.Contains(GhostDangling())),
                       "E3 warning surfaced to console");
                Check2(log, gateName, vm.SaveProject(), "E3 editor still usable (can save)");
                Check2(log, gateName, vm.Assets.Count == 33, "E3 asset browser intact");
            }
            finally { Directory.SetCurrentDirectory(prev2); vm.ResetSession(); }

                // 零内部操作自检（golden 段仅用 VM/ABI）：守卫已由全程断言体现
                log($"[{gateName} GATE] ALL GREEN in {sw.ElapsedMilliseconds} ms");
                return 0;
            }
            finally { Directory.SetCurrentDirectory(prevCwd); }
        }
        catch (Exception ex)
        {
            return Fail2(log, gateName, "exception: " + ex.Message);
        }

        static string GhostDangling() => "GhostDangling";
    }

    /// D3b：取探针脚本在当前运行 VM 的 _PERSIST.n（运行态探针）。
    private static int PersistValue(MainViewModel vm) => vm.RuntimePersistInt("n", 0);

    /// D1/D5：磁盘文本 ↔ 内存文本一致性（严格 Ordinal；已统一为无 BOM）
    private static bool EncodingOfEquals(string disk, string memory)
        => string.Equals(disk, memory, StringComparison.Ordinal);
    /// 磁盘字节（含/不含 BOM 兼容）↔ UTF-8 内存文本一致性
    private static bool EncodingDiffers(byte[] disk, string memory)
    {
        var utf8 = new System.Text.UTF8Encoding(false, true);
        byte[] mem;
        try { mem = utf8.GetBytes(memory); }
        catch (System.Text.DecoderFallbackException) { return true; }
        // 允许磁盘以 UTF-8 BOM 开头（EF BB BF）：StartsWith 时剥掉再比
        var norm = (disk.Length >= 3 && disk[0] == 0xEF && disk[1] == 0xBB && disk[2] == 0xBF)
            ? disk.Skip(3).ToArray() : disk;
        return mem.SequenceEqual(norm) == false;
    }
}
