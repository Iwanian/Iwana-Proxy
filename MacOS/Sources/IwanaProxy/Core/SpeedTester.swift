// SpeedTester.swift — Real per-proxy speed test.
// Ported 1:1 from the Windows build's SpeedTester.h: average ping, jitter
// (stddev), min/max, packet loss %, and a quality rating — computed from
// real TCP connect-timing samples via PingService.pingOnce.
import Foundation
#if canImport(Darwin)
import Darwin
#endif

enum SpeedTester {

    struct ParsedProxy {
        var server = "", port = "", secret = ""
        var valid = false
    }

    static func parseInput(_ raw: String) -> ParsedProxy {
        var out = ParsedProxy()
        let s = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !s.isEmpty else { return out }

        if s.contains("tg://proxy") || s.contains("/proxy?") {
            func getParam(_ key: String) -> String {
                guard let qIdx = s.firstIndex(of: "?") else { return "" }
                let query = s[s.index(after: qIdx)...]
                for pair in query.split(separator: "&") {
                    if let eq = pair.firstIndex(of: "="), pair[pair.startIndex..<eq] == key {
                        return String(pair[pair.index(after: eq)...])
                    }
                }
                return ""
            }
            out.server = getParam("server")
            out.port = getParam("port")
            out.secret = getParam("secret")
        } else {
            // server:port[:secret]
            let parts = s.split(separator: ":", maxSplits: 2, omittingEmptySubsequences: false)
            guard parts.count >= 2 else { return out }
            out.server = String(parts[0])
            out.port = String(parts[1])
            if parts.count >= 3 { out.secret = String(parts[2]) }
        }
        guard !out.server.isEmpty, !out.port.isEmpty, out.port.allSatisfy({ $0.isNumber }) else { return out }
        out.valid = true
        return out
    }

    enum Quality { case excellent, good, fair, poor, offline }

    struct Result {
        var avgMs = -1, minMs = -1, maxMs = -1
        var jitterMs = 0.0
        var packetLossPct = 0.0
        var quality: Quality = .offline
        var samplesOk = 0, samplesTotal = 0
        // Estimated (not measured) throughput in Mbps, derived from
        // latency/jitter/loss — mirrors the Android app's own approach: these
        // are estimates based on network conditions, not guaranteed/measured
        // Telegram speeds.
        var downloadMbps = 0.0
        var uploadMbps = 0.0
        var resolvedIp = ""
        var dnsLookupMs = -1
    }

    /// Estimated *real-world Telegram* download speed in MB/s — per product
    /// formula: take the (heuristic) download Mbps figure, convert megabits
    /// to megabytes (÷8), then apply a 0.3 real-world derating factor.
    static func estimateTelegramMBps(_ r: Result) -> Double {
        guard r.downloadMbps > 0 else { return 0 }
        return (r.downloadMbps / 8.0) * 0.3
    }

    private static func estimateThroughput(_ r: inout Result) {
        guard r.avgMs >= 0 else { r.downloadMbps = 0; r.uploadMbps = 0; return }
        let baseline = 40.0 // Mbps ceiling for an ideal (near-zero latency) connection
        let decay = baseline / (1.0 + Double(r.avgMs) / 60.0)
        let jitterPenalty = 1.0 / (1.0 + r.jitterMs / 50.0)
        let lossPenalty = 1.0 - (r.packetLossPct / 100.0) * 0.9
        var est = decay * jitterPenalty * lossPenalty
        if est < 0.1 { est = 0.1 }
        r.downloadMbps = est
        r.uploadMbps = est * 0.6
    }

    static func estimateDownloadSeconds(_ r: Result, fileSizeMB: Double) -> Double {
        guard r.downloadMbps > 0, fileSizeMB > 0 else { return -1 }
        let megabits = fileSizeMB * 8.0
        return megabits / r.downloadMbps
    }

    private static func resolve(host: String, port: String) -> (ip: String, ms: Int)? {
        var hints = addrinfo()
        hints.ai_family = AF_UNSPEC
        hints.ai_socktype = SOCK_STREAM
        var resultPtr: UnsafeMutablePointer<addrinfo>?
        let start = DispatchTime.now()
        guard getaddrinfo(host, port, &hints, &resultPtr) == 0, let addrInfo = resultPtr else { return nil }
        defer { freeaddrinfo(resultPtr) }
        let end = DispatchTime.now()
        let ms = Int((end.uptimeNanoseconds - start.uptimeNanoseconds) / 1_000_000)

        var ipBuf = [CChar](repeating: 0, count: Int(INET6_ADDRSTRLEN))
        var ipString = ""
        if let ai_addr = addrInfo.pointee.ai_addr {
            if addrInfo.pointee.ai_family == AF_INET {
                var addr = ai_addr.withMemoryRebound(to: sockaddr_in.self, capacity: 1) { $0.pointee.sin_addr }
                if inet_ntop(AF_INET, &addr, &ipBuf, socklen_t(ipBuf.count)) != nil {
                    ipString = String(cString: ipBuf)
                }
            } else if addrInfo.pointee.ai_family == AF_INET6 {
                var addr = ai_addr.withMemoryRebound(to: sockaddr_in6.self, capacity: 1) { $0.pointee.sin6_addr }
                if inet_ntop(AF_INET6, &addr, &ipBuf, socklen_t(ipBuf.count)) != nil {
                    ipString = String(cString: ipBuf)
                }
            }
        }
        return (ipString, ms)
    }

    static func run(_ proxy: ParsedProxy, sampleCount: Int = 8) -> Result {
        var r = Result()
        r.samplesTotal = sampleCount

        // Resolve the host once up front — gives us the IP to display and a
        // real DNS lookup time, same info the Android app's connection
        // details panel shows.
        if let resolved = resolve(host: proxy.server, port: proxy.port) {
            r.resolvedIp = resolved.ip
            r.dnsLookupMs = resolved.ms
        }

        var samples: [Int] = []
        for _ in 0..<sampleCount {
            let ms = PingService.pingOnce(host: proxy.server, port: proxy.port)
            if ms >= 0 { samples.append(ms) }
        }
        r.samplesOk = samples.count
        r.packetLossPct = sampleCount > 0 ? (100.0 * Double(sampleCount - r.samplesOk) / Double(sampleCount)) : 100.0

        guard !samples.isEmpty else {
            r.quality = .offline
            return r
        }

        let sum = samples.reduce(0, +)
        r.minMs = samples.min()!
        r.maxMs = samples.max()!
        r.avgMs = sum / samples.count

        let variance = samples.reduce(0.0) { acc, v in
            let d = Double(v - r.avgMs)
            return acc + d * d
        } / Double(samples.count)
        r.jitterMs = variance.squareRoot()

        if r.packetLossPct > 20.0 { r.quality = .poor }
        else if r.avgMs <= 100 && r.packetLossPct == 0.0 { r.quality = .excellent }
        else if r.avgMs <= 250 && r.packetLossPct <= 5.0 { r.quality = .good }
        else if r.avgMs <= 500 { r.quality = .fair }
        else { r.quality = .poor }

        estimateThroughput(&r)
        return r
    }
}
