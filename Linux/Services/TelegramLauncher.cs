using System.Diagnostics;
using System.Runtime.InteropServices;

namespace IwanaProxy.Services;

/// <summary>
/// Opens Telegram Desktop with a given proxy link via the "tg://" URI scheme.
/// Telegram Desktop registers this scheme handler at install time on every
/// platform (Windows registry, macOS Info.plist, or a Linux .desktop file
/// with a MimeType=x-scheme-handler/tg entry), so launching it is just a
/// matter of asking the OS to open the URI.
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
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
            {
                // xdg-open resolves the tg:// scheme via the desktop's
                // registered MIME handler (Telegram Desktop registers one).
                Process.Start(new ProcessStartInfo
                {
                    FileName = "xdg-open",
                    Arguments = $"\"{uri}\"",
                    UseShellExecute = false
                });
            }
            else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = "open",
                    Arguments = $"\"{uri}\"",
                    UseShellExecute = false
                });
            }
            else
            {
                // Windows: ShellExecute handles the tg:// protocol directly.
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
            // Most common cause: Telegram Desktop isn't installed, so the OS
            // has no handler registered for the tg:// scheme.
            return (false, ex.Message);
        }
    }
}
