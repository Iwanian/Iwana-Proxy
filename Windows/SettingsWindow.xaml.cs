using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using IwanaProxy.Services;

namespace IwanaProxy;

public partial class SettingsWindow : Window
{
    // Named borders inside ControlTemplates — resolved after ApplyTemplate
    private Border? _darkBg;
    private Border? _lightBg;
    private TextBlock? _darkBtnTb;
    private TextBlock? _lightBtnTb;

    public SettingsWindow()
    {
        InitializeComponent();
        Loaded += OnLoaded;
        AppSettings.Instance.ThemeChanged += StyleThemeButtons;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        // ControlTemplates are applied after Loaded — safe to FindName now
        DarkBtn.ApplyTemplate();
        LightBtn.ApplyTemplate();
        _darkBg    = (Border?)   DarkBtn.Template.FindName("DarkBg",    DarkBtn);
        _lightBg   = (Border?)  LightBtn.Template.FindName("LightBg",   LightBtn);
        _darkBtnTb = (TextBlock?)DarkBtn.Template.FindName("DarkBtnText",  DarkBtn);
        _lightBtnTb= (TextBlock?)LightBtn.Template.FindName("LightBtnText",LightBtn);

        LoadCurrentSettings();
        LocalizeUi();
        StyleThemeButtons();
    }

    private void LocalizeUi()
    {
        TitleText.Text   = Loc.Get("settings_title");
        ThemeLabel.Text  = Loc.Get("theme");
        LangLabel.Text   = Loc.Get("language");
        if (_darkBtnTb  != null) _darkBtnTb.Text  = "🌙  " + Loc.Get("dark_mode");
        if (_lightBtnTb != null) _lightBtnTb.Text = "☀️  " + Loc.Get("light_mode");
        // CloseBtnText is inside a ControlTemplate too — use Tag workaround via direct lookup
        var closeTb = CloseBtn.Template.FindName("CloseBtnText", CloseBtn) as TextBlock;
        if (closeTb != null) closeTb.Text = Loc.Get("save") + " & Close";
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
        if (_darkBg == null) return; // not yet loaded

        var isDark     = AppSettings.Instance.Theme == AppTheme.Dark;
        var accent     = (SolidColorBrush)Application.Current.Resources["AccentBrush"];
        var transp     = new SolidColorBrush(Colors.Transparent);
        var white      = new SolidColorBrush(Colors.White);
        var muted      = (SolidColorBrush)Application.Current.Resources["TextSecondaryBrush"];

        _darkBg.Background  = isDark  ? accent : transp;
        _lightBg!.Background = !isDark ? accent : transp;
        if (_darkBtnTb  != null) _darkBtnTb.Foreground  = isDark  ? white : muted;
        if (_lightBtnTb != null) _lightBtnTb.Foreground = !isDark ? white : muted;
    }

    private void DarkBtn_Click(object sender, RoutedEventArgs e)
    {
        AppSettings.Instance.Theme = AppTheme.Dark;
        StyleThemeButtons();
    }

    private void LightBtn_Click(object sender, RoutedEventArgs e)
    {
        AppSettings.Instance.Theme = AppTheme.Light;
        StyleThemeButtons();
    }

    private void Lang_Click(object sender, RoutedEventArgs e)
    {
        if      (LangFa.IsChecked == true) AppSettings.Instance.Language = AppLanguage.Persian;
        else if (LangRu.IsChecked == true) AppSettings.Instance.Language = AppLanguage.Russian;
        else if (LangZh.IsChecked == true) AppSettings.Instance.Language = AppLanguage.Chinese;
        else if (LangHi.IsChecked == true) AppSettings.Instance.Language = AppLanguage.Hindi;
        else                               AppSettings.Instance.Language = AppLanguage.English;

        LocalizeUi(); // update settings window labels immediately
    }

    private void CloseBtn_Click(object sender, RoutedEventArgs e) => Close();

    protected override void OnClosed(EventArgs e)
    {
        AppSettings.Instance.ThemeChanged -= StyleThemeButtons;
        base.OnClosed(e);
    }
}
