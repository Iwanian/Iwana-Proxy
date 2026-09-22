// ProxySource.h — Real network fetch pipeline.
//
// STRICT ORDER (per product requirement, unchanged from Windows build):
//   1) Config::ProxySourceUrls[0]  (primary GitHub raw URL)
//   2) Config::ProxySourceUrls[1]  (fallback URL)
//   3) Local disk cache (Config::CacheFilePath()) if both network attempts fail
//
// On ANY successful network fetch, the raw text is written to the cache file
// so future offline attempts (step 3) have fresh-as-possible data.
//
// Uses libcurl instead of WinINet (PORT_SPEC.md §2), keeping the same 6s
// connect / 8s total-read timeout budget.
#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <curl/curl.h>
#include "Config.h"

namespace ProxySource {

    enum class FetchOrigin { PrimaryUrl, FallbackUrl, Cache, None };

    struct FetchResult {
        bool        success = false;
        FetchOrigin origin  = FetchOrigin::None;
        std::string rawText;      // raw proxy list text (UTF-8)
        std::string errorDetail;  // populated when success == false
    };

    constexpr long kConnectTimeoutMs = 6000;
    constexpr long kTotalTimeoutMs   = 8000; // matches the Windows build's read-timeout budget

    inline size_t WriteCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* out = static_cast<std::string*>(userdata);
        out->append(ptr, size * nmemb);
        return size * nmemb;
    }

    // Attempts a single HTTPS GET. Returns true and fills `outText` on success
    // (2xx status + non-empty body).
    inline bool HttpGet(const std::string& url, std::string& outText) {
        CURL* curl = curl_easy_init();
        if (!curl) return false;

        std::string buffer;
        buffer.reserve(1 << 16);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "IwanaProxy/1.0 (Linux)");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, kTotalTimeoutMs);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // enable all supported encodings

        CURLcode rc = curl_easy_perform(curl);
        long statusCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
        curl_easy_cleanup(curl);

        if (rc != CURLE_OK) return false;
        if (statusCode < 200 || statusCode >= 300) return false;
        if (buffer.empty()) return false;

        outText = std::move(buffer);
        return true;
    }

    inline bool ReadCache(std::string& outText) {
        std::string path = Config::CacheFilePath();
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        outText = ss.str();
        return !outText.empty();
    }

    inline void WriteCache(const std::string& text) {
        std::string path = Config::CacheFilePath();
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (f.is_open()) f << text;
    }

    // Sequential-fallback fetch: primary -> fallback -> cache.
    // Safe to call from a worker thread (no UI touched here); the caller is
    // expected to marshal the result back to the GTK main thread (e.g. via
    // g_idle_add / g_main_context_invoke), matching the Windows build's
    // PostMessage hand-off.
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
        res.errorDetail = "Both sources unreachable and no cached data is available.";
        return res;
    }

}
