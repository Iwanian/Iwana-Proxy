using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;

namespace IwanaProxy.Services;

/// <summary>
/// Minimal replacement for WPF's MessageBox — Avalonia has no built-in
/// equivalent, so this shows a small owned window with the message and an
/// OK button, styled with the app's current theme brushes.
/// </summary>
public static class ErrorDialogs
{
    public static void ShowError(string title, string message)
    {
        void Show()
        {
            var owner = (Avalonia.Application.Current?.ApplicationLifetime
                as IClassicDesktopStyleApplicationLifetime)?.MainWindow;

            var win = new Window
            {
                Title = title,
                Width = 420,
                Height = 240,
                CanResize = false,
                WindowStartupLocation = WindowStartupLocation.CenterOwner,
                Background = TryGetBrush("BgBrush"),
            };

            var text = new TextBlock
            {
                Text = message,
                TextWrapping = Avalonia.Media.TextWrapping.Wrap,
                Margin = new Avalonia.Thickness(20),
                Foreground = TryGetBrush("TextPrimaryBrush"),
            };

            var scroll = new ScrollViewer { Content = text };

            var okBtn = new Button
            {
                Content = "OK",
                Width = 100,
                HorizontalAlignment = HorizontalAlignment.Center,
                Margin = new Avalonia.Thickness(0, 0, 0, 16),
                Background = TryGetBrush("AccentBrush"),
                Foreground = Brushes.White,
            };
            okBtn.Click += (_, _) => win.Close();

            var panel = new DockPanel();
            DockPanel.SetDock(okBtn, Dock.Bottom);
            panel.Children.Add(okBtn);
            panel.Children.Add(scroll);
            win.Content = panel;

            if (owner != null && owner.IsVisible)
                win.ShowDialog(owner);
            else
                win.Show();
        }

        if (Dispatcher.UIThread.CheckAccess()) Show();
        else Dispatcher.UIThread.Invoke(Show);
    }

    private static IBrush TryGetBrush(string key)
    {
        if (Avalonia.Application.Current?.Resources.TryGetResource(key, null, out var val) == true
            && val is IBrush brush)
            return brush;
        return Brushes.Gray;
    }
}
