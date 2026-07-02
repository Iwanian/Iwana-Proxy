using System.Diagnostics;

namespace IwanaProxy.Services;

/// <summary>
/// Opens Telegram Desktop on Windows with a given proxy link.
/// Telegram Desktop registers the "tg://" protocol handler at install time,
/// so launching it is just a matter of asking Windows to open the URI
/// (same mechanism as ACTION_VIEW Intents in the original Android app).
/// </summary>
public static class TelegramLauncher
{
    public static (bool success, string? error) LaunchProxy(string link)
    {
        var tgLink = link switch
        {
            _ when link.StartsWith("https://t.me/proxy") =>
                link.Replace("https://t.me/proxy", "tg://proxy"),
            _ when link.StartsWith("https://telegram.me/proxy") =>
                link.Replace("https://telegram.me/proxy", "tg://proxy"),
            _ => link
        };

        return LaunchUri(tgLink);
    }

    public static (bool success, string? error) LaunchChannel()
    {
        return LaunchUri("tg://resolve?domain=I_w_a_n_a");
    }

    private static (bool success, string? error) LaunchUri(string uri)
    {
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = uri,
                UseShellExecute = true
            });
            return (true, null);
        }
        catch (Exception ex)
        {
            // Most common cause: Telegram Desktop isn't installed,
            // so Windows has no handler registered for the tg:// scheme.
            return (false, ex.Message);
        }
    }
}
