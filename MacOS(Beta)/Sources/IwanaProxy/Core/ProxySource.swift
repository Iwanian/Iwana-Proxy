// ProxySource.swift — Real network fetch pipeline.
//
// STRICT ORDER (per product requirement, unchanged from Windows/Android):
//   1) Config.proxySourceUrls[0]  (primary GitHub raw URL)
//   2) Config.proxySourceUrls[1]  (fallback URL)
//   3) Local disk cache (Config.cacheFilePath()) if both network attempts fail
//
// On ANY successful network fetch, the raw text is written to the cache file
// so future offline attempts (step 3) have fresh-as-possible data.
//
// Uses URLSession (the macOS-native equivalent of the Windows build's
// WinINet), so no third-party networking dependency is needed.
import Foundation

enum FetchOrigin {
    case primaryUrl, fallbackUrl, cache, none
}

struct FetchResult {
    var success = false
    var origin: FetchOrigin = .none
    var rawText: String = ""
    var errorDetail: String = ""
}

enum ProxySource {

    static let connectTimeout: TimeInterval = 6
    static let readTimeout: TimeInterval = 8

    private static func httpGet(_ urlString: String) async -> String? {
        guard let url = URL(string: urlString) else { return nil }
        var request = URLRequest(url: url)
        request.timeoutInterval = connectTimeout + readTimeout
        request.cachePolicy = .reloadIgnoringLocalAndRemoteCacheData
        request.setValue("IwanaProxy/1.0", forHTTPHeaderField: "User-Agent")

        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            guard let http = response as? HTTPURLResponse, (200..<300).contains(http.statusCode) else {
                return nil
            }
            guard !data.isEmpty, let text = String(data: data, encoding: .utf8) else { return nil }
            return text
        } catch {
            return nil
        }
    }

    private static func readCache() -> String? {
        guard let text = try? String(contentsOf: Config.cacheFilePath(), encoding: .utf8), !text.isEmpty else {
            return nil
        }
        return text
    }

    private static func writeCache(_ text: String) {
        try? text.write(to: Config.cacheFilePath(), atomically: true, encoding: .utf8)
    }

    /// Sequential-fallback fetch: primary -> fallback -> cache.
    /// Safe to call from any (non-main) task; does not touch UI state.
    static func fetch() async -> FetchResult {
        var res = FetchResult()

        let urls = Config.proxySourceUrls
        if urls.count >= 1, let text = await httpGet(urls[0]) {
            res.success = true
            res.origin = .primaryUrl
            res.rawText = text
            writeCache(text)
            return res
        }

        if urls.count >= 2, let text = await httpGet(urls[1]) {
            res.success = true
            res.origin = .fallbackUrl
            res.rawText = text
            writeCache(text)
            return res
        }

        if let cached = readCache() {
            res.success = true
            res.origin = .cache
            res.rawText = cached
            return res
        }

        res.success = false
        res.origin = .none
        res.errorDetail = "Both sources unreachable and no cached data is available."
        return res
    }
}
