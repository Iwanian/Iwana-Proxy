using Avalonia.Controls;
using Avalonia.Interactivity;
using IwanaProxy.Services;

namespace IwanaProxy;

public partial class SettingsWindow : Window
{
    public SettingsWindow()
    {
        InitializeComponent();
        Loaded += OnLoaded;
        AppSettings.Instance.ThemeChanged += StyleThemeButtons;
    }

    private void OnLoaded(object? sender, RoutedEventArgs e)
    {
        LoadCurrentSettings();
        LocalizeUi();
        StyleThemeButtons();
    }

    private void LocalizeUi()
    {
        TitleText.Text    = Loc.Get("settings_title");
        ThemeLabel.Text   = Loc.Get("theme");
        LangLabel.Text    = Loc.Get("language");
        DarkBtnText.Text  = "🌙  " + Loc.Get("dark_mode");
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
            case AppLanguage.Hindi:   LangHi.IsChecked = true; break;
            default:                  LangEn.IsChecked = true; break;
        }
    }

    private void StyleThemeButtons()
    {
        var isDark = AppSettings.Instance.Theme == AppTheme.Dark;

        DarkBtn.Classes.Set("active", isDark);
        LightBtn.Classes.Set("active", !isDark);
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
        if      (LangFa.IsChecked == true) AppSettings.Instance.Language = AppLanguage.Persian;
        else if (LangRu.IsChecked == true) AppSettings.Instance.Language = AppLanguage.Russian;
        else if (LangZh.IsChecked == true) AppSettings.Instance.Language = AppLanguage.Chinese;
        else if (LangHi.IsChecked == true) AppSettings.Instance.Language = AppLanguage.Hindi;
        else                                AppSettings.Instance.Language = AppLanguage.English;

        LocalizeUi(); // update settings window labels immediately
    }

    private void CloseBtn_Click(object? sender, RoutedEventArgs e) => Close();

    protected override void OnClosed(EventArgs e)
    {
        AppSettings.Instance.ThemeChanged -= StyleThemeButtons;
        base.OnClosed(e);
    }
}
