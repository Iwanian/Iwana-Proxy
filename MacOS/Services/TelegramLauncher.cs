using System.Diagnostics;
using System.Runtime.InteropServices;

namespace IwanaProxy.Services;

/// <summary>
/// Opens Telegram Desktop with a given proxy link.
/// On macOS, Telegram Desktop registers the "tg://" URL scheme with
/// Launch Services at install time, so we hand the URI to the system
/// via `open`, the macOS equivalent of Windows' ShellExecute.
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
            if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = "open",
                    Arguments = uri,
                    UseShellExecute = false
                });
            }
            else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = "xdg-open",
                    Arguments = uri,
                    UseShellExecute = false
                });
            }
            else
            {
                // Windows fallback, kept for cross-platform completeness
                Process.Start(new ProcessStartInfo
                {
                    FileName = uri,
                    UseShellExecute = true
                });
            }

            return (true, null);
        }
        catch (Exception ex)
        {
            // Most common cause: Telegram Desktop isn't installed,
            // so macOS has no handler registered for the tg:// scheme.
            return (false, ex.Message);
        }
    }
}
