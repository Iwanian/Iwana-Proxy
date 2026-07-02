# Iwana Proxy — Linux Edition

This is a port of the original Windows-only WPF app to **[Avalonia UI](https://avaloniaui.net/)**,
a cross-platform .NET UI framework. It's the same app — same layout, same
colors, same logic (proxy fetching, TCP ping scanning, Telegram launching,
themes, and 5-language localization) — just running natively on Linux
(and also builds for macOS/Windows from the same code).

## Build & run on Linux

You need the [.NET 8 SDK](https://dotnet.microsoft.com/download/dotnet/8.0)
installed (this sandbox has no internet access to NuGet, so the project
could not be compiled/tested here — please build it in your own environment).

```bash
# 1. Install the .NET 8 SDK (Ubuntu/Debian example)
wget https://dot.net/v1/dotnet-install.sh
chmod +x dotnet-install.sh
./dotnet-install.sh --channel 8.0
export PATH="$HOME/.dotnet:$PATH"

# 2. Restore & run directly (development)
cd IwanaProxy
dotnet restore
dotnet run
```

## Build a standalone Linux binary

```bash
dotnet publish -c Release -r linux-x64 --self-contained true \
  -p:PublishSingleFile=true -p:EnableCompressionInSingleFile=true \
  -o publish

chmod +x publish/IwanaProxy
./publish/IwanaProxy
```

For ARM64 (e.g. Raspberry Pi), replace `linux-x64` with `linux-arm64`.

A ready-made GitHub Actions workflow (`.github/workflows/build.yml`) also
builds `linux-x64` and `linux-arm64` binaries automatically on every push —
just push this project to a GitHub repo and download the artifacts from the
Actions tab.

## Notes on the port

- **UI**: WPF XAML → Avalonia AXAML. Layout, colors, corner radii, and
  button styles were carried over 1:1; `ControlTemplate`-in-Style became
  Avalonia `ControlTheme`, and `DataTrigger`-based coloring became small
  `IValueConverter`s (`Services/Converters.cs`) since Avalonia styles the
  bindings a bit differently.
- **Clipboard / MessageBox**: WPF's `Clipboard`/`MessageBox` don't exist in
  Avalonia — replaced with `TopLevel.Clipboard` and a small custom dialog
  (`Services/ErrorDialogs.cs`).
- **Launching Telegram**: on Linux this now shells out to `xdg-open` (which
  resolves the `tg://` URI via Telegram Desktop's registered
  `x-scheme-handler/tg` MIME association) instead of Windows' ShellExecute.
  Telegram Desktop must be installed for this to work, same requirement as
  the original.
- **Business logic unchanged**: `ProxyParser`, `ProxyRepository`,
  `PingService`, `AppSettings`, and `Loc` (translations) are byte-for-byte
  the same as the original app — only the WPF-specific UI layer was
  rewritten.

## Requirements to actually connect

Like the original, this app only *finds and launches* proxies — it hands
the `tg://proxy?...` link to Telegram Desktop, which must be installed
separately (e.g. `sudo snap install telegram-desktop` or your distro's
package).
