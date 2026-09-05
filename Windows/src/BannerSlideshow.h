// BannerSlideshow.h — Fetches real promotional images from the same GitHub
// source the Android app uses (BannerRepository.kt) and decodes them with
// GDI+ for an auto-rotating, clickable banner strip on the Home screen.
// Mirrors the Android app's per-image-links-via-companion-.txt-file logic.
#pragma once
#include <windows.h>
#include <wininet.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <regex>
#include <algorithm>

#pragma comment(lib, "wininet.lib")

namespace BannerSlideshow {

    inline const std::vector<std::wstring> kApiEndpoints = {
        L"https://api.github.com/repos/Iwanian/Sub/contents/pic",
        L"https://api.github.com/repos/Iwanian/Sub/contents",
    };

    struct BannerItem {
        std::wstring imageUrl;
        std::wstring targetLink; // may be empty
    };

    inline bool HttpGetBytes(const std::wstring& url, std::string& outBytes) {
        HINTERNET hInternet = InternetOpenW(L"IwanaProxy/1.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        if (!hInternet) return false;
        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
        HINTERNET hUrl = InternetOpenUrlW(hInternet, url.c_str(), L"User-Agent: IwanaProxyWindows\r\n", (DWORD)-1, flags, 0);
        if (!hUrl) { InternetCloseHandle(hInternet); return false; }
        char buf[8192]; DWORD read = 0;
        std::string data;
        while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0) data.append(buf, read);
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        if (data.empty()) return false;
        outBytes = std::move(data);
        return true;
    }

    inline std::wstring Utf8ToWide(const std::string& s) {
        if (s.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring out(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
        return out;
    }

    inline std::wstring ToLowerCopy(std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
        return s;
    }

    inline std::wstring TrimQuotes(std::wstring s) {
        while (!s.empty() && (s.front()==L' '||s.front()==L'\t')) s.erase(s.begin());
        while (!s.empty() && (s.back()==L' '||s.back()==L'\t'||s.back()==L'\r')) s.pop_back();
        if (s.size()>=2 && ((s.front()==L'"'&&s.back()==L'"')||(s.front()==L'\''&&s.back()==L'\''))) s = s.substr(1, s.size()-2);
        return s;
    }

    inline bool LooksLikeLink(const std::wstring& s) {
        std::wstring low = ToLowerCopy(s);
        return low.rfind(L"http://",0)==0 || low.rfind(L"https://",0)==0 || low.rfind(L"tg://",0)==0
            || low.rfind(L"t.me/",0)==0 || (!s.empty() && s[0]==L'@');
    }

    // Fetches text content and returns the first link-like line, or the first
    // non-empty line if none look like a link, matching Android's fetchTextContent.
    inline std::wstring FetchFirstLinkLine(const std::wstring& url) {
        std::string bytes;
        if (!HttpGetBytes(url, bytes)) return L"";
        std::wstring text = Utf8ToWide(bytes);
        std::vector<std::wstring> lines;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t nl = text.find(L'\n', pos);
            std::wstring line = TrimQuotes(text.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos));
            if (!line.empty()) lines.push_back(line);
            if (nl == std::wstring::npos) break;
            pos = nl + 1;
        }
        for (auto& l : lines) if (LooksLikeLink(l)) return l;
        return lines.empty() ? L"" : lines[0];
    }

    struct ContentsEntry { std::wstring name, downloadUrl; };

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
        size_t n = (std::min)(names.size(), urls.size());
        for (size_t i = 0; i < n; ++i) {
            ContentsEntry e;
            e.name = Utf8ToWide(names[i].second);
            e.downloadUrl = Utf8ToWide(urls[i].second);
            out.push_back(e);
        }
        return out;
    }

    inline bool IsImageFile(const std::wstring& lowerName) {
        return (lowerName.size()>4 && lowerName.rfind(L".jpg")==lowerName.size()-4) ||
               (lowerName.size()>4 && lowerName.rfind(L".png")==lowerName.size()-4) ||
               (lowerName.size()>5 && lowerName.rfind(L".jpeg")==lowerName.size()-5) ||
               (lowerName.size()>5 && lowerName.rfind(L".webp")==lowerName.size()-5);
    }

    inline std::wstring BaseName(const std::wstring& name) {
        size_t dot = name.find_last_of(L'.');
        return dot == std::wstring::npos ? name : name.substr(0, dot);
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

        std::vector<std::pair<std::wstring,std::wstring>> images; // baseName(lower), url
        std::unordered_map<std::wstring, std::wstring> txtFiles;  // baseName(lower) -> url
        for (auto& e : entries) {
            std::wstring lowerName = ToLowerCopy(e.name);
            if (IsImageFile(lowerName)) {
                images.push_back({ ToLowerCopy(BaseName(e.name)), e.downloadUrl });
            } else if (lowerName.size() > 4 && lowerName.rfind(L".txt") == lowerName.size()-4) {
                txtFiles[ToLowerCopy(BaseName(e.name))] = e.downloadUrl;
            }
        }
        if (images.empty()) return {};

        std::wstring generalLinkUrl;
        for (const wchar_t* key : { L"link", L"links", L"url", L"urls" }) {
            auto it = txtFiles.find(key);
            if (it != txtFiles.end()) { generalLinkUrl = it->second; break; }
        }
        std::vector<std::wstring> generalLinks;
        if (!generalLinkUrl.empty()) {
            std::string bytes;
            if (HttpGetBytes(generalLinkUrl, bytes)) {
                std::wstring text = Utf8ToWide(bytes);
                size_t pos = 0;
                while (pos <= text.size()) {
                    size_t nl = text.find(L'\n', pos);
                    std::wstring line = TrimQuotes(text.substr(pos, nl==std::wstring::npos?std::wstring::npos:nl-pos));
                    if (!line.empty()) generalLinks.push_back(line);
                    if (nl == std::wstring::npos) break;
                    pos = nl + 1;
                }
            }
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
                item.targetLink = generalLinks[(std::min)(i, generalLinks.size()-1)];
            }
            result.push_back(item);
        }
        return result;
    }

    inline Gdiplus::Image* DecodeImage(const std::string& bytes) {
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
        if (!hMem) return nullptr;
        void* p = GlobalLock(hMem);
        memcpy(p, bytes.data(), bytes.size());
        GlobalUnlock(hMem);

        IStream* stream = nullptr;
        if (CreateStreamOnHGlobal(hMem, TRUE, &stream) != S_OK) {
            GlobalFree(hMem);
            return nullptr;
        }
        Gdiplus::Image* img = new Gdiplus::Image(stream);
        stream->Release();
        if (img->GetLastStatus() != Gdiplus::Ok) {
            delete img;
            return nullptr;
        }
        return img;
    }

}

