// TelegramLauncher.h — Opens a proxy link, always routing through the tg://
// scheme so it launches Telegram Desktop directly instead of a web browser.
// Same explicit https://t.me/proxy -> tg://proxy conversion as the Windows
// build; only the "hand it to the OS" step changes (PORT_SPEC.md §11):
// ShellExecuteW -> GLib's g_app_info_launch_default_for_uri (which itself
// dispatches to xdg-open under the hood but also gives us a real GError to
// check, unlike shelling out blind).
#pragma once
#include <string>
#include <gio/gio.h>

namespace TelegramLauncher {

    // Converts a web-form proxy link (https://t.me/proxy?... or
    // https://telegram.me/proxy?...) into the tg://proxy?... scheme so the OS
    // routes it to Telegram Desktop's registered .desktop MIME handler for
    // the tg:// scheme instead of opening a browser. Links already in tg://
    // form pass through unchanged.
    inline std::string ToTelegramScheme(const std::string& link) {
        auto replacePrefix = [&](const std::string& prefix) -> std::string {
            if (link.compare(0, prefix.size(), prefix) == 0) {
                return "tg://proxy" + link.substr(prefix.size());
            }
            return "";
        };
        std::string converted;
        if (!(converted = replacePrefix("https://t.me/proxy")).empty()) return converted;
        if (!(converted = replacePrefix("http://t.me/proxy")).empty()) return converted;
        if (!(converted = replacePrefix("https://telegram.me/proxy")).empty()) return converted;
        if (!(converted = replacePrefix("http://telegram.me/proxy")).empty()) return converted;
        return link; // already tg://proxy?... or not a proxy link
    }

    inline bool OpenUri(const std::string& uri) {
        GError* error = nullptr;
        gboolean ok = g_app_info_launch_default_for_uri(uri.c_str(), nullptr, &error);
        if (error) g_error_free(error);
        return ok == TRUE;
    }

    // Converts a plain https://t.me/<username> (channel/user/bot) link into
    // tg://resolve?domain=<username>, so it opens inside the Telegram app
    // rather than a browser landing page. Unlike ToTelegramScheme above,
    // this covers ordinary profile/channel links, not proxy links.
    inline std::string ToTelegramResolveScheme(const std::string& link) {
        auto stripPrefix = [&](const std::string& prefix) -> std::string {
            if (link.compare(0, prefix.size(), prefix) == 0) return link.substr(prefix.size());
            return "";
        };
        std::string rest;
        if (rest.empty()) rest = stripPrefix("https://t.me/");
        if (rest.empty()) rest = stripPrefix("http://t.me/");
        if (rest.empty()) rest = stripPrefix("https://telegram.me/");
        if (rest.empty()) rest = stripPrefix("http://telegram.me/");
        if (rest.empty()) return link; // not a t.me link (or already tg://) — pass through
        if (rest.rfind("proxy", 0) == 0) return link; // proxy links use ToTelegramScheme instead
        return "tg://resolve?domain=" + rest;
    }

    inline bool OpenTelegramLink(const std::string& link) {
        return OpenUri(ToTelegramResolveScheme(link));
    }

    // Returns true if the desktop reported it could launch a handler.
    inline bool OpenProxyLink(const std::string& link) {
        return OpenUri(ToTelegramScheme(link));
    }

    // For non-proxy URLs (GitHub, plain https links) — opens as-is, no conversion.
    inline bool OpenPlainUrl(const std::string& url) {
        return OpenUri(url);
    }

    // Minimal percent-encoder, just enough for building a mailto: body param.
    inline std::string UrlEncode(const std::string& in) {
        std::string out;
        char buf[8];
        for (unsigned char c : in) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') { out += (char)c; continue; }
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
        return out;
    }

    // Linux desktops have no single universal "share sheet" either — mirroring
    // what the Windows build does, Share hands the link to the user's default
    // mail client pre-filled via a mailto: URI (a real, working share channel,
    // as opposed to Copy, which only puts it on the clipboard).
    inline bool ShareProxyLink(const std::string& link) {
        std::string target = "mailto:?subject=" + UrlEncode("Iwana Proxy") + "&body=" + UrlEncode(link);
        return OpenUri(target);
    }

}
