using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;
using IwanaProxy.Services;

namespace IwanaProxy;

public partial class App : Application
{
    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        AppDomain.CurrentDomain.UnhandledException += CurrentDomain_UnhandledException;
        System.Threading.Tasks.TaskScheduler.UnobservedTaskException += TaskScheduler_UnobservedTaskException;
        Dispatcher.UIThread.UnhandledException += Dispatcher_UnhandledException;

        ThemeManager.Apply(AppSettings.Instance.Theme);

        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            desktop.MainWindow = new MainWindow();
        }

        base.OnFrameworkInitializationCompleted();
    }

    private void Dispatcher_UnhandledException(object? sender, DispatcherUnhandledExceptionEventArgs e)
    {
        ErrorDialogs.ShowError(Loc.Get("error_app"), e.Exception.ToString());
        e.Handled = true;
    }

    private void CurrentDomain_UnhandledException(object sender, UnhandledExceptionEventArgs e)
    {
        ErrorDialogs.ShowError(Loc.Get("error_app") + " (Fatal)", e.ExceptionObject?.ToString() ?? "");
    }

    private void TaskScheduler_UnobservedTaskException(object? sender, System.Threading.Tasks.UnobservedTaskExceptionEventArgs e)
    {
        ErrorDialogs.ShowError(Loc.Get("error_app"), e.Exception.ToString());
        e.SetObserved();
    }
}
