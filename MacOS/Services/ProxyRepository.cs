using System.Net.Http;
using IwanaProxy.Models;

namespace IwanaProxy.Services;

/// <summary>
/// Downloads the proxy list from the same GitHub raw source the original
/// Android app uses, then hands it off to ProxyParser.
/// </summary>
public class ProxyRepository
{
    private const string ProxyUrl =
        "https://raw.githubusercontent.com/Iwanian/Sub/main/Proxy-Channel-%2540I_w_a_n_a.txt";

    private static readonly HttpClient Client = new(new HttpClientHandler())
    {
        Timeout = TimeSpan.FromSeconds(8)
    };

    static ProxyRepository()
    {
        Client.DefaultRequestHeaders.UserAgent.ParseAdd("IwanaProxyWindowsApp");
    }

    public async Task<List<ProxyItem>> FetchProxiesAsync(CancellationToken ct = default)
    {
        var body = await Client.GetStringAsync(ProxyUrl, ct);
        return ProxyParser.Parse(body);
    }
}
