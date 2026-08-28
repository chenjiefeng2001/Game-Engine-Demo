using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace AvaloniaEditor.Services;

/// <summary>
/// 极简确认对话框（纯代码，零依赖）。Avalonia 11.2.7 无内置 MessageBox，
/// 按迁移纪律不为此引入 NuGet 包 —— 一个 Window + 两个按钮足够 P2-E 的
/// unsaved-close 提示场景。
/// </summary>
public static class ConfirmDialog
{
    public static async Task<bool> ConfirmAsync(Window owner, string title, string message)
    {
        var dialog = new Window
        {
            Title = title,
            Width = 420,
            Height = 170,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            CanResize = false,
            SystemDecorations = SystemDecorations.Full,
        };

        var yesBtn = new Button { Content = "Yes", MinWidth = 84 };
        var noBtn = new Button { Content = "Cancel", MinWidth = 84 };
        yesBtn.Click += (_, _) => dialog.Close(true);
        noBtn.Click += (_, _) => dialog.Close(false);

        dialog.Content = new StackPanel
        {
            Margin = new Thickness(16),
            Spacing = 14,
            Children =
            {
                new TextBlock { Text = message, TextWrapping = TextWrapping.Wrap },
                new StackPanel
                {
                    Orientation = Orientation.Horizontal,
                    HorizontalAlignment = HorizontalAlignment.Right,
                    Spacing = 8,
                    Children = { noBtn, yesBtn },
                },
            },
        };

        return await dialog.ShowDialog<bool>(owner);
    }
}
