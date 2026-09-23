// BannerLoader.swift — Loads the promo banner slider images from
// https://github.com/Iwanian/Sub/tree/main/pic at runtime.
//
// Contract of that folder (mirrors what the Android app reads): for every
// banner there's an image file (jpg/jpeg/png/webp) and a .txt file with the
// SAME base name, whose contents are the URL to open when that banner is
// tapped, e.g.:
//   pic/IMG_20260905_115540_784.jpg
//   pic/IMG_20260905_115540_784.txt   -> "https://github.com/.../releases/tag/v1.0.0"
//
// Any number of banners can live in that folder — this fetches the
// directory listing via the GitHub Contents API, so nothing is hardcoded.
// Downloaded banners are cached to disk so the slider still has something
// to show on a later launch with no network.
import Foundation
#if canImport(AppKit)
import AppKit
#endif

struct BannerItem: Identifiable {
    let id: String       // base file name, used as a stable identity
    let image: NSImage
    let link: String      // URL to open on tap; may be empty if no .txt was found
}

enum BannerLoader {

    private static let apiURL = URL(string: "https://api.github.com/repos/Iwanian/Sub/contents/pic")!
    private static let imageExtensions: Set<String> = ["jpg", "jpeg", "png", "webp"]

    private struct GitHubEntry: Decodable {
        let name: String
        let type: String
        let download_url: String?
    }

    private static var cacheDir: URL = {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.temporaryDirectory
        let dir = base.appendingPathComponent("IwanaProxy/BannerCache", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }()

    /// Fetches the current banners from GitHub; falls back to whatever was
    /// cached from the last successful fetch if the network call fails or
    /// the folder is empty.
    static func loadBanners() async -> [BannerItem] {
        if let fresh = await fetchRemote(), !fresh.isEmpty {
            cacheToDisk(fresh)
            return fresh
        }
        return loadFromCache()
    }

    private static func fetchRemote() async -> [BannerItem]? {
        do {
            var request = URLRequest(url: apiURL)
            request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
            let (data, _) = try await URLSession.shared.data(for: request)
            let entries = try JSONDecoder().decode([GitHubEntry].self, from: data)

            var imageURLByBase: [String: String] = [:]
            var linkURLByBase: [String: String] = [:]
            for entry in entries where entry.type == "file" {
                guard let downloadURL = entry.download_url else { continue }
                let ext = (entry.name as NSString).pathExtension.lowercased()
                let base = (entry.name as NSString).deletingPathExtension
                if imageExtensions.contains(ext) {
                    imageURLByBase[base] = downloadURL
                } else if ext == "txt" {
                    linkURLByBase[base] = downloadURL
                }
            }

            var items: [BannerItem] = []
            for (base, imageURLString) in imageURLByBase {
                guard let imageURL = URL(string: imageURLString),
                      let (imageData, _) = try? await URLSession.shared.data(from: imageURL),
                      let image = NSImage(data: imageData) else { continue }

                var link = ""
                if let linkURLString = linkURLByBase[base], let linkURL = URL(string: linkURLString),
                   let (linkData, _) = try? await URLSession.shared.data(from: linkURL) {
                    link = String(data: linkData, encoding: .utf8)?
                        .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
                }
                items.append(BannerItem(id: base, image: image, link: link))
            }
            return items.sorted { $0.id < $1.id }
        } catch {
            return nil
        }
    }

    private static func cacheToDisk(_ items: [BannerItem]) {
        // Clear stale entries so a banner removed upstream doesn't linger.
        if let existing = try? FileManager.default.contentsOfDirectory(at: cacheDir, includingPropertiesForKeys: nil) {
            for url in existing { try? FileManager.default.removeItem(at: url) }
        }
        for item in items {
            guard let tiff = item.image.tiffRepresentation,
                  let rep = NSBitmapImageRep(data: tiff),
                  let jpeg = rep.representation(using: .jpeg, properties: [:]) else { continue }
            try? jpeg.write(to: cacheDir.appendingPathComponent("\(item.id).jpg"))
            try? item.link.write(to: cacheDir.appendingPathComponent("\(item.id).txt"),
                                  atomically: true, encoding: .utf8)
        }
    }

    private static func loadFromCache() -> [BannerItem] {
        guard let files = try? FileManager.default.contentsOfDirectory(at: cacheDir, includingPropertiesForKeys: nil) else {
            return []
        }
        var items: [BannerItem] = []
        for file in files where file.pathExtension.lowercased() == "jpg" {
            let base = file.deletingPathExtension().lastPathComponent
            guard let data = try? Data(contentsOf: file), let image = NSImage(data: data) else { continue }
            let link = (try? String(contentsOf: cacheDir.appendingPathComponent("\(base).txt"), encoding: .utf8))?
                .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            items.append(BannerItem(id: base, image: image, link: link))
        }
        return items.sorted { $0.id < $1.id }
    }
}
