using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace IwanaProxy.Services;

public enum AppTheme { Dark, Light }
public enum AppLanguage { English, Persian, Russian, Chinese, Hindi }

public class AppSettings : INotifyPropertyChanged
{
    public static AppSettings Instance { get; } = new();

    private AppTheme _theme = AppTheme.Dark;
    public AppTheme Theme
    {
        get => _theme;
        set { _theme = value; OnPropertyChanged(); ThemeChanged?.Invoke(); }
    }

    private AppLanguage _language = AppLanguage.English;
    public AppLanguage Language
    {
        get => _language;
        set { _language = value; OnPropertyChanged(); LanguageChanged?.Invoke(); }
    }

    public event Action? ThemeChanged;
    public event Action? LanguageChanged;
    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? n = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(n));
}
