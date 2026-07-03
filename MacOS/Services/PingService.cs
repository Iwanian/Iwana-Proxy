using System.Net.Sockets;
using IwanaProxy.Models;

namespace IwanaProxy.Services;

/// <summary>
/// Measures TCP connect latency to each proxy server/port.
/// Mirrors the original app's PingService.kt: bounded-concurrency
/// parallel scan using a semaphore, default 2.5s timeout per host.
/// </summary>
public static class PingService
{
    public static async Task<long> PingAsync(string server, int port, int timeoutMs = 2500, CancellationToken ct = default)
    {
        using var client = new TcpClient();
        var start = DateTime.UtcNow;
        try
        {
            using var cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
            cts.CancelAfter(timeoutMs);

            var connectTask = client.ConnectAsync(server, port, cts.Token).AsTask();
            await connectTask;

            return (long)(DateTime.UtcNow - start).TotalMilliseconds;
        }
        catch
        {
            return -1L;
        }
    }

    /// <summary>
    /// Pings every proxy in the list with up to 8 concurrent connections,
    /// invoking onUpdated as each individual result comes back so the UI
    /// can update live (same "streaming" behavior as the Android app).
    /// </summary>
    public static async Task PingAllAsync(
        List<ProxyItem> proxies,
        Action<ProxyItem> onUpdated,
        CancellationToken ct = default)
    {
        using var semaphore = new SemaphoreSlim(8);
        var tasks = proxies.Select(async proxy =>
        {
            await semaphore.WaitAsync(ct);
            try
            {
                var latency = await PingAsync(proxy.Server, proxy.Port, 2500, ct);
                if (latency >= 0)
                {
                    proxy.Ping = latency;
                    proxy.IsAlive = true;
                }
                else
                {
                    proxy.Ping = -1;
                    proxy.IsAlive = false;
                }
                proxy.IsScanned = true;
                onUpdated(proxy);
            }
            finally
            {
                semaphore.Release();
            }
        });

        await Task.WhenAll(tasks);
    }
}
