using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Layout;
using Avalonia.Media;

namespace IwanaProxy.Services;

/// <summary>
/// Minimal replacement for WPF's MessageBox.Show, since Avalonia has no
/// built-in equivalent. Shows a small modal window with a message and an OK button.
/// </summary>
public static class ErrorDialog
{
    public static void Show(string title, string message, Window? owner = null)
    {
        var okButton = new Button
        {
            Content = "OK",
            HorizontalAlignment = HorizontalAlignment.Center,
            Padding = new Avalonia.Thickness(24, 8),
            Margin = new Avalonia.Thickness(0, 16, 0, 0)
        };

        var panel = new StackPanel { Margin = new Avalonia.Thickness(20) };
        panel.Children.Add(new TextBlock
        {
            Text = message,
            TextWrapping = TextWrapping.Wrap,
            MaxWidth = 380
        });
        panel.Children.Add(okButton);

        var dialog = new Window
        {
            Title = title,
            Width = 420,
            SizeToContent = SizeToContent.Height,
            CanResize = false,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            Content = panel
        };

        okButton.Click += (_, _) => dialog.Close();

        var target = owner ?? (Application_Current_MainWindow());
        if (target != null)
            dialog.ShowDialog(target);
        else
            dialog.Show();
    }

    private static Window? Application_Current_MainWindow()
    {
        if (Avalonia.Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            return desktop.MainWindow;
        return null;
    }
}
