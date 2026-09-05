// ProxySource.h — Real network fetch pipeline (Stage 5, Part 1).
//
// STRICT ORDER (per product requirement):
//   1) Config::ProxySourceUrls[0]  (primary GitHub raw URL)
//   2) Config::ProxySourceUrls[1]  (fallback URL)
//   3) Local disk cache (Config::CacheFilePath()) if both network attempts fail
//
// On ANY successful network fetch, the raw text is written to the cache file
// so future offline attempts (step 3) have fresh-as-possible data.
//
// Uses WinINet (wininet.lib) — available on Windows 7 through 11, no extra
// runtime needed, keeps the binary small and dependency-free.
#pragma once
#include <windows.h>
#include <wininet.h>
#include <string>
#include <fstream>
#include <sstream>
#include "Config.h"

#pragma comment(lib, "wininet.lib")

namespace ProxySource {

    enum class FetchOrigin { PrimaryUrl, FallbackUrl, Cache, None };

    struct FetchResult {
        bool         success = false;
        FetchOrigin  origin  = FetchOrigin::None;
        std::string  rawText;      // raw proxy list text (ASCII/UTF-8)
        std::wstring errorDetail;  // populated when success == false
    };

    constexpr DWORD kConnectTimeoutMs = 6000;
    constexpr DWORD kReadTimeoutMs    = 8000;

    // Attempts a single HTTP GET. Returns true and fills `outText` on success.
    inline bool HttpGet(const std::wstring& url, std::string& outText) {
        HINTERNET hInternet = InternetOpenW(L"IwanaProxy/1.0",
            INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        if (!hInternet) return false;

        InternetSetOptionW(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, (LPVOID)&kConnectTimeoutMs, sizeof(DWORD));
        InternetSetOptionW(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, (LPVOID)&kReadTimeoutMs, sizeof(DWORD));
        InternetSetOptionW(hInternet, INTERNET_OPTION_SEND_TIMEOUT, (LPVOID)&kReadTimeoutMs, sizeof(DWORD));

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
        HINTERNET hUrl = InternetOpenUrlW(hInternet, url.c_str(), nullptr, 0, flags, 0);
        if (!hUrl) {
            InternetCloseHandle(hInternet);
            return false;
        }

        // Verify HTTP status is 2xx.
        DWORD statusCode = 0, statusSize = sizeof(statusCode);
        HttpQueryInfoW(hUrl, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_STATUS_CODE, &statusCode, &statusSize, nullptr);
        if (statusCode < 200 || statusCode >= 300) {
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            return false;
        }

        char buffer[4096];
        DWORD bytesRead = 0;
        std::string result;
        result.reserve(1 << 16);
        while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            result.append(buffer, bytesRead);
        }

        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);

        if (result.empty()) return false;
        outText = std::move(result);
        return true;
    }

    inline bool ReadCache(std::string& outText) {
        std::wstring path = Config::CacheFilePath();
        std::ifstream f(path.c_str(), std::ios::binary);
        if (!f.is_open()) return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        outText = ss.str();
        return !outText.empty();
    }

    inline void WriteCache(const std::string& text) {
        std::wstring path = Config::CacheFilePath();
        std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
        if (f.is_open()) f << text;
    }

    // Sequential-fallback fetch: primary -> fallback -> cache.
    // Safe to call from a worker thread (no UI touched here).
    inline FetchResult Fetch() {
        FetchResult res;

        if (Config::ProxySourceUrls.size() >= 1) {
            std::string text;
            if (HttpGet(Config::ProxySourceUrls[0], text)) {
                res.success = true;
                res.origin  = FetchOrigin::PrimaryUrl;
                res.rawText = std::move(text);
                WriteCache(res.rawText);
                return res;
            }
        }

        if (Config::ProxySourceUrls.size() >= 2) {
            std::string text;
            if (HttpGet(Config::ProxySourceUrls[1], text)) {
                res.success = true;
                res.origin  = FetchOrigin::FallbackUrl;
                res.rawText = std::move(text);
                WriteCache(res.rawText);
                return res;
            }
        }

        std::string cached;
        if (ReadCache(cached)) {
            res.success = true;
            res.origin  = FetchOrigin::Cache;
            res.rawText = std::move(cached);
            return res;
        }

        res.success = false;
        res.origin  = FetchOrigin::None;
        res.errorDetail = L"Both sources unreachable and no cached data is available.";
        return res;
    }

}
