using System.Collections.ObjectModel;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Threading;
using IwanaProxy.Models;
using IwanaProxy.Services;

namespace IwanaProxy;

public partial class MainWindow : Window
{
    private readonly ProxyRepository _repository = new();
    private readonly List<ProxyItem> _masterProxies = new();
    private readonly ObservableCollection<ProxyItem> _displayedProxies = new();
    private CancellationTokenSource? _scanCts;
    private readonly string _searchQuery = "";
    private int _aliveCount = 0;

    public MainWindow()
    {
        InitializeComponent();
        ProxyList.ItemsSource = _displayedProxies;

        // Apply theme and language initially
        ThemeManager.Apply(AppSettings.Instance.Theme);
        ApplyLanguage();

        // React to settings changes
        AppSettings.Instance.ThemeChanged += () =>
        {
            ThemeManager.Apply(AppSettings.Instance.Theme);
        };
        AppSettings.Instance.LanguageChanged += () =>
        {
            ApplyLanguage();
            RefreshStatusText();
        };

        Loaded += async (_, _) => await LoadAndScanAsync();
    }

    private void ApplyLanguage()
    {
        var loc = (string key) => Loc.Get(key);

        ScanBtnText.Text = loc("scan_proxies");
        FollowText.Text = "🙏 " + loc("follow_us");
        // Status will be refreshed via RefreshStatusText() which is called after this
    }

    private void RefreshStatusText()
    {
        // Called after language change to refresh dynamic text
        // _aliveCount is stored so we can reformat with new language
        if (_masterProxies.Count > 0)
            StatusText.Text = string.Format(Loc.Get("alive_count"), _aliveCount);
    }

    private void RefreshDisplayedList()
    {
        IEnumerable<ProxyItem> query = _masterProxies;
        query = query.Where(p => !p.IsScanned || p.IsAlive);

        if (!string.IsNullOrWhiteSpace(_searchQuery))
        {
            query = query.Where(p =>
                p.Server.Contains(_searchQuery, StringComparison.OrdinalIgnoreCase)
                || p.Port.ToString().Contains(_searchQuery));
        }

        var sorted = query
            .OrderByDescending(p => p.IsAlive)
            .ThenBy(p => p.IsAlive ? p.Ping : long.MaxValue)
            .ToList();

        _displayedProxies.Clear();
        foreach (var item in sorted)
            _displayedProxies.Add(item);
    }

    private async Task LoadAndScanAsync()
    {
        _scanCts?.Cancel();
        _scanCts = new CancellationTokenSource();
        var ct = _scanCts.Token;

        _aliveCount = 0;
        StatusText.Text = Loc.Get("loading");

        try
        {
            var fetched = await _repository.FetchProxiesAsync(ct);

            _masterProxies.Clear();
            _masterProxies.AddRange(fetched);
            RefreshDisplayedList();

            if (fetched.Count == 0)
            {
                StatusText.Text = Loc.Get("no_proxies");
                return;
            }

            StatusText.Text = string.Format(Loc.Get("testing"), fetched.Count);

            await PingService.PingAllAsync(fetched, updated =>
            {
                Dispatcher.UIThread.Invoke(() =>
                {
                    if (updated.IsAlive) _aliveCount++;
                    RefreshDisplayedList();
                    StatusText.Text = string.Format(Loc.Get("alive_count"), _aliveCount)
                                      + $" ({_aliveCount})";
                });
            }, ct);
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            StatusText.Text = Loc.Get("error_fetch");
            ErrorDialog.Show(Loc.Get("error_title"), ex.Message, this);
        }
    }

    private async void RefreshButton_Click(object? sender, RoutedEventArgs e)
    {
        await LoadAndScanAsync();
    }

    private void SettingsButton_Click(object? sender, RoutedEventArgs e)
    {
        var dlg = new SettingsWindow();
        dlg.ShowDialog(this);
    }

    private void ChannelButton_Click(object? sender, RoutedEventArgs e)
    {
        var (success, error) = TelegramLauncher.LaunchChannel();
        if (!success)
        {
            ErrorDialog.Show(Loc.Get("error_title"), Loc.Get("telegram_error") + error, this);
        }
    }

    private async void CopyButton_Click(object? sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: ProxyItem item })
        {
            try
            {
                if (Clipboard != null)
                    await Clipboard.SetTextAsync(item.Link);
            }
            catch { /* clipboard may be locked */ }
        }
    }

    private void ConnectButton_Click(object? sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: ProxyItem item }) return;

        var (success, error) = TelegramLauncher.LaunchProxy(item.Link);
        if (!success)
        {
            ErrorDialog.Show(Loc.Get("error_title"), Loc.Get("telegram_error") + error, this);
        }
    }

    protected override void OnClosed(EventArgs e)
    {
        _scanCts?.Cancel();
        base.OnClosed(e);
    }
}
