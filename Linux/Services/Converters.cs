using System.Globalization;
using Avalonia;
using Avalonia.Data.Converters;
using Avalonia.Media;

namespace IwanaProxy.Services;

/// <summary>
/// Converts ProxyItem.IsAlive into the badge/latency brushes that WPF did
/// with DataTrigger. Reads current theme brushes from Application.Resources
/// so it stays correct across theme switches.
/// </summary>
public abstract class BoolThemeBrushConverter : IValueConverter
{
    protected abstract string TrueKey { get; }
    protected abstract string FalseKey { get; }

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        var isAlive = value is true;
        var key = isAlive ? TrueKey : FalseKey;
        if (Application.Current?.Resources.TryGetResource(key, null, out var brush) == true)
            return brush;
        return Brushes.Gray;
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => throw new NotSupportedException();
}

public class BoolToBadgeBgConverter : BoolThemeBrushConverter
{
    public static readonly BoolToBadgeBgConverter Instance = new();
    protected override string TrueKey => "PingBgAliveBrush";
    protected override string FalseKey => "PingBgDeadBrush";
}

public class BoolToBadgeFgConverter : BoolThemeBrushConverter
{
    public static readonly BoolToBadgeFgConverter Instance = new();
    protected override string TrueKey => "AliveBrush";
    protected override string FalseKey => "TextSecondaryBrush";
}

public class BoolToPingFgConverter : BoolThemeBrushConverter
{
    public static readonly BoolToPingFgConverter Instance = new();
    protected override string TrueKey => "AccentBrush";
    protected override string FalseKey => "TextPrimaryBrush";
}
