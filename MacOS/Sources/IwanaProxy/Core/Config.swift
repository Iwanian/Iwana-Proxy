// Config.swift — Settings persistence (UserDefaults, the macOS-native
// equivalent of the Windows build's %APPDATA%\IwanaProxy\settings.ini) and
// the canonical ordered proxy-source list used by the fetch pipeline.
//
// IMPORTANT (per product requirement, unchanged from the Windows/Android app):
//   Source fetch order is strict and sequential:
//     1) Primary GitHub raw URL
//     2) Fallback GitHub-adjacent URL
//     3) If both fail -> use last cached raw text on disk (offline mode)
//   This file only declares the source list; ProxySource.swift implements the
//   actual sequential-fallback fetch logic.
import Foundation

enum Config {

    // Mirrors Android ProxyRepository.kt / Windows Config.h source order exactly.
    static let proxySourceUrls: [String] = [
        "https://raw.githubusercontent.com/Iwanian/Sub/main/Proxy-Channel-%2540I_w_a_n_a.txt",
        "https://c-mamad.ir/proxies/proxy.txt"
    ]

    struct AppSettings {
        var language: String = "en"     // "fa" | "en" | "ru"
        var themeMode: String = "system" // "light" | "dark" | "system"
        var autoScanEnabled: Bool = false
        var autoScanIntervalS: Int = 15
        var bannerEnabled: Bool = true
    }

    // ~/Library/Application Support/IwanaProxy
    static func appDataDir() -> URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        let dir = base.appendingPathComponent("IwanaProxy", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    static func cacheFilePath() -> URL {
        appDataDir().appendingPathComponent("proxies_cache.txt")
    }

    static func favoritesFilePath() -> URL {
        appDataDir().appendingPathComponent("favorites.txt")
    }

    private static let defaults = UserDefaults.standard
    private enum Keys {
        static let language = "App.Language"
        static let theme = "App.Theme"
        static let autoScanEnabled = "App.AutoScanEnabled"
        static let autoScanInterval = "App.AutoScanInterval"
        static let bannerEnabled = "App.BannerEnabled"
    }

    static func load() -> AppSettings {
        var s = AppSettings()
        s.language = defaults.string(forKey: Keys.language) ?? "en"
        s.themeMode = defaults.string(forKey: Keys.theme) ?? "system"
        s.autoScanEnabled = defaults.object(forKey: Keys.autoScanEnabled) != nil
            ? defaults.bool(forKey: Keys.autoScanEnabled) : false
        s.autoScanIntervalS = defaults.object(forKey: Keys.autoScanInterval) != nil
            ? defaults.integer(forKey: Keys.autoScanInterval) : 15
        s.bannerEnabled = defaults.object(forKey: Keys.bannerEnabled) != nil
            ? defaults.bool(forKey: Keys.bannerEnabled) : true
        return s
    }

    static func save(_ s: AppSettings) {
        defaults.set(s.language, forKey: Keys.language)
        defaults.set(s.themeMode, forKey: Keys.theme)
        defaults.set(s.autoScanEnabled, forKey: Keys.autoScanEnabled)
        defaults.set(s.autoScanIntervalS, forKey: Keys.autoScanInterval)
        defaults.set(s.bannerEnabled, forKey: Keys.bannerEnabled)
    }
}
