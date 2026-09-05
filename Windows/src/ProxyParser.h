// ProxyParser.h — Extracts MTProto proxy links (tg://proxy?... or t.me/proxy?...)
// from raw fetched text and parses their query parameters. Mirrors the Android
// app's ProxyParser.kt logic (regex extraction + query param parsing + de-dup).
#pragma once
#include <string>
#include <vector>
#include <regex>
#include <sstream>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include "ProxyItem.h"

namespace ProxyParser {

    // Minimal query-string parser for a URL of the form scheme://host/path?k=v&k2=v2
    inline std::wstring UrlDecode(const std::wstring& in) {
        std::wstring out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            if (in[i] == L'%' && i + 2 < in.size()) {
                auto hex = in.substr(i + 1, 2);
                try {
                    wchar_t ch = (wchar_t)std::stoi(hex, nullptr, 16);
                    out += ch;
                    i += 2;
                } catch (...) { out += in[i]; }
            } else if (in[i] == L'+') {
                out += L' ';
            } else {
                out += in[i];
            }
        }
        return out;
    }

    inline std::wstring GetQueryParam(const std::wstring& link, const std::wstring& key) {
        size_t qPos = link.find(L'?');
        if (qPos == std::wstring::npos) return L"";
        std::wstring query = link.substr(qPos + 1);
        size_t pos = 0;
        while (pos < query.size()) {
            size_t amp = query.find(L'&', pos);
            std::wstring pair = query.substr(pos, amp == std::wstring::npos ? std::wstring::npos : amp - pos);
            size_t eq = pair.find(L'=');
            if (eq != std::wstring::npos) {
                std::wstring k = pair.substr(0, eq);
                if (k == key) return UrlDecode(pair.substr(eq + 1));
            }
            if (amp == std::wstring::npos) break;
            pos = amp + 1;
        }
        return L"";
    }

    inline std::wstring ToLowerCopy(std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)std::towlower(c); });
        return s;
    }

    inline bool ContainsCI(const std::wstring& haystack, const std::wstring& needle) {
        return ToLowerCopy(haystack).find(ToLowerCopy(needle)) != std::wstring::npos;
    }

    // narrow(UTF-8-ish ASCII raw text) -> wide, good enough for these plain-text proxy lists.
    inline std::wstring NarrowToWide(const std::string& s) {
        if (s.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring out(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
        return out;
    }

    inline std::vector<ProxyItem> Parse(const std::string& rawTextUtf8) {
        std::wstring rawText = NarrowToWide(rawTextUtf8);
        std::vector<ProxyItem> proxies;
        std::unordered_set<std::wstring> seenKeys;
        int idCounter = 1;

        // Matches: tg://proxy?... or http(s)://t.me/proxy?... or http(s)://telegram.me/proxy?...
        static const std::wregex kProxyRegex(
            LR"((tg://proxy\?[^\s"']+|https?://(?:t\.me|telegram\.me)/proxy\?[^\s"']+))");

        auto tryAdd = [&](std::wstring rawLink, const std::wstring& lineContext) {
            // Trim a trailing period that isn't part of a .txt-style suffix.
            if (!rawLink.empty() && rawLink.back() == L'.') rawLink.pop_back();

            std::wstring server = GetQueryParam(rawLink, L"server");
            std::wstring port   = GetQueryParam(rawLink, L"port");
            std::wstring secret = GetQueryParam(rawLink, L"secret");
            if (server.empty() || port.empty()) return;
            // Port must be numeric.
            for (wchar_t c : port) if (!iswdigit(c)) return;

            std::wstring key = server + L":" + port;
            if (seenKeys.count(key)) return;
            seenKeys.insert(key);

            ProxyItem item;
            item.id = idCounter++;
            item.server = server;
            item.port = port;
            item.secret = secret;
            item.link = rawLink;
            item.pingMs = -1;
            item.isForDownload = ContainsCI(lineContext, L"\u2B50") || ContainsCI(lineContext, L"\u2605")
                               || ContainsCI(lineContext, L"\u2728");
            item.isRussian = ContainsCI(lineContext, L"#ru") || ContainsCI(lineContext, L"#rus")
                            || ContainsCI(lineContext, L"#russia") || ContainsCI(lineContext, L"\U0001F1F7\U0001F1FA");
            proxies.push_back(std::move(item));
        };

        // Line-by-line pass (primary strategy, matches Android behavior).
        {
            std::wstringstream ss(rawText);
            std::wstring line;
            while (std::getline(ss, line)) {
                std::wsmatch m;
                if (std::regex_search(line, m, kProxyRegex)) {
                    tryAdd(m[0].str(), line);
                }
            }
        }

        // Fallback: scan the whole blob for any matches if line-based pass found nothing.
        if (proxies.empty()) {
            auto begin = std::wsregex_iterator(rawText.begin(), rawText.end(), kProxyRegex);
            auto end = std::wsregex_iterator();
            for (auto it = begin; it != end; ++it) {
                tryAdd(it->str(), it->str());
            }
        }

        return proxies;
    }

}
