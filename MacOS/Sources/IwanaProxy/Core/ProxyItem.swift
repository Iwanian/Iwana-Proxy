// ProxyItem.swift — Data model for a parsed MTProto proxy.
// Ported 1:1 from the Windows build's ProxyItem.h (which itself mirrors the
// Android app's ProxyItem.kt).
import Foundation

struct ProxyItem: Identifiable, Hashable {
    let id: Int
    var server: String
    var port: String
    var secret: String
    var link: String            // original tg://proxy?... link, used to open Telegram
    var pingMs: Int = -1        // -1 = not yet tested / unreachable
    var isScanned: Bool = false // true once a ping attempt has completed (success or fail)
    var isFavorite: Bool = false
    var isForDownload: Bool = false // "starred" marker found in the source list
    var isRussian: Bool = false

    var isAlive: Bool { isScanned && pingMs >= 0 }
    var key: String { "\(server):\(port)" }
}
