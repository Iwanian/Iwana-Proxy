// SupportView.swift — Support/donate screen. Matches the Android app:
// "always free" banner, three wallet cards (USDT/BTC/TRX) with the full
// address + copy icon, a network-mismatch warning, then GitHub/Telegram
// link rows and a thank-you line.
import SwiftUI

private struct Wallet: Identifiable {
    let id = UUID()
    let label: String
    let address: String
}

struct SupportView: View {
    @EnvironmentObject var app: AppState

    // Ported verbatim from main.cpp's Screen::Support wallet list.
    private let wallets: [Wallet] = [
        Wallet(label: "USDT (Polygon)", address: "0x3d76c651ee3f76ac468e2769c9d9fbfc"),
        Wallet(label: "BTC (Ethereum)", address: "0x3d76c651ee3f76ac468e2769c9d9fbfc"),
    ]
    private let trxAddress = "TFaCWNT4N9wHJ2e1Z9MSuz1waUoMse"

    var body: some View {
        VStack(spacing: 0) {
            TopBar(title: app.t(.supportTitle))

            ScrollView {
                VStack(alignment: .leading, spacing: Theme.spacingMedium) {
                    Text(app.t(.supportBanner))
                        .font(.callout.bold())
                        .foregroundStyle(.blue)
                        .padding(Theme.spacingMedium)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(RoundedRectangle(cornerRadius: Theme.cornerRadiusLarge).fill(.background.secondary))

                    Label(app.t(.cryptoHeader), systemImage: "dollarsign.circle.fill")
                        .font(.headline)
                        .foregroundStyle(.primary)

                    ForEach(wallets) { wallet in
                        walletCard(label: wallet.label, address: wallet.address)
                    }
                    walletCard(label: app.t(.trxRecommended), address: trxAddress)

                    HStack(alignment: .top, spacing: 8) {
                        Text("⚠️")
                        Text(app.t(.networkWarning))
                            .font(.callout)
                            .foregroundStyle(.red)
                    }
                    .padding(Theme.spacingMedium)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(RoundedRectangle(cornerRadius: Theme.cornerRadiusCard).fill(.red.opacity(0.08)))

                    Divider()

                    Label(app.t(.starHeader), systemImage: "star.fill")
                        .font(.headline)
                        .foregroundStyle(.yellow)

                    Text(app.t(.starSubtext)).font(.callout)
                    linkRow(title: "GitHub", systemImage: "chevron.left.forwardslash.chevron.right") {
                        TelegramLauncher.openPlainUrl("https://github.com/Iwanian/Iwana-Proxy")
                    }

                    Text(app.t(.telegramSubtext)).font(.callout)
                    linkRow(title: "Telegram", systemImage: "paperplane.fill") {
                        TelegramLauncher.openPlainUrl("https://t.me/I_w_a_n_a")
                    }

                    Text(app.t(.thankYou) + " ❤️")
                        .font(.callout.bold())
                        .foregroundStyle(.blue)
                        .padding(.top, Theme.spacingSmall)
                        .frame(maxWidth: .infinity, alignment: .center)
                }
                .padding(Theme.spacingLarge)
            }
        }
    }

    private func walletCard(label: String, address: String) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(label).font(.callout.bold()).foregroundStyle(.blue)
            HStack {
                Text(address)
                    .font(.system(.callout, design: .monospaced))
                Spacer()
                Button {
                    TelegramLauncher.copyToClipboard(address)
                } label: {
                    Image(systemName: "doc.on.doc")
                }
                .buttonStyle(.plain)
            }
        }
        .padding(Theme.spacingMedium)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: Theme.cornerRadiusCard).fill(.background.secondary))
    }

    private func linkRow(title: String, systemImage: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack {
                Image(systemName: systemImage)
                Text(title).bold()
                Spacer()
                Image(systemName: "arrow.up.forward.square")
            }
            .padding(Theme.spacingMedium)
        }
        .buttonStyle(.plain)
        .overlay(RoundedRectangle(cornerRadius: Theme.cornerRadiusCard).stroke(.secondary.opacity(0.3), lineWidth: 1))
    }
}
