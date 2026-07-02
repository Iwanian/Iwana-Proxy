using System.Windows;
using System.Windows.Threading;
using IwanaProxy.Services;

namespace IwanaProxy;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        DispatcherUnhandledException += App_DispatcherUnhandledException;
        AppDomain.CurrentDomain.UnhandledException += CurrentDomain_UnhandledException;
        System.Threading.Tasks.TaskScheduler.UnobservedTaskException += TaskScheduler_UnobservedTaskException;

        ThemeManager.Apply(AppSettings.Instance.Theme);

        base.OnStartup(e);
    }

    private void App_DispatcherUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        var title = Loc.Get("error_app");
        MessageBox.Show(title + ":\n\n" + e.Exception, title, MessageBoxButton.OK, MessageBoxImage.Error);
        e.Handled = true;
    }

    private void CurrentDomain_UnhandledException(object sender, UnhandledExceptionEventArgs e)
    {
        var title = Loc.Get("error_app");
        MessageBox.Show(title + " (Fatal):\n\n" + e.ExceptionObject, title, MessageBoxButton.OK, MessageBoxImage.Error);
    }

    private void TaskScheduler_UnobservedTaskException(object? sender, System.Threading.Tasks.UnobservedTaskExceptionEventArgs e)
    {
        var title = Loc.Get("error_app");
        MessageBox.Show(title + ":\n\n" + e.Exception, title, MessageBoxButton.OK, MessageBoxImage.Error);
        e.SetObserved();
    }
}
