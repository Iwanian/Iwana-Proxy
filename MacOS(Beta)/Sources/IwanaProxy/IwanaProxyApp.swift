// IwanaProxyApp.swift — App entry point (replaces the Windows build's
// WinMain / window-class registration in main.cpp). SwiftUI's App protocol
// gives us a native macOS window, menu bar, and app lifecycle for free.
import SwiftUI

@main
struct IwanaProxyApp: App {
    @StateObject private var appState = AppState()

    var body: some Scene {
        WindowGroup(appState.t(.appName)) {
            RootView()
                .environmentObject(appState)
        }
        .windowResizability(.contentSize)
        .commands {
            CommandGroup(replacing: .newItem) {} // single-window app, no "New Window"
        }
    }
}
