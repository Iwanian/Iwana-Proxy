using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
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

        ThemeManager.Apply(AppSettings.Instance.Theme);

        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            desktop.MainWindow = new MainWindow();
        }

        base.OnFrameworkInitializationCompleted();
    }

    private void CurrentDomain_UnhandledException(object sender, UnhandledExceptionEventArgs e)
    {
        var title = Loc.Get("error_app");
        ErrorDialog.Show(title, title + " (Fatal):\n\n" + e.ExceptionObject);
    }

    private void TaskScheduler_UnobservedTaskException(object? sender, System.Threading.Tasks.UnobservedTaskExceptionEventArgs e)
    {
        var title = Loc.Get("error_app");
        ErrorDialog.Show(title, title + ":\n\n" + e.Exception);
        e.SetObserved();
    }
}
