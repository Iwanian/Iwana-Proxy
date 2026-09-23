// TelegramLauncher.swift — Opens a proxy link, always routing through the
// tg:// scheme so it launches Telegram Desktop directly instead of a web
// browser. Ported 1:1 from the Windows build's TelegramLauncher.h
// (ShellExecuteW -> NSWorkspace.shared.open), which itself mirrors the
// Android app's TelegramLauncher.kt explicit https://t.me/proxy ->
// tg://proxy conversion.
import Foundation
#if canImport(AppKit)
import AppKit
#endif

enum TelegramLauncher {

    /// Converts a web-form proxy link (https://t.me/proxy?... or
    /// https://telegram.me/proxy?...) into the tg://proxy?... scheme so macOS
    /// routes it to Telegram Desktop's registered URL handler instead of
    /// opening a browser. Links already in tg:// form pass through unchanged.
    static func toTelegramScheme(_ link: String) -> String {
        func replacePrefix(_ prefix: String) -> String? {
            guard link.hasPrefix(prefix) else { return nil }
            return "tg://proxy" + link.dropFirst(prefix.count)
        }
        if let c = replacePrefix("https://t.me/proxy") { return c }
        if let c = replacePrefix("http://t.me/proxy") { return c }
        if let c = replacePrefix("https://telegram.me/proxy") { return c }
        if let c = replacePrefix("http://telegram.me/proxy") { return c }
        return link // already tg://proxy?... or not a proxy link
    }

    @discardableResult
    static func openProxyLink(_ link: String) -> Bool {
        let target = toTelegramScheme(link)
        guard let url = URL(string: target) else { return false }
        #if canImport(AppKit)
        return NSWorkspace.shared.open(url)
        #else
        return false
        #endif
    }

    /// For non-proxy URLs (GitHub, plain https links) — opens as-is, no conversion.
    @discardableResult
    static func openPlainUrl(_ urlString: String) -> Bool {
        guard let url = URL(string: urlString) else { return false }
        #if canImport(AppKit)
        return NSWorkspace.shared.open(url)
        #else
        return false
        #endif
    }

    private static func urlEncode(_ s: String) -> String {
        s.addingPercentEncoding(withAllowedCharacters: .alphanumerics) ?? s
    }

    /// macOS has no single universal "share sheet" equivalent reachable from
    /// a plain command without NSSharingService UI plumbing, so — mirroring
    /// what the Windows build does — Share hands the link to the user's
    /// default mail client pre-filled, a real, working share channel (as
    /// opposed to Copy, which only puts it on the clipboard).
    @discardableResult
    static func shareProxyLink(_ link: String) -> Bool {
        let target = "mailto:?subject=\(urlEncode("Iwana Proxy"))&body=\(urlEncode(link))"
        guard let url = URL(string: target) else { return false }
        #if canImport(AppKit)
        return NSWorkspace.shared.open(url)
        #else
        return false
        #endif
    }

    /// Copies text to the system clipboard (equivalent of the Windows
    /// build's clipboard "Copy" action).
    static func copyToClipboard(_ text: String) {
        #if canImport(AppKit)
        let pb = NSPasteboard.general
        pb.clearContents()
        pb.setString(text, forType: .string)
        #endif
    }
}
