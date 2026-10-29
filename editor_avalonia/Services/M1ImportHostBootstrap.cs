using System;
using System.IO;
using AvaloniaEditor.ViewModels;

namespace AvaloniaEditor.Services;

/// <summary>
/// RF-4 后续（#141）的 host harness：只打开一个 scratch 工程并把窗口标题设为可被 UIA 识别的值。
/// 不提供任何导入旁路；导入必须仍由真实 Import 按钮 + Windows 文件选择器驱动。
/// </summary>
public static class M1ImportHostBootstrap
{
    public const string Flag = "--m1import-host";
    public const string ReadyTitle = "Avalonia Editor — M1IMPORT-HOST";

    public static string? FindRoot(string[] args)
    {
        for (int i = 0; i < args.Length - 1; i++)
        {
            if (string.Equals(args[i], Flag, StringComparison.OrdinalIgnoreCase))
                return args[i + 1];
        }
        return null;
    }

    /// <returns>0 = ready (keep running); non-zero = startup failure.</returns>
    public static int Run(MainWindow window, MainViewModel vm, Action<string> log, string? root)
    {
        if (string.IsNullOrWhiteSpace(root) || !Directory.Exists(root))
        {
            log($"[M1IMPORT-HOST] invalid scratch root: {root ?? "<missing>"}");
            return 2;
        }

        if (!vm.OpenProjectRooted(root, "assets/gp01/manifest.json", "assets/gp01/Main.scene"))
        {
            log($"[M1IMPORT-HOST] open failed: {root}");
            return 3;
        }

        window.Title = ReadyTitle;
        vm.LogToConsole($"[M1IMPORT-HOST] ready: {root}");
        log($"[M1IMPORT-HOST] ready: {root}");
        return 0;
    }
}
