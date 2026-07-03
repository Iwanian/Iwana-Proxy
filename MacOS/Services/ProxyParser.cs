using System.Text.RegularExpressions;
using System.Web;
using IwanaProxy.Models;

namespace IwanaProxy.Services;

/// <summary>
/// Parses raw text for Telegram MTProto proxy links.
/// Direct port of the original app's ProxyParser.kt regex-based logic.
/// </summary>
public static partial class ProxyParser
{
    // Matches tg://proxy?... or https://t.me/proxy?... or https://telegram.me/proxy?...
    private static readonly Regex ProxyRegex = new(
        @"(tg://proxy\?[^\s""']+|https?://(t\.me|telegram\.me)/proxy\?[^\s""']+)",
        RegexOptions.Compiled);

    public static List<ProxyItem> Parse(string rawText)
    {
        var result = new List<ProxyItem>();
        var idCounter = 1;

        foreach (Match match in ProxyRegex.Matches(rawText))
        {
            var link = match.Value.Trim();
            try
            {
                // Query string starts after the first '?'
                var queryIndex = link.IndexOf('?');
                if (queryIndex < 0) continue;

                var query = HttpUtility.ParseQueryString(link[(queryIndex + 1)..]);
                var server = query.Get("server") ?? "";
                var portStr = query.Get("port") ?? "";
                var secret = query.Get("secret") ?? "";

                if (server.Length == 0 || portStr.Length == 0) continue;
                if (!int.TryParse(portStr, out var port)) continue;

                result.Add(new ProxyItem
                {
                    Id = idCounter++,
                    Server = server,
                    Port = port,
                    Secret = secret,
                    Link = link,
                    Ping = -1,
                    IsAlive = false,
                    IsScanned = false,
                    IsFavorite = false
                });
            }
            catch
            {
                // Skip malformed entries, same as original app's catch-and-log behavior
            }
        }

        // De-duplicate by server:port, same as the original app
        var seen = new HashSet<string>();
        var deduped = new List<ProxyItem>();
        foreach (var item in result)
        {
            var key = $"{item.Server}:{item.Port}";
            if (seen.Add(key)) deduped.Add(item);
        }

        return deduped;
    }
}
