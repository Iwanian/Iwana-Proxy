// BannerSlideshow.h — Fetches promotional images from the same GitHub source
// the Windows/Android builds use, and decodes them for an auto-rotating,
// clickable banner strip on the Home screen.
//
// Same per-image-links-via-companion-.txt-file logic as the Windows build,
// only the transport (libcurl instead of WinINet) and image decode
// (GdkPixbuf instead of GDI+) are swapped.
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <regex>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <curl/curl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

namespace BannerSlideshow {

    inline const std::vector<std::string> kApiEndpoints = {
        "https://api.github.com/repos/Iwanian/Sub/contents/pic",
        "https://api.github.com/repos/Iwanian/Sub/contents",
    };

    struct BannerItem {
        std::string imageUrl;
        std::string targetLink; // may be empty
    };

    inline size_t WriteCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* out = static_cast<std::string*>(userdata);
        out->append(ptr, size * nmemb);
        return size * nmemb;
    }

    inline bool HttpGetBytes(const std::string& url, std::string& outBytes) {
        CURL* curl = curl_easy_init();
        if (!curl) return false;
        std::string data;
        struct curl_slist* headers = curl_slist_append(nullptr, "User-Agent: IwanaProxyLinux");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "IwanaProxy/1.0 (Linux)");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 6000L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 8000L);
        CURLcode rc = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (rc != CURLE_OK || data.empty()) return false;
        outBytes = std::move(data);
        return true;
    }

    inline std::string ToLowerCopy(std::string s) {
        for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        return s;
    }

    inline std::string TrimQuotes(std::string s) {
        while (!s.empty() && (s.front()==' '||s.front()=='\t')) s.erase(s.begin());
        while (!s.empty() && (s.back()==' '||s.back()=='\t'||s.back()=='\r')) s.pop_back();
        if (s.size()>=2 && ((s.front()=='"'&&s.back()=='"')||(s.front()=='\''&&s.back()=='\''))) s = s.substr(1, s.size()-2);
        return s;
    }

    inline bool LooksLikeLink(const std::string& s) {
        std::string low = ToLowerCopy(s);
        return low.rfind("http://",0)==0 || low.rfind("https://",0)==0 || low.rfind("tg://",0)==0
            || low.rfind("t.me/",0)==0 || (!s.empty() && s[0]=='@');
    }

    inline std::vector<std::string> SplitLines(const std::string& text) {
        std::vector<std::string> lines;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t nl = text.find('\n', pos);
            std::string line = TrimQuotes(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
            if (!line.empty()) lines.push_back(line);
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
        return lines;
    }

    // Fetches text content and returns the first link-like line, or the first
    // non-empty line if none look like a link.
    inline std::string FetchFirstLinkLine(const std::string& url) {
        std::string bytes;
        if (!HttpGetBytes(url, bytes)) return "";
        auto lines = SplitLines(bytes);
        for (auto& l : lines) if (LooksLikeLink(l)) return l;
        return lines.empty() ? "" : lines[0];
    }

    struct ContentsEntry { std::string name, downloadUrl; };

    inline std::vector<ContentsEntry> ParseContentsJson(const std::string& json) {
        std::vector<ContentsEntry> out;
        static const std::regex reName("\"name\"\\s*:\\s*\"([^\"]+)\"");
        static const std::regex reUrl("\"download_url\"\\s*:\\s*\"([^\"]+)\"");
        // Naive pairing: GitHub's contents API emits one object per file with both
        // fields; scan sequentially and pair the nearest name+download_url.
        std::sregex_iterator nameIt(json.begin(), json.end(), reName), nameEnd;
        std::sregex_iterator urlIt(json.begin(), json.end(), reUrl), urlEnd;
        std::vector<std::pair<size_t,std::string>> names, urls;
        for (; nameIt != nameEnd; ++nameIt) names.push_back({(size_t)nameIt->position(), (*nameIt)[1].str()});
        for (; urlIt != urlEnd; ++urlIt) urls.push_back({(size_t)urlIt->position(), (*urlIt)[1].str()});
        size_t n = std::min(names.size(), urls.size());
        for (size_t i = 0; i < n; ++i) {
            ContentsEntry e;
            e.name = names[i].second;
            e.downloadUrl = urls[i].second;
            out.push_back(e);
        }
        return out;
    }

    inline bool IsImageFile(const std::string& lowerName) {
        auto endsWith = [&](const char* suf) {
            size_t sl = std::strlen(suf);
            return lowerName.size() > sl && lowerName.compare(lowerName.size()-sl, sl, suf) == 0;
        };
        return endsWith(".jpg") || endsWith(".png") || endsWith(".jpeg") || endsWith(".webp");
    }

    inline std::string BaseName(const std::string& name) {
        size_t dot = name.find_last_of('.');
        return dot == std::string::npos ? name : name.substr(0, dot);
    }

    // Fetches the banner list with per-image target links resolved from
    // companion .txt files (or a general link.txt/links.txt fallback).
    inline std::vector<BannerItem> FetchBannerItems() {
        std::vector<ContentsEntry> entries;
        for (const auto& api : kApiEndpoints) {
            std::string json;
            if (HttpGetBytes(api, json)) {
                entries = ParseContentsJson(json);
                if (!entries.empty()) break;
            }
        }
        if (entries.empty()) return {};

        std::vector<std::pair<std::string,std::string>> images; // baseName(lower), url
        std::unordered_map<std::string, std::string> txtFiles;  // baseName(lower) -> url
        for (auto& e : entries) {
            std::string lowerName = ToLowerCopy(e.name);
            if (IsImageFile(lowerName)) {
                images.push_back({ ToLowerCopy(BaseName(e.name)), e.downloadUrl });
            } else if (lowerName.size() > 4 && lowerName.compare(lowerName.size()-4, 4, ".txt") == 0) {
                txtFiles[ToLowerCopy(BaseName(e.name))] = e.downloadUrl;
            }
        }
        if (images.empty()) return {};

        std::string generalLinkUrl;
        for (const char* key : { "link", "links", "url", "urls" }) {
            auto it = txtFiles.find(key);
            if (it != txtFiles.end()) { generalLinkUrl = it->second; break; }
        }
        std::vector<std::string> generalLinks;
        if (!generalLinkUrl.empty()) {
            std::string bytes;
            if (HttpGetBytes(generalLinkUrl, bytes)) generalLinks = SplitLines(bytes);
        }

        std::vector<BannerItem> result;
        for (size_t i = 0; i < images.size() && i < 6; ++i) {
            BannerItem item;
            item.imageUrl = images[i].second;
            auto txtIt = txtFiles.find(images[i].first);
            if (txtIt != txtFiles.end()) {
                item.targetLink = FetchFirstLinkLine(txtIt->second);
            }
            if (item.targetLink.empty() && !generalLinks.empty()) {
                item.targetLink = generalLinks[std::min(i, generalLinks.size()-1)];
            }
            result.push_back(item);
        }
        return result;
    }

    // Decodes raw image bytes (jpg/png/webp) into a GdkPixbuf. Caller owns
    // the returned pixbuf and must g_object_unref() it. Returns nullptr on
    // decode failure (mirrors DecodeImage's nullptr-on-failure contract from
    // the Windows build).
    inline GdkPixbuf* DecodeImage(const std::string& bytes) {
        GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
        GError* error = nullptr;
        gboolean ok = gdk_pixbuf_loader_write(loader,
            reinterpret_cast<const guchar*>(bytes.data()), bytes.size(), &error);
        if (error) { g_error_free(error); error = nullptr; }
        gdk_pixbuf_loader_close(loader, ok ? nullptr : &error);
        if (error) { g_error_free(error); }
        GdkPixbuf* pixbuf = ok ? gdk_pixbuf_loader_get_pixbuf(loader) : nullptr;
        if (pixbuf) g_object_ref(pixbuf);
        g_object_unref(loader);
        return pixbuf;
    }

}
