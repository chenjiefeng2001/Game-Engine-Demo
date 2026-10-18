using Avalonia;
using System;
using AvaloniaEditor.Services;

namespace AvaloniaEditor;

internal static class Program
{
    /// <summary>
    /// HRC-1 gate 注入链必须继续使用 legacy EditorSession；其余一律为 A7 EngineHost 生产路径。
    /// backend 只在此处选定一次，运行期不可切换、无回退。
    /// </summary>
    private static readonly string[] LegacyBackendFlags =
    {
        "--hrc1", "--selftest", "--gate1", "--gate2", "--gate3a",
        "--gate3b", "--gate3c", "--gate3d", "--gate3g",
        // A3/D2：event-before-return 顺序对照必须用 legacy 后端（其事件在命令返回前同步送达），
        // 才可以与 EngineHost 的 return-before-event 形成机械可比的两种顺序。
        "--uistatelegacy",
    };

    [STAThread]
    public static void Main(string[] args)
    {
        // 必须在首次 P/Invoke（DLL 加载）之前设置 —— GP-DX ASan 拦截教训
        Environment.SetEnvironmentVariable(
            "ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE", "1");
        using var host = new EditorHostService(SelectBackend(args));
        BuildAvaloniaApp(host).StartWithClassicDesktopLifetime(args);
    }

    internal static HostBackend SelectBackend(string[] args)
    {
        foreach (var flag in LegacyBackendFlags)
            foreach (var arg in args)
                if (string.Equals(arg, flag, StringComparison.OrdinalIgnoreCase))
                    return HostBackend.LegacySession;
        return HostBackend.EngineHost;
    }

    public static AppBuilder BuildAvaloniaApp(EditorHostService host)
        => AppBuilder.Configure(() => new App(host))
            .UsePlatformDetect()
            .LogToTrace();
}
