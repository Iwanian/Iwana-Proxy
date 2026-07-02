using Avalonia;
using Avalonia.Media;

namespace IwanaProxy.Services;

public static class ThemeManager
{
    public static void Apply(AppTheme theme)
    {
        var res = Application.Current!.Resources;

        void Set(string key, string hex) => res[key] = new SolidColorBrush(Color.Parse(hex));

        if (theme == AppTheme.Dark)
        {
            Application.Current.RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
            Set("BgBrush", "#0E1621");
            Set("CardBrush", "#17212B");
            Set("Card2Brush", "#1E2C3A");
            Set("AccentBrush", "#2EA6FF");
            Set("AliveBrush", "#3FCF6E");
            Set("DeadBrush", "#E5544F");
            Set("TextPrimaryBrush", "#F1F1F1");
            Set("TextSecondaryBrush", "#8C97A3");
            Set("InputBgBrush", "#0E1621");
            Set("DividerBrush", "#1E2C3A");
            Set("IconButtonHoverBrush", "#1F2C38");
            Set("PingBgAliveBrush", "#1C3A2A");
            Set("PingBgDeadBrush", "#2A333D");
        }
        else
        {
            Application.Current.RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Light;
            Set("BgBrush", "#F0F2F5");
            Set("CardBrush", "#FFFFFF");
            Set("Card2Brush", "#F7F9FC");
            Set("AccentBrush", "#2196F3");
            Set("AliveBrush", "#27AE60");
            Set("DeadBrush", "#E74C3C");
            Set("TextPrimaryBrush", "#1A1A2E");
            Set("TextSecondaryBrush", "#6B7A8D");
            Set("InputBgBrush", "#EBF0F5");
            Set("DividerBrush", "#E0E6ED");
            Set("IconButtonHoverBrush", "#E8EDF2");
            Set("PingBgAliveBrush", "#E8F8EF");
            Set("PingBgDeadBrush", "#F0F3F6");
        }
    }
}
