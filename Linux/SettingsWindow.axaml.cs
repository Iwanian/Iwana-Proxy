using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;
using IwanaProxy.Services;

namespace IwanaProxy;

public partial class SettingsWindow : Window
{
    public SettingsWindow()
    {
        InitializeComponent();
        Opened += OnOpened;
        AppSettings.Instance.ThemeChanged += StyleThemeButtons;
        Closed += (_, _) => AppSettings.Instance.ThemeChanged -= StyleThemeButtons;
    }

    private void OnOpened(object? sender, EventArgs e)
    {
        LoadCurrentSettings();
        LocalizeUi();
        StyleThemeButtons();
    }

    private void LocalizeUi()
    {
        TitleText.Text = Loc.Get("settings_title");
        ThemeLabel.Text = Loc.Get("theme");
        LangLabel.Text = Loc.Get("language");
        DarkBtnText.Text = "🌙  " + Loc.Get("dark_mode");
        LightBtnText.Text = "☀️  " + Loc.Get("light_mode");
        CloseBtnText.Text = Loc.Get("save") + " & Close";
    }

    private void LoadCurrentSettings()
    {
        switch (AppSettings.Instance.Language)
        {
            case AppLanguage.Persian: LangFa.IsChecked = true; break;
            case AppLanguage.Russian: LangRu.IsChecked = true; break;
            case AppLanguage.Chinese: LangZh.IsChecked = true; break;
            case AppLanguage.Hindi: LangHi.IsChecked = true; break;
            default: LangEn.IsChecked = true; break;
        }
    }

    private void StyleThemeButtons()
    {
        var isDark = AppSettings.Instance.Theme == AppTheme.Dark;
        var accent = TryGetBrush("AccentBrush");
        var transp = Brushes.Transparent;
        var white = Brushes.White;
        var muted = TryGetBrush("TextSecondaryBrush");

        DarkBg.Background = isDark ? accent : transp;
        LightBg.Background = !isDark ? accent : transp;
        DarkBtnText.Foreground = isDark ? white : muted;
        LightBtnText.Foreground = !isDark ? white : muted;
    }

    private static IBrush TryGetBrush(string key)
    {
        if (Avalonia.Application.Current?.Resources.TryGetResource(key, null, out var val) == true
            && val is IBrush brush)
            return brush;
        return Brushes.Gray;
    }

    private void DarkBtn_Click(object? sender, RoutedEventArgs e)
    {
        AppSettings.Instance.Theme = AppTheme.Dark;
        StyleThemeButtons();
    }

    private void LightBtn_Click(object? sender, RoutedEventArgs e)
    {
        AppSettings.Instance.Theme = AppTheme.Light;
        StyleThemeButtons();
    }

    private void Lang_Click(object? sender, RoutedEventArgs e)
    {
        if (LangFa.IsChecked == true) AppSettings.Instance.Language = AppLanguage.Persian;
        else if (LangRu.IsChecked == true) AppSettings.Instance.Language = AppLanguage.Russian;
        else if (LangZh.IsChecked == true) AppSettings.Instance.Language = AppLanguage.Chinese;
        else if (LangHi.IsChecked == true) AppSettings.Instance.Language = AppLanguage.Hindi;
        else AppSettings.Instance.Language = AppLanguage.English;

        LocalizeUi();
    }

    private void CloseBtn_Click(object? sender, RoutedEventArgs e) => Close();
}
