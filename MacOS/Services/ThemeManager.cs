using Avalonia;
using Avalonia.Media;
using Avalonia.Styling;

namespace IwanaProxy.Services;

public static class ThemeManager
{
    public static void Apply(AppTheme theme)
    {
        var res = Application.Current!.Resources;

        if (theme == AppTheme.Dark)
        {
            res["BgBrush"]              = new SolidColorBrush(Color.Parse("#0E1621"));
            res["CardBrush"]            = new SolidColorBrush(Color.Parse("#17212B"));
            res["Card2Brush"]           = new SolidColorBrush(Color.Parse("#1E2C3A"));
            res["AccentBrush"]          = new SolidColorBrush(Color.Parse("#2EA6FF"));
            res["AliveBrush"]           = new SolidColorBrush(Color.Parse("#3FCF6E"));
            res["DeadBrush"]            = new SolidColorBrush(Color.Parse("#E5544F"));
            res["TextPrimaryBrush"]     = new SolidColorBrush(Color.Parse("#F1F1F1"));
            res["TextSecondaryBrush"]   = new SolidColorBrush(Color.Parse("#8C97A3"));
            res["InputBgBrush"]         = new SolidColorBrush(Color.Parse("#0E1621"));
            res["DividerBrush"]         = new SolidColorBrush(Color.Parse("#1E2C3A"));
            res["IconButtonHoverBrush"] = new SolidColorBrush(Color.Parse("#1F2C38"));
            res["PingBgAliveBrush"]     = new SolidColorBrush(Color.Parse("#1C3A2A"));
            res["PingBgDeadBrush"]      = new SolidColorBrush(Color.Parse("#2A333D"));
            Application.Current.RequestedThemeVariant = ThemeVariant.Dark;
        }
        else
        {
            res["BgBrush"]              = new SolidColorBrush(Color.Parse("#F0F2F5"));
            res["CardBrush"]            = new SolidColorBrush(Color.Parse("#FFFFFF"));
            res["Card2Brush"]           = new SolidColorBrush(Color.Parse("#F7F9FC"));
            res["AccentBrush"]          = new SolidColorBrush(Color.Parse("#2196F3"));
            res["AliveBrush"]           = new SolidColorBrush(Color.Parse("#27AE60"));
            res["DeadBrush"]            = new SolidColorBrush(Color.Parse("#E74C3C"));
            res["TextPrimaryBrush"]     = new SolidColorBrush(Color.Parse("#1A1A2E"));
            res["TextSecondaryBrush"]   = new SolidColorBrush(Color.Parse("#6B7A8D"));
            res["InputBgBrush"]         = new SolidColorBrush(Color.Parse("#EBF0F5"));
            res["DividerBrush"]         = new SolidColorBrush(Color.Parse("#E0E6ED"));
            res["IconButtonHoverBrush"] = new SolidColorBrush(Color.Parse("#E8EDF2"));
            res["PingBgAliveBrush"]     = new SolidColorBrush(Color.Parse("#E8F8EF"));
            res["PingBgDeadBrush"]      = new SolidColorBrush(Color.Parse("#F0F3F6"));
            Application.Current.RequestedThemeVariant = ThemeVariant.Light;
        }
    }
}
