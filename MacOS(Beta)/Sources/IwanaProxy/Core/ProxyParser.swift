// ProxyParser.swift — Extracts MTProto proxy links (tg://proxy?... or
// t.me/proxy?...) from raw fetched text and parses their query parameters.
// Ported 1:1 from the Windows build's ProxyParser.h (regex extraction +
// query param parsing + de-dup), which itself mirrors the Android app's
// ProxyParser.kt.
import Foundation

enum ProxyParser {

    private static func queryParam(_ link: String, _ key: String) -> String {
        guard let qIdx = link.firstIndex(of: "?") else { return "" }
        let query = link[link.index(after: qIdx)...]
        for pair in query.split(separator: "&") {
            if let eq = pair.firstIndex(of: "=") {
                let k = pair[pair.startIndex..<eq]
                if k == key {
                    let vRaw = String(pair[pair.index(after: eq)...])
                    return vRaw.replacingOccurrences(of: "+", with: " ").removingPercentEncoding ?? vRaw
                }
            }
        }
        return ""
    }

    private static func containsCI(_ haystack: String, _ needle: String) -> Bool {
        haystack.range(of: needle, options: .caseInsensitive) != nil
    }

    // Matches: tg://proxy?... or http(s)://t.me/proxy?... or http(s)://telegram.me/proxy?...
    private static let proxyRegex: NSRegularExpression = {
        // swiftlint:disable:next force_try
        try! NSRegularExpression(
            pattern: #"(tg://proxy\?[^\s"']+|https?://(?:t\.me|telegram\.me)/proxy\?[^\s"']+)"#
        )
    }()

    private static func matches(in text: String) -> [String] {
        let ns = text as NSString
        let range = NSRange(location: 0, length: ns.length)
        return proxyRegex.matches(in: text, range: range).map { ns.substring(with: $0.range) }
    }

    static func parse(_ rawText: String) -> [ProxyItem] {
        var proxies: [ProxyItem] = []
        var seenKeys = Set<String>()
        var idCounter = 1

        func tryAdd(_ rawLinkIn: String, lineContext: String) {
            var rawLink = rawLinkIn
            if rawLink.hasSuffix(".") { rawLink.removeLast() }

            let server = queryParam(rawLink, "server")
            let port = queryParam(rawLink, "port")
            let secret = queryParam(rawLink, "secret")
            guard !server.isEmpty, !port.isEmpty else { return }
            guard port.allSatisfy({ $0.isNumber }) else { return }

            let key = "\(server):\(port)"
            guard !seenKeys.contains(key) else { return }
            seenKeys.insert(key)

            let item = ProxyItem(
                id: idCounter,
                server: server,
                port: port,
                secret: secret,
                link: rawLink,
                pingMs: -1,
                isScanned: false,
                isFavorite: false,
                isForDownload: containsCI(lineContext, "\u{2B50}") || containsCI(lineContext, "\u{2605}")
                    || containsCI(lineContext, "\u{2728}"),
                isRussian: containsCI(lineContext, "#ru") || containsCI(lineContext, "#rus")
                    || containsCI(lineContext, "#russia") || lineContext.contains("\u{1F1F7}\u{1F1FA}")
            )
            idCounter += 1
            proxies.append(item)
        }

        // Line-by-line pass (primary strategy, matches Android/Windows behavior).
        rawText.enumerateLines { line, _ in
            if let first = matches(in: line).first {
                tryAdd(first, lineContext: line)
            }
        }

        // Fallback: scan the whole blob for any matches if the line-based pass found nothing.
        if proxies.isEmpty {
            for m in matches(in: rawText) {
                tryAdd(m, lineContext: m)
            }
        }

        return proxies
    }
}
