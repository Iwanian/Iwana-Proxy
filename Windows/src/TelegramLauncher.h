// TelegramLauncher.h — Opens a proxy link, always routing through the tg://
// scheme so it launches Telegram Desktop directly instead of a web browser.
// Mirrors Android TelegramLauncher.kt's explicit https://t.me/proxy ->
// tg://proxy conversion.
#pragma once
#include <windows.h>
#include <string>

namespace TelegramLauncher {

    // Converts a web-form proxy link (https://t.me/proxy?... or
    // https://telegram.me/proxy?...) into the tg://proxy?... scheme so the OS
    // routes it to Telegram Desktop's registered protocol handler instead of
    // opening a browser. Links already in tg:// form pass through unchanged.
    inline std::wstring ToTelegramScheme(const std::wstring& link) {
        auto replacePrefix = [&](const std::wstring& prefix) -> std::wstring {
            if (link.compare(0, prefix.size(), prefix) == 0) {
                return L"tg://proxy" + link.substr(prefix.size());
            }
            return L"";
        };
        std::wstring converted;
        if (!(converted = replacePrefix(L"https://t.me/proxy")).empty()) return converted;
        if (!(converted = replacePrefix(L"http://t.me/proxy")).empty()) return converted;
        if (!(converted = replacePrefix(L"https://telegram.me/proxy")).empty()) return converted;
        if (!(converted = replacePrefix(L"http://telegram.me/proxy")).empty()) return converted;
        return link; // already tg://proxy?... or not a proxy link
    }

    // Returns true if the shell reported it could launch a handler.
    inline bool OpenProxyLink(HWND owner, const std::wstring& link) {
        std::wstring target = ToTelegramScheme(link);
        HINSTANCE result = ShellExecuteW(owner, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return (INT_PTR)result > 32;
    }

    // For non-proxy URLs (GitHub, plain https links) — opens as-is, no conversion.
    inline bool OpenPlainUrl(HWND owner, const std::wstring& url) {
        HINSTANCE result = ShellExecuteW(owner, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return (INT_PTR)result > 32;
    }

    // Minimal percent-encoder, just enough for building a mailto: body param.
    inline std::wstring UrlEncode(const std::wstring& in) {
        std::wstring out;
        for (wchar_t c : in) {
            if (iswalnum(c) || c == L'-' || c == L'_' || c == L'.' || c == L'~') { out += c; continue; }
            wchar_t buf[8]; swprintf(buf, 8, L"%%%02X", (unsigned)c);
            out += buf;
        }
        return out;
    }

    // Windows desktop has no single universal "share sheet" equivalent to
    // Android's Intent.ACTION_SEND without pulling in UWP APIs, so — mirroring
    // what most Win32 desktop apps do — Share hands the link to the user's
    // default mail client pre-filled, which is a real, working share channel
    // (as opposed to Copy, which only puts it on the clipboard).
    inline bool ShareProxyLink(HWND owner, const std::wstring& link) {
        std::wstring target = L"mailto:?subject=" + UrlEncode(L"Iwana Proxy") + L"&body=" + UrlEncode(link);
        HINSTANCE result = ShellExecuteW(owner, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return (INT_PTR)result > 32;
    }

}

