using System.Windows;
using System.Windows.Media;

namespace IwanaProxy.Services;

public static class ThemeManager
{
    public static void Apply(AppTheme theme)
    {
        var res = Application.Current.Resources;

        if (theme == AppTheme.Dark)
        {
            res["BgBrush"]          = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#0E1621"));
            res["CardBrush"]        = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#17212B"));
            res["Card2Brush"]       = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#1E2C3A"));
            res["AccentBrush"]      = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#2EA6FF"));
            res["AliveBrush"]       = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#3FCF6E"));
            res["DeadBrush"]        = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#E5544F"));
            res["TextPrimaryBrush"] = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#F1F1F1"));
            res["TextSecondaryBrush"] = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#8C97A3"));
            res["InputBgBrush"]     = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#0E1621"));
            res["DividerBrush"]     = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#1E2C3A"));
            res["IconButtonHoverBrush"] = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#1F2C38"));
            res["PingBgAliveBrush"] = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#1C3A2A"));
            res["PingBgDeadBrush"]  = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#2A333D"));
        }
        else
        {
            res["BgBrush"]          = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#F0F2F5"));
            res["CardBrush"]        = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#FFFFFF"));
            res["Card2Brush"]       = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#F7F9FC"));
            res["AccentBrush"]      = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#2196F3"));
            res["AliveBrush"]       = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#27AE60"));
            res["DeadBrush"]        = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#E74C3C"));
            res["TextPrimaryBrush"] = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#1A1A2E"));
            res["TextSecondaryBrush"] = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#6B7A8D"));
            res["InputBgBrush"]     = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#EBF0F5"));
            res["DividerBrush"]     = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#E0E6ED"));
            res["IconButtonHoverBrush"] = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#E8EDF2"));
            res["PingBgAliveBrush"] = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#E8F8EF"));
            res["PingBgDeadBrush"]  = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#F0F3F6"));
        }
    }
}
