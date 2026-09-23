// Storage.swift — Persistent favorites storage.
// Ported 1:1 from the Windows build's Storage.h: a plain newline-delimited
// "server:port" list on disk (equivalent of the Android app's
// DataStoreManager.kt favorites feature).
import Foundation

enum Storage {

    static func loadFavoriteKeys() -> Set<String> {
        guard let text = try? String(contentsOf: Config.favoritesFilePath(), encoding: .utf8) else {
            return []
        }
        let lines = text.split(separator: "\n").map(String.init).filter { !$0.isEmpty }
        return Set(lines)
    }

    static func saveFavoriteKeys(_ keys: Set<String>) {
        let text = keys.sorted().joined(separator: "\n") + (keys.isEmpty ? "" : "\n")
        try? text.write(to: Config.favoritesFilePath(), atomically: true, encoding: .utf8)
    }
}
