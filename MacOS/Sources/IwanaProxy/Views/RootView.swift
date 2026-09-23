// RootView.swift — Top-level screen switcher (equivalent of main.cpp's
// g_currentScreen dispatch inside Paint()/WndProc). Applies RTL layout
// automatically when the language is Farsi, and the light/dark/system theme.
import SwiftUI

struct RootView: View {
    @EnvironmentObject var app: AppState

    var body: some View {
        Group {
            switch app.currentScreen {
            case .home: HomeView()
            case .settings: SettingsView()
            case .saved: SavedView()
            case .speedTest: SpeedTestView()
            case .support: SupportView()
            }
        }
        .frame(minWidth: 420, idealWidth: 480, minHeight: 640, idealHeight: 760)
        .environment(\.layoutDirection, app.layoutDirection)
        .preferredColorScheme(app.colorScheme)
        .onAppear {
            app.updateAutoScan()
        }
    }
}
