// SettingsView.swift — Settings screen, laid out to match the Android app
// exactly: Saved Proxies / Proxy Speed Test / Support Us colored rows at
// the top, then Appearance (3-box picker), Banner Slider toggle, Language
// (radio rows with flag/heart icons), then Auto Scan Settings (toggle +
// live "Scan Interval: N seconds" label + slider).
import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var app: AppState

    var body: some View {
        VStack(spacing: 0) {
            TopBar(title: app.t(.settings))

            ScrollView {
                VStack(alignment: .leading, spacing: Theme.spacingMedium) {

                    navRow(app.t(.savedProxies), systemImage: "bookmark.fill",
                           tint: .gray, screen: .saved)
                    navRow(app.t(.proxySpeedTest), systemImage: "checkmark.circle",
                           tint: .green, screen: .speedTest)
                    navRow(app.t(.supportTitle), systemImage: "heart.fill",
                           tint: .pink, screen: .support)

                    sectionLabel(app.t(.theme))
                    HStack(spacing: 10) {
                        appearanceBox("moon.stars", app.t(.themeDark), value: "dark")
                        appearanceBox("sun.max", app.t(.themeLight), value: "light")
                        appearanceBox("circle.lefthalf.filled", app.t(.themeSystem), value: "system")
                    }

                    Toggle(app.t(.bannerSliderToggle), isOn: $app.bannerEnabled)
                        .onChange(of: app.bannerEnabled) { _, _ in app.persistSettings() }
                        .padding(Theme.spacingMedium)
                        .background(RoundedRectangle(cornerRadius: Theme.cornerRadiusCard).fill(.background.secondary))

                    sectionLabel(app.t(.language))
                    languageRow("❤️", "فارسی", value: "fa")
                    languageRow("🇺🇸", "English", value: "en")
                    languageRow("🇷🇺", "Русский", value: "ru")

                    sectionLabel(app.t(.autoScanSettingsTitle))
                    VStack(alignment: .leading, spacing: Theme.spacingMedium) {
                        Toggle(isOn: $app.autoScanEnabled) {
                            Label(app.t(.autoScanEnableToggle), systemImage: "clock")
                        }
                        .onChange(of: app.autoScanEnabled) { _, _ in
                            app.persistSettings()
                            app.updateAutoScan()
                        }

                        if app.autoScanEnabled {
                            Text(String(format: app.t(.autoScanIntervalText), app.autoScanIntervalS))
                                .font(.callout)
                            Slider(value: Binding(
                                get: { Double(app.autoScanIntervalS) },
                                set: { app.autoScanIntervalS = Int($0) }
                            ), in: 5...300, step: 5)
                            .onChange(of: app.autoScanIntervalS) { _, _ in
                                app.persistSettings()
                                app.updateAutoScan()
                            }
                        }
                    }
                    .padding(Theme.spacingMedium)
                    .background(RoundedRectangle(cornerRadius: Theme.cornerRadiusCard).fill(.background.secondary))
                }
                .padding(Theme.spacingLarge)
            }
        }
    }

    private func sectionLabel(_ text: String) -> some View {
        Text(text)
            .font(.callout.bold())
            .foregroundStyle(.blue)
    }

    private func navRow(_ title: String, systemImage: String, tint: Color, screen: Screen) -> some View {
        Button {
            app.currentScreen = screen
        } label: {
            HStack {
                Image(systemName: systemImage).foregroundStyle(tint)
                Text(title).bold().foregroundStyle(tint)
                Spacer()
                Image(systemName: "chevron.forward").foregroundStyle(tint)
            }
            .padding(Theme.spacingMedium)
            .background(RoundedRectangle(cornerRadius: Theme.cornerRadiusCard).fill(tint.opacity(0.12)))
        }
        .buttonStyle(.plain)
    }

    private func appearanceBox(_ systemImage: String, _ title: String, value: String) -> some View {
        let selected = app.themeMode == value
        return Button {
            app.themeMode = value
            app.persistSettings()
        } label: {
            VStack(spacing: 6) {
                Image(systemName: systemImage)
                Text(title).font(.caption)
            }
            .frame(maxWidth: .infinity)
            .padding(.vertical, Theme.spacingMedium)
            .background(RoundedRectangle(cornerRadius: Theme.cornerRadiusCard)
                .fill(selected ? Color.blue.opacity(0.12) : Color.clear))
            .overlay(RoundedRectangle(cornerRadius: Theme.cornerRadiusCard)
                .stroke(selected ? Color.blue : Color.secondary.opacity(0.3), lineWidth: 1))
        }
        .buttonStyle(.plain)
        .foregroundStyle(selected ? .blue : .primary)
    }

    private func languageRow(_ flag: String, _ title: String, value: String) -> some View {
        let selected = app.language == value
        return Button {
            app.language = value
            app.persistSettings()
        } label: {
            HStack {
                Text(flag)
                Text(title).foregroundStyle(selected ? .blue : .primary)
                Spacer()
                Image(systemName: selected ? "largecircle.fill.circle" : "circle")
                    .foregroundStyle(selected ? .blue : .secondary)
            }
            .padding(Theme.spacingMedium)
            .background(RoundedRectangle(cornerRadius: Theme.cornerRadiusCard)
                .fill(selected ? Color.blue.opacity(0.08) : Color.clear))
            .overlay(RoundedRectangle(cornerRadius: Theme.cornerRadiusCard)
                .stroke(selected ? Color.blue : Color.secondary.opacity(0.25), lineWidth: 1))
        }
        .buttonStyle(.plain)
    }
}
