// ProxyParser.h — Extracts MTProto proxy links (tg://proxy?... or t.me/proxy?...)
// from raw fetched text and parses their query parameters.
//
// Direct port of the Windows build's ProxyParser.h. The only real change is
// std::wstring/std::wregex -> std::string/std::regex: raw fetched text is
// UTF-8 bytes and every marker we scan for (tg://, #ru, digits, the ⭐/★/✨/🇷🇺
// bytes) works fine as a *byte*-level regex/substring match, so there's no
// need for a wide-char conversion step at all on Linux (no MultiByteToWideChar
// equivalent needed). ToLowerCopy below only folds ASCII 'A'-'Z' bytes, which
// is safe on UTF-8: multi-byte sequence continuation bytes are always >= 0x80
// and are left untouched.
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

    inline std::string UrlDecode(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            if (in[i] == '%' && i + 2 < in.size()) {
                std::string hex = in.substr(i + 1, 2);
                try {
                    char ch = (char)std::stoi(hex, nullptr, 16);
                    out += ch;
                    i += 2;
                } catch (...) { out += in[i]; }
            } else if (in[i] == '+') {
                out += ' ';
            } else {
                out += in[i];
            }
        }
        return out;
    }

    inline std::string GetQueryParam(const std::string& link, const std::string& key) {
        size_t qPos = link.find('?');
        if (qPos == std::string::npos) return "";
        std::string query = link.substr(qPos + 1);
        size_t pos = 0;
        while (pos < query.size()) {
            size_t amp = query.find('&', pos);
            std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
            size_t eq = pair.find('=');
            if (eq != std::string::npos) {
                std::string k = pair.substr(0, eq);
                if (k == key) return UrlDecode(pair.substr(eq + 1));
            }
            if (amp == std::string::npos) break;
            pos = amp + 1;
        }
        return "";
    }

    // Folds only ASCII 'A'-'Z' bytes; UTF-8 continuation/lead bytes (>=0x80)
    // pass through untouched, so this is safe to call on UTF-8 text.
    inline std::string ToLowerCopy(std::string s) {
        for (char& c : s) {
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        }
        return s;
    }

    inline bool ContainsCI(const std::string& haystack, const std::string& needle) {
        return ToLowerCopy(haystack).find(ToLowerCopy(needle)) != std::string::npos;
    }

    inline std::vector<ProxyItem> Parse(const std::string& rawText) {
        std::vector<ProxyItem> proxies;
        std::unordered_set<std::string> seenKeys;
        int idCounter = 1;

        // Matches: tg://proxy?... or http(s)://t.me/proxy?... or http(s)://telegram.me/proxy?...
        static const std::regex kProxyRegex(
            R"((tg://proxy\?[^\s"']+|https?://(?:t\.me|telegram\.me)/proxy\?[^\s"']+))");

        auto tryAdd = [&](std::string rawLink, const std::string& lineContext) {
            // Trim a trailing period that isn't part of a .txt-style suffix.
            if (!rawLink.empty() && rawLink.back() == '.') rawLink.pop_back();

            std::string server = GetQueryParam(rawLink, "server");
            std::string port   = GetQueryParam(rawLink, "port");
            std::string secret = GetQueryParam(rawLink, "secret");
            if (server.empty() || port.empty()) return;
            // Port must be numeric.
            for (char c : port) if (!std::isdigit((unsigned char)c)) return;

            std::string key = server + ":" + port;
            if (seenKeys.count(key)) return;
            seenKeys.insert(key);

            ProxyItem item;
            item.id = idCounter++;
            item.server = server;
            item.port = port;
            item.secret = secret;
            item.link = rawLink;
            item.pingMs = -1;
            // "\u2B50"=⭐ "\u2605"=★ "\u2728"=✨ as raw UTF-8 byte sequences.
            item.isForDownload = ContainsCI(lineContext, "\xE2\xAD\x90") // ⭐
                               || ContainsCI(lineContext, "\xE2\x98\x85") // ★
                               || ContainsCI(lineContext, "\xE2\x9C\xA8"); // ✨
            item.isRussian = ContainsCI(lineContext, "#ru") || ContainsCI(lineContext, "#rus")
                            || ContainsCI(lineContext, "#russia")
                            || ContainsCI(lineContext, "\xF0\x9F\x87\xB7\xF0\x9F\x87\xBA"); // 🇷🇺
            proxies.push_back(std::move(item));
        };

        // Line-by-line pass (primary strategy, matches Android behavior).
        {
            std::stringstream ss(rawText);
            std::string line;
            while (std::getline(ss, line)) {
                std::smatch m;
                if (std::regex_search(line, m, kProxyRegex)) {
                    tryAdd(m[0].str(), line);
                }
            }
        }

        // Fallback: scan the whole blob for any matches if line-based pass found nothing.
        if (proxies.empty()) {
            auto begin = std::sregex_iterator(rawText.begin(), rawText.end(), kProxyRegex);
            auto end = std::sregex_iterator();
            for (auto it = begin; it != end; ++it) {
                tryAdd(it->str(), it->str());
            }
        }

        return proxies;
    }

}
