// HomeView.swift — Main screen: header with live proxy count, promo banner,
// disclaimer card, proxy list, and a full-width "SCAN PROXIES" button
// pinned to the bottom of the window. Mirrors the Android app's Home screen.
import SwiftUI

struct HomeView: View {
    @EnvironmentObject var app: AppState

    var body: some View {
        VStack(spacing: 0) {
            TopBar(title: app.t(.appName))

            ScrollView {
                VStack(alignment: .leading, spacing: Theme.spacingMedium) {

                    if app.bannerEnabled {
                        BannerSlider()
                    }

                    // Disclaimer card
                    Text(app.t(.disclaimer))
                        .font(.callout)
                        .foregroundStyle(.secondary)
                        .padding(Theme.spacingMedium)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(RoundedRectangle(cornerRadius: Theme.cornerRadiusLarge).fill(.background.secondary))

                    if app.usingOfflineCache {
                        Text(app.t(.offlineNotice))
                            .font(.caption)
                            .foregroundStyle(.orange)
                    }

                    if let error = app.loadError {
                        VStack(alignment: .leading, spacing: 6) {
                            Text(app.t(.errorLoadTitle)).bold().foregroundStyle(.red)
                            Text(error).font(.caption).foregroundStyle(.secondary)
                            Button(app.t(.retry)) { app.scan() }
                        }
                        .padding(Theme.spacingMedium)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(RoundedRectangle(cornerRadius: Theme.cornerRadiusCard).fill(.red.opacity(0.08)))
                    }

                    if !app.proxies.isEmpty {
                        TextField("", text: $app.searchText, prompt: Text(app.t(.proxyInputHint)))
                            .textFieldStyle(.roundedBorder)

                        ForEach(app.filteredProxies) { item in
                            ProxyCard(item: item)
                        }
                    } else if !app.isScanning {
                        VStack(spacing: 8) {
                            Text(app.t(.noProxiesFound)).foregroundStyle(.secondary)
                        }
                        .frame(maxWidth: .infinity)
                        .padding(.top, Theme.spacingLarge)
                    }
                }
                .padding(Theme.spacingLarge)
            }

            Divider()

            // Pinned full-width scan button, matching the Android app's
            // fixed bottom "⚡ SCAN PROXIES" bar.
            Button {
                app.scan()
            } label: {
                HStack {
                    if app.isScanning {
                        ProgressView().controlSize(.small)
                    } else {
                        Image(systemName: "bolt.fill")
                    }
                    Text(app.isScanning ? app.t(.scanning) : app.t(.scanProxies))
                        .bold()
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, Theme.spacingSmall)
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
            .disabled(app.isScanning)
            .padding(Theme.spacingMedium)
        }
    }
}

/// Promo banner image slider. Pulls the images and their tap-through links
/// live from https://github.com/Iwanian/Sub/tree/main/pic (see
/// BannerLoader.swift for the exact folder contract), auto-advancing every
/// 3 seconds, with dot indicators and click-through — same behavior as the
/// Android app's slider, whatever banners currently live in that folder.
private struct BannerSlider: View {
    @State private var banners: [BannerItem] = []
    @State private var index = 0
    @State private var loadFailed = false

    private let timer = Timer.publish(every: 3, on: .main, in: .common).autoconnect()

    var body: some View {
        Group {
            if banners.isEmpty {
                RoundedRectangle(cornerRadius: Theme.cornerRadiusLarge)
                    .fill(.background.secondary)
                    .frame(height: 140)
                    .overlay {
                        if !loadFailed {
                            ProgressView()
                        }
                    }
            } else {
                VStack(spacing: 8) {
                    ZStack {
                        ForEach(Array(banners.enumerated()), id: \.element.id) { i, item in
                            Button {
                                if !item.link.isEmpty {
                                    TelegramLauncher.openPlainUrl(item.link)
                                }
                            } label: {
                                Image(nsImage: item.image)
                                    .resizable()
                                    .aspectRatio(contentMode: .fill)
                            }
                            .buttonStyle(.plain)
                            .opacity(i == index ? 1 : 0)
                        }
                    }
                    .frame(height: 140)
                    .clipShape(RoundedRectangle(cornerRadius: Theme.cornerRadiusLarge))

                    if banners.count > 1 {
                        HStack(spacing: 6) {
                            ForEach(banners.indices, id: \.self) { i in
                                Circle()
                                    .fill(i == index ? Color.blue : Color.secondary.opacity(0.3))
                                    .frame(width: 6, height: 6)
                            }
                        }
                    }
                }
            }
        }
        .task {
            let loaded = await BannerLoader.loadBanners()
            banners = loaded
            loadFailed = loaded.isEmpty
        }
        .onReceive(timer) { _ in
            guard banners.count > 1 else { return }
            withAnimation { index = (index + 1) % banners.count }
        }
    }
}
