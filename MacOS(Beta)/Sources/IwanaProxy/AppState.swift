// AppState.swift — Central app state and business-logic glue.
// This replaces the Windows build's global variables + WndProc dispatch
// (main.cpp) with a single ObservableObject that SwiftUI views bind to.
// All actual logic (fetch/parse/ping/speedtest/favorites/telegram-launch)
// lives in the ported Core/*.swift files above; this just wires them
// together and holds UI-facing state.
import Foundation
import SwiftUI
import Combine

enum Screen {
    case home, settings, saved, speedTest, support
}

@MainActor
final class AppState: ObservableObject {

    // Navigation
    @Published var currentScreen: Screen = .home

    // Settings
    @Published var language: String
    @Published var themeMode: String       // "light" | "dark" | "system"
    @Published var autoScanEnabled: Bool
    @Published var autoScanIntervalS: Int
    @Published var bannerEnabled: Bool

    // Home / proxy list
    @Published var proxies: [ProxyItem] = []
    @Published var isScanning = false
    @Published var searchText = ""
    @Published var loadError: String?
    @Published var usingOfflineCache = false

    // Favorites
    @Published var favoriteKeys: Set<String>

    // Speed test screen
    @Published var speedTestInput = ""
    @Published var speedTestFileSizeMB = ""
    @Published var speedTestRunning = false
    @Published var speedTestResult: SpeedTester.Result?
    @Published var speedTestError: String?

    private var autoScanTask: Task<Void, Never>?

    init() {
        let s = Config.load()
        language = s.language
        themeMode = s.themeMode
        autoScanEnabled = s.autoScanEnabled
        autoScanIntervalS = s.autoScanIntervalS
        bannerEnabled = s.bannerEnabled
        favoriteKeys = Storage.loadFavoriteKeys()
    }

    func t(_ key: LocKey) -> String { Loc.t(language, key) }

    var layoutDirection: LayoutDirection { language == "fa" ? .rightToLeft : .leftToRight }

    var colorScheme: ColorScheme? {
        switch themeMode {
        case "light": return .light
        case "dark": return .dark
        default: return nil // follow system
        }
    }

    var filteredProxies: [ProxyItem] {
        guard !searchText.isEmpty else { return proxies }
        let q = searchText.lowercased()
        return proxies.filter { $0.server.lowercased().contains(q) || $0.port.contains(q) }
    }

    var favoriteProxies: [ProxyItem] {
        proxies.filter { $0.isFavorite }
    }

    // MARK: - Persistence

    func persistSettings() {
        Config.save(Config.AppSettings(
            language: language, themeMode: themeMode,
            autoScanEnabled: autoScanEnabled, autoScanIntervalS: autoScanIntervalS,
            bannerEnabled: bannerEnabled
        ))
    }

    func persistFavorites() {
        Storage.saveFavoriteKeys(favoriteKeys)
    }

    // MARK: - Scan pipeline (fetch -> parse -> ping), mirrors main.cpp's scan button flow

    func scan() {
        guard !isScanning else { return }
        isScanning = true
        loadError = nil
        Task {
            let result = await ProxySource.fetch()
            guard result.success else {
                await MainActor.run {
                    self.isScanning = false
                    self.loadError = result.errorDetail
                }
                return
            }
            var parsedMutable = ProxyParser.parse(result.rawText)
            let favs = self.favoriteKeys
            for i in parsedMutable.indices { parsedMutable[i].isFavorite = favs.contains(parsedMutable[i].key) }
            let parsed = parsedMutable // now immutable, safe to capture across the Task.detached below

            await MainActor.run {
                self.proxies = parsed
                self.usingOfflineCache = (result.origin == .cache)
            }

            // Ping in the background (blocking work), then publish results.
            let pinged = await Task.detached(priority: .userInitiated) {
                PingService.pingAll(parsed)
            }.value

            await MainActor.run {
                self.proxies = pinged
                self.isScanning = false
            }
        }
    }

    func toggleFavorite(_ item: ProxyItem) {
        guard let idx = proxies.firstIndex(where: { $0.id == item.id }) else { return }
        proxies[idx].isFavorite.toggle()
        if proxies[idx].isFavorite {
            favoriteKeys.insert(proxies[idx].key)
        } else {
            favoriteKeys.remove(proxies[idx].key)
        }
        persistFavorites()
    }

    func clearFavorites() {
        for i in proxies.indices { proxies[i].isFavorite = false }
        favoriteKeys.removeAll()
        persistFavorites()
    }

    func connect(_ item: ProxyItem) {
        TelegramLauncher.openProxyLink(item.link)
    }

    func copyLink(_ item: ProxyItem) {
        TelegramLauncher.copyToClipboard(item.link)
    }

    func shareLink(_ item: ProxyItem) {
        TelegramLauncher.shareProxyLink(item.link)
    }

    // MARK: - Speed test screen

    func runSpeedTest() {
        let parsed = SpeedTester.parseInput(speedTestInput)
        guard parsed.valid else {
            speedTestError = t(.invalidProxyFormat)
            return
        }
        speedTestError = nil
        speedTestRunning = true
        speedTestResult = nil
        Task {
            let result = await Task.detached(priority: .userInitiated) {
                SpeedTester.run(parsed)
            }.value
            await MainActor.run {
                self.speedTestResult = result
                self.speedTestRunning = false
            }
        }
    }

    /// Rebuilds a tg://proxy?... link from the currently-tested input, for
    /// the Connect/Save/Copy row under the speed test results.
    private func speedTestLink() -> String {
        TelegramLauncher.toTelegramScheme(speedTestInput.trimmingCharacters(in: .whitespacesAndNewlines))
    }

    func connectSpeedTestProxy() {
        TelegramLauncher.openProxyLink(speedTestLink())
    }

    func copySpeedTestProxy() {
        TelegramLauncher.copyToClipboard(speedTestLink())
    }

    func saveSpeedTestProxy() {
        let parsed = SpeedTester.parseInput(speedTestInput)
        guard parsed.valid else { return }
        let key = "\(parsed.server):\(parsed.port)"
        favoriteKeys.insert(key)
        persistFavorites()
        if let idx = proxies.firstIndex(where: { $0.key == key }) {
            proxies[idx].isFavorite = true
        } else {
            proxies.append(ProxyItem(id: (proxies.map(\.id).max() ?? 0) + 1,
                                      server: parsed.server, port: parsed.port, secret: parsed.secret,
                                      link: speedTestLink(), isFavorite: true))
        }
    }

    var estimatedDownloadSecondsText: String? {
        guard let r = speedTestResult, let mb = Double(speedTestFileSizeMB.replacingOccurrences(of: ",", with: ".")) else {
            return nil
        }
        let secs = SpeedTester.estimateDownloadSeconds(r, fileSizeMB: mb)
        guard secs >= 0 else { return nil }
        if secs < 60 { return String(format: "%.1fs", secs) }
        return String(format: "%dm %ds", Int(secs) / 60, Int(secs) % 60)
    }

    // MARK: - Auto scan (mirrors AutoScanSettings in main.cpp)

    func updateAutoScan() {
        autoScanTask?.cancel()
        guard autoScanEnabled else { return }
        let interval = max(5, autoScanIntervalS)
        autoScanTask = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: UInt64(interval) * 1_000_000_000)
                guard !Task.isCancelled else { return }
                await MainActor.run { self?.scan() }
            }
        }
    }
}
