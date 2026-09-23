// SpeedTestView.swift — Proxy speed test screen, matching the Android app:
// link input + Paste/Start row, a big ping-ring result card with a
// two-column metric grid, the "Real Telegram Download Speed" estimator
// card with size-preset buttons, connection info, and a
// Connect / Save / Copy row at the bottom.
import SwiftUI

struct SpeedTestView: View {
    @EnvironmentObject var app: AppState

    private let presets = ["10", "50", "100", "500", "1000"]

    var body: some View {
        VStack(spacing: 0) {
            TopBar(title: app.t(.proxySpeedTest))

            ScrollView {
                VStack(alignment: .leading, spacing: Theme.spacingMedium) {

                    HStack {
                        Image(systemName: "link")
                            .foregroundStyle(.blue)
                        TextField("", text: $app.speedTestInput, prompt: Text(app.t(.proxyInputHint)), axis: .vertical)
                            .textFieldStyle(.plain)
                        if !app.speedTestInput.isEmpty {
                            Button {
                                app.speedTestInput = ""
                            } label: {
                                Image(systemName: "xmark").foregroundStyle(.secondary)
                            }
                            .buttonStyle(.plain)
                        }
                    }
                    .padding(Theme.spacingMedium)
                    .background(RoundedRectangle(cornerRadius: Theme.cornerRadiusCard).fill(.background.secondary))

                    if let err = app.speedTestError {
                        Text(err).font(.caption).foregroundStyle(.red)
                    }

                    HStack(spacing: 10) {
                        Button {
                            #if canImport(AppKit)
                            if let s = NSPasteboard.general.string(forType: .string) {
                                app.speedTestInput = s
                            }
                            #endif
                        } label: {
                            Label(app.t(.paste), systemImage: "clipboard")
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.bordered)

                        Button {
                            app.runSpeedTest()
                        } label: {
                            HStack {
                                if app.speedTestRunning { ProgressView().controlSize(.small) }
                                else { Image(systemName: "play.fill") }
                                Text(app.speedTestRunning ? app.t(.testing)
                                     : (app.speedTestResult == nil ? app.t(.startTest) : app.t(.testAgain)))
                                    .bold()
                            }
                            .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(app.speedTestRunning || app.speedTestInput.isEmpty)
                    }

                    if let r = app.speedTestResult {
                        resultsCard(r)
                        downloadEstimatorCard(r)
                        connectionInfoCard(r)
                        actionRow
                    }
                }
                .padding(Theme.spacingLarge)
            }
        }
    }

    private func qualityText(_ q: SpeedTester.Quality) -> String {
        switch q {
        case .excellent: return app.t(.qualityExcellent)
        case .good: return app.t(.qualityGood)
        case .fair: return app.t(.qualityFair)
        case .poor: return app.t(.qualityPoor)
        case .offline: return app.t(.qualityOffline)
        }
    }

    private func qualityColor(_ q: SpeedTester.Quality) -> Color {
        switch q {
        case .excellent: return .green
        case .good: return .mint
        case .fair: return .yellow
        case .poor: return .orange
        case .offline: return .red
        }
    }

    @ViewBuilder
    private func resultsCard(_ r: SpeedTester.Result) -> some View {
        VStack(spacing: Theme.spacingMedium) {
            // Ping ring
            ZStack {
                Circle()
                    .stroke(qualityColor(r.quality), lineWidth: 4)
                    .frame(width: 160, height: 160)
                VStack(spacing: 0) {
                    Text(r.avgMs >= 0 ? "\(r.avgMs)" : "—")
                        .font(.system(size: 40, weight: .bold))
                        .foregroundStyle(qualityColor(r.quality))
                    Text("ms").font(.subheadline).foregroundStyle(.secondary)
                }
            }
            HStack(spacing: 6) {
                Circle().fill(qualityColor(r.quality)).frame(width: 8, height: 8)
                Text("\(app.t(.quality)): \(qualityText(r.quality))")
                    .font(.callout.bold())
                    .foregroundStyle(qualityColor(r.quality))
            }
            .padding(.horizontal, 12).padding(.vertical, 6)
            .background(Capsule().fill(qualityColor(r.quality).opacity(0.15)))

            Divider()

            let stabilityPct = r.samplesTotal > 0 ? Int(100.0 * Double(r.samplesOk) / Double(r.samplesTotal)) : 0
            LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], alignment: .leading, spacing: Theme.spacingMedium) {
                metric("antenna.radiowaves.left.and.right", app.t(.avgPing), r.avgMs >= 0 ? "\(r.avgMs) ms" : "—")
                metric("checkmark.seal.fill", app.t(.stability), "\(stabilityPct)%")
                metric("arrow.down", app.t(.downloadSpeed), String(format: "%.1f Mbps", r.downloadMbps))
                metric("arrow.up", app.t(.uploadSpeed), String(format: "%.1f Mbps", r.uploadMbps))
                metric("waveform.path.ecg", app.t(.jitter), String(format: "±%.0f ms", r.jitterMs))
                metric("arrow.triangle.2.circlepath", app.t(.packetLoss), String(format: "%.0f%%", r.packetLossPct))
            }
        }
        .frame(maxWidth: .infinity)
        .padding(Theme.spacingLarge)
        .background(RoundedRectangle(cornerRadius: Theme.cornerRadiusLarge).fill(.background.secondary))
    }

    private func metric(_ icon: String, _ label: String, _ value: String) -> some View {
        HStack(spacing: 8) {
            Image(systemName: icon).foregroundStyle(.blue)
            VStack(alignment: .leading, spacing: 2) {
                Text(label).font(.caption).foregroundStyle(.secondary)
                Text(value).font(.body.bold())
            }
        }
    }

    @ViewBuilder
    private func downloadEstimatorCard(_ r: SpeedTester.Result) -> some View {
        VStack(alignment: .leading, spacing: Theme.spacingSmall) {
            HStack {
                Image(systemName: "icloud.and.arrow.down").foregroundStyle(.blue)
                Text(app.t(.realTelegramSpeedTitle)).bold()
                Spacer()
                Text(app.t(.estimatedBadge))
                    .font(.caption2.bold())
                    .padding(.horizontal, 8).padding(.vertical, 2)
                    .background(Capsule().fill(.blue.opacity(0.15)))
                    .foregroundStyle(.blue)
            }
            (Text(String(format: "%.2f", SpeedTester.estimateTelegramMBps(r))).font(.system(size: 28, weight: .bold))
             + Text(" MB/s").font(.callout.bold()))
                .foregroundStyle(.blue)
            Text(app.t(.telegramSpeedDisclaimer))
                .font(.caption2).foregroundStyle(.secondary)

            Divider()

            Text(app.t(.fileDownloadEstimator)).font(.callout.bold())
            TextField("", text: $app.speedTestFileSizeMB, prompt: Text(app.t(.fileSizeMbHint)))
                .textFieldStyle(.roundedBorder)

            HStack(spacing: 8) {
                ForEach(presets, id: \.self) { p in
                    Button("\(p) M") { app.speedTestFileSizeMB = p }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                }
            }

            if let text = app.estimatedDownloadSecondsText {
                HStack {
                    Text(app.t(.estimatedTimeResult))
                    Spacer()
                    Text(text).bold()
                }
            }
        }
        .padding(Theme.spacingMedium)
        .background(RoundedRectangle(cornerRadius: Theme.cornerRadiusCard).fill(.blue.opacity(0.06)))
        .overlay(RoundedRectangle(cornerRadius: Theme.cornerRadiusCard).stroke(.blue.opacity(0.25), lineWidth: 1))
    }

    @ViewBuilder
    private func connectionInfoCard(_ r: SpeedTester.Result) -> some View {
        if !r.resolvedIp.isEmpty {
            VStack(alignment: .leading, spacing: 2) {
                let parsed = SpeedTester.parseInput(app.speedTestInput)
                Text("\(parsed.server):\(parsed.port)")
                    .font(.system(.callout, design: .monospaced)).bold()
                Text("IP: \(r.resolvedIp)").font(.caption).foregroundStyle(.secondary)
                Text("\(app.t(.dnsLookupLabel)): \(r.dnsLookupMs >= 0 ? "\(r.dnsLookupMs) ms" : "—")")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
    }

    private var actionRow: some View {
        VStack(spacing: 10) {
            Button {
                app.connectSpeedTestProxy()
            } label: {
                HStack {
                    Image(systemName: "arrow.forward")
                    Text(app.t(.connect)).bold()
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, 4)
            }
            .buttonStyle(.borderedProminent)
            .clipShape(Capsule())

            HStack(spacing: 10) {
                Button {
                    app.saveSpeedTestProxy()
                } label: {
                    Label(app.t(.save), systemImage: "bookmark")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)

                Button {
                    app.copySpeedTestProxy()
                } label: {
                    Label(app.t(.copy), systemImage: "doc.on.doc")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
            }
        }
    }
}
