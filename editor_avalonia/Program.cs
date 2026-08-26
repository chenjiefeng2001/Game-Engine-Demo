using Avalonia;
using System;

namespace AvaloniaEditor;

internal static class Program
{
    [STAThread]
    public static void Main(string[] args)
    {
        // 必须在首次 P/Invoke（DLL 加载）之前设置 —— GP-DX ASan 拦截教训
        Environment.SetEnvironmentVariable(
            "ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE", "1");
        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    public static AppBuilder BuildAvaloniaApp()
        => AppBuilder.Configure<App>()
            .UsePlatformDetect()
            .LogToTrace();
}
