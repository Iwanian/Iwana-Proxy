using System.ComponentModel;
using System.Runtime.CompilerServices;
using IwanaProxy.Services;

namespace IwanaProxy.Models;

public class ProxyItem : INotifyPropertyChanged
{
    public int Id { get; set; }
    public string Server { get; set; } = "";
    public int Port { get; set; }
    public string Secret { get; set; } = "";
    public string Link { get; set; } = "";

    /// <summary>Formatted server:port for display</summary>
    public string ServerDisplay => $"{Server}:{Port}";

    private long _ping = -1;
    public long Ping
    {
        get => _ping;
        set { _ping = value; OnPropertyChanged(); OnPropertyChanged(nameof(PingDisplay)); OnPropertyChanged(nameof(PingMs)); }
    }

    /// <summary>Numeric ping value for the big latency display (e.g. "104")</summary>
    public string PingMs => !IsScanned ? "..." : (IsAlive ? Ping.ToString() : "—");

    private bool _isAlive;
    public bool IsAlive
    {
        get => _isAlive;
        set { _isAlive = value; OnPropertyChanged(); OnPropertyChanged(nameof(StatusDisplay)); OnPropertyChanged(nameof(PingMs)); }
    }

    private bool _isScanned;
    public bool IsScanned
    {
        get => _isScanned;
        set { _isScanned = value; OnPropertyChanged(); OnPropertyChanged(nameof(StatusDisplay)); OnPropertyChanged(nameof(PingMs)); }
    }

    private bool _isFavorite;
    public bool IsFavorite
    {
        get => _isFavorite;
        set { _isFavorite = value; OnPropertyChanged(); OnPropertyChanged(nameof(FavoriteDisplay)); }
    }

    public string PingDisplay => !IsScanned ? "..." : (IsAlive ? $"{Ping} ms" : "—");

    public string StatusDisplay
    {
        get
        {
            if (!IsScanned) return Loc.Get("testing_status");
            return IsAlive ? Loc.Get("online") : Loc.Get("offline");
        }
    }

    public string FavoriteDisplay => IsFavorite ? "★" : "☆";

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
