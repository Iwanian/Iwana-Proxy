// Chrome.swift — Top bar. Mirrors the Android app's app bar exactly: on
// Home it's "Iwana Proxy" + a green-dot "CONNECTED PROXIES (N)" subtitle
// with a settings gear top-right; on every other screen it's a centered
// title with a back chevron top-left. There is no bottom navigation bar —
// Saved/Speed Test/Support live as rows inside Settings, same as the app.
import SwiftUI

struct TopBar: View {
    @EnvironmentObject var app: AppState
    let title: String

    var body: some View {
        HStack(alignment: .top) {
            if app.currentScreen != .home {
                Button {
                    app.currentScreen = .home
                } label: {
                    Label(app.t(.back), systemImage: "chevron.backward")
                }
                .buttonStyle(.plain)
                Spacer()
                Text(title).font(.title2.bold())
                Spacer()
                Spacer().frame(width: 20)
            } else {
                VStack(alignment: .leading, spacing: 2) {
                    Text(title).font(.title2.bold())
                    HStack(spacing: 6) {
                        Circle().fill(.green).frame(width: 8, height: 8)
                        Text("\(app.t(.systemReady)) (\(app.proxies.count))")
                            .font(.caption.bold())
                            .foregroundStyle(.secondary)
                    }
                }
                Spacer()
                Button {
                    app.currentScreen = .settings
                } label: {
                    Image(systemName: "gearshape")
                        .font(.title3)
                }
                .buttonStyle(.plain)
            }
        }
        .padding(.horizontal, Theme.spacingLarge)
        .padding(.top, Theme.spacingMedium)
        .frame(minHeight: Theme.topBarHeight)
    }
}
