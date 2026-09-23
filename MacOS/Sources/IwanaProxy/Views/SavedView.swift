// SavedView.swift — Favorites screen. Mirrors main.cpp's Screen::Saved block.
import SwiftUI

struct SavedView: View {
    @EnvironmentObject var app: AppState
    @State private var showClearConfirm = false

    var body: some View {
        VStack(spacing: 0) {
            TopBar(title: app.t(.savedProxies))

            if app.favoriteProxies.isEmpty {
                VStack(spacing: Theme.spacingSmall) {
                    Spacer()
                    Text(app.t(.noSavedProxies)).font(.headline)
                    Text(app.t(.tapStarHint)).font(.caption).foregroundStyle(.secondary)
                    Spacer()
                }
                .frame(maxWidth: .infinity)
            } else {
                ScrollView {
                    VStack(alignment: .leading, spacing: Theme.spacingMedium) {
                        ForEach(app.favoriteProxies) { item in
                            ProxyCard(item: item)
                        }

                        Button(role: .destructive) {
                            showClearConfirm = true
                        } label: {
                            Text(app.t(.clearFavorites)).frame(maxWidth: .infinity)
                        }
                        .padding(.top, Theme.spacingSmall)
                    }
                    .padding(Theme.spacingLarge)
                }
            }
        }
        .confirmationDialog(app.t(.clearFavConfirmTitle), isPresented: $showClearConfirm) {
            Button(app.t(.clear), role: .destructive) { app.clearFavorites() }
            Button(app.t(.cancel), role: .cancel) {}
        } message: {
            Text(app.t(.clearFavConfirmBody))
        }
    }
}
