using System;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Threading;

namespace AvaloniaEditor.Services;

/// <summary>
/// managed gate 共享的 UI 驱动/断言工具（Stage A/A2 新增）。
/// 刻意不与 <see cref="ProductionPathGate"/> 合并：A1 证据已冻结，不回改其实现。
/// </summary>
internal static class GateProbe
{
    internal static T Control<T>(MainWindow window, string name) where T : Control
        => window.FindControl<T>(name)
           ?? throw new InvalidOperationException($"control '{name}' not found");

    /// <summary>通过真实 routed Click 事件驱动 XAML 绑定的 handler（不新增旁路入口）。</summary>
    internal static void Click(Button button)
        => button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent, button));

    internal static void Click(MenuItem item)
        => item.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent, item));

    internal static void Check(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }

    internal static bool Throws<T>(Action action) where T : Exception
    {
        try
        {
            action();
            return false;
        }
        catch (T)
        {
            return true;
        }
        catch (Exception ex)
        {
            throw new InvalidOperationException(
                $"expected {typeof(T).Name} but got {ex.GetType().Name}: {ex.Message}");
        }
    }

    internal static async Task<bool> WaitUntilAsync(Func<bool> predicate, int timeoutMs)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            if (predicate()) return true;
            await Task.Delay(20);
        }
        return predicate();
    }

    /// <summary>等待 UI 驱动的唯一 pump 至少再推进 n 帧。</summary>
    internal static async Task<bool> WaitFramesAsync(EditorHostService host, int frames, int timeoutMs)
    {
        var start = host.RuntimeCounts.frames;
        return await WaitUntilAsync(() => host.RuntimeCounts.frames >= start + (ulong)frames, timeoutMs);
    }

    /// <summary>
    /// UI 线程未处理异常探针（A2/F2-05 的核心证据）：任何逃出 XAML 反射派发的异常都会
    /// 被计入。这里**不**标记 Handled —— 真实逃逸仍然会让进程按 Avalonia 默认行为失败。
    /// </summary>
    internal static void InstallUnhandledProbe(Action<string> onUnhandled)
        => Dispatcher.UIThread.UnhandledException += (_, e) => onUnhandled(e.Exception.GetType().Name);

    internal static DirectoryInfo? FindRepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (; dir is not null; dir = dir.Parent)
            if (File.Exists(Path.Combine(dir.FullName, "assets", "gp01", "manifest.json")))
                return dir;
        return null;
    }
}
