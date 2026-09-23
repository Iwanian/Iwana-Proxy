// ProxyCard.swift — One row in the proxy list. Mirrors the Android app's
// card exactly: "Proxy N" + status/region badges on the left, latency +
// "LATENCY" caption on the right, host:port below, then a row of
// copy/bookmark icons and a pill-shaped Connect button.
import SwiftUI

struct ProxyCard: View {
    @EnvironmentObject var app: AppState
    let item: ProxyItem

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.spacingSmall) {
            HStack(alignment: .top) {
                VStack(alignment: .leading, spacing: 6) {
                    HStack(spacing: 6) {
                        Text("Proxy \(item.id)")
                            .font(.body.bold())
                        statusBadge
                        if item.isRussian {
                            badge(app.t(.russianBadge), color: .orange)
                        }
                        if item.isForDownload {
                            badge(app.t(.forDownloadBadge), color: .blue)
                        }
                    }
                    Text("\(item.server).:\(item.port)")
                        .font(.system(.caption, design: .monospaced))
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
                Spacer()
                if item.isScanned && item.pingMs >= 0 {
                    VStack(alignment: .trailing, spacing: 0) {
                        (Text("\(item.pingMs)").font(.title3.bold()) + Text(" ms").font(.caption.bold()))
                            .foregroundStyle(.green)
                        Text(app.t(.latencyLabel))
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                    }
                }
            }

            Divider()

            HStack(spacing: 14) {
                Button { app.copyLink(item) } label: {
                    Image(systemName: "doc.on.doc")
                        .foregroundStyle(.secondary)
                }
                .buttonStyle(.plain)

                Button { app.toggleFavorite(item) } label: {
                    Image(systemName: item.isFavorite ? "bookmark.fill" : "bookmark")
                        .foregroundStyle(.secondary)
                }
                .buttonStyle(.plain)

                Spacer()

                Button {
                    app.connect(item)
                } label: {
                    HStack(spacing: 4) {
                        Image(systemName: "arrow.forward")
                        Text(app.t(.connect))
                            .font(.callout.bold())
                    }
                    .padding(.horizontal, 16)
                    .padding(.vertical, 6)
                }
                .buttonStyle(.borderedProminent)
                .clipShape(Capsule())
            }
        }
        .padding(Theme.spacingMedium)
        .background(RoundedRectangle(cornerRadius: Theme.cornerRadiusCard).fill(.background.secondary))
        .overlay(RoundedRectangle(cornerRadius: Theme.cornerRadiusCard).stroke(.separator, lineWidth: 1))
    }

    @ViewBuilder
    private var statusBadge: some View {
        if item.isScanned {
            badge(item.pingMs >= 0 ? app.t(.online) : app.t(.offline),
                  color: item.pingMs >= 0 ? .green : .red)
        }
    }

    private func badge(_ text: String, color: Color) -> some View {
        Text(text)
            .font(.system(size: 9, weight: .bold))
            .padding(.horizontal, 6)
            .padding(.vertical, 2)
            .background(RoundedRectangle(cornerRadius: Theme.cornerRadiusPill / 2).fill(color.opacity(0.18)))
            .foregroundStyle(color)
    }
}
