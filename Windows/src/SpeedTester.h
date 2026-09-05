// SpeedTester.h — Real per-proxy speed test (Stage: UX fix).
// Mirrors ProxySpeedTester.kt's metric set: average ping, jitter (stddev),
// min/max, packet loss %, and a quality rating — computed from real TCP
// connect-timing samples via PingService::PingOnce.
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include "PingService.h"

namespace SpeedTester {

    struct ParsedProxy { std::wstring server, port, secret; bool valid = false; };

    inline ParsedProxy ParseInput(const std::wstring& raw) {
        ParsedProxy out;
        std::wstring s = raw;
        // Trim whitespace
        while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
        while (!s.empty() && iswspace(s.back())) s.pop_back();
        if (s.empty()) return out;

        if (s.find(L"tg://proxy") != std::wstring::npos || s.find(L"/proxy?") != std::wstring::npos) {
            auto getParam = [&](const std::wstring& key) -> std::wstring {
                size_t q = s.find(L'?');
                if (q == std::wstring::npos) return L"";
                std::wstring query = s.substr(q + 1);
                size_t pos = 0;
                while (pos < query.size()) {
                    size_t amp = query.find(L'&', pos);
                    std::wstring pair = query.substr(pos, amp == std::wstring::npos ? std::wstring::npos : amp - pos);
                    size_t eq = pair.find(L'=');
                    if (eq != std::wstring::npos && pair.substr(0, eq) == key) return pair.substr(eq + 1);
                    if (amp == std::wstring::npos) break;
                    pos = amp + 1;
                }
                return L"";
            };
            out.server = getParam(L"server");
            out.port = getParam(L"port");
            out.secret = getParam(L"secret");
        } else {
            // server:port[:secret]
            size_t first = s.find(L':');
            if (first == std::wstring::npos) return out;
            size_t second = s.find(L':', first + 1);
            out.server = s.substr(0, first);
            out.port = (second == std::wstring::npos) ? s.substr(first + 1) : s.substr(first + 1, second - first - 1);
            if (second != std::wstring::npos) out.secret = s.substr(second + 1);
        }
        if (out.server.empty() || out.port.empty()) return out;
        for (wchar_t c : out.port) if (!iswdigit(c)) return out;
        out.valid = true;
        return out;
    }

    enum class Quality { Excellent, Good, Fair, Poor, Offline };

    struct Result {
        int avgMs = -1, minMs = -1, maxMs = -1;
        double jitterMs = 0.0;
        double packetLossPct = 0.0;
        Quality quality = Quality::Offline;
        int samplesOk = 0, samplesTotal = 0;
        // Estimated (not measured) throughput in Mbps, derived from latency/jitter/loss —
        // mirrors the Android app's own approach (see telegram_speed_disclaimer: these are
        // estimates based on network conditions, not guaranteed/measured Telegram speeds).
        double downloadMbps = 0.0;
        double uploadMbps = 0.0;
        std::wstring resolvedIp;
        int dnsLookupMs = -1;
    };

    // Estimated *real-world Telegram* download speed in MB/s — per product
    // formula: take the (heuristic) download Mbps figure, convert megabits
    // to megabytes (÷8), then apply a 0.3 real-world derating factor (a raw
    // TCP-connect-based Mbps estimate is well above what Telegram's own
    // protocol/CDN path typically sustains).
    inline double EstimateTelegramMBps(const Result& r) {
        if (r.downloadMbps <= 0.0) return 0.0;
        return (r.downloadMbps / 8.0) * 0.3;
    }

    // Heuristic bandwidth estimate from RTT/jitter/loss. Not a real throughput
    // measurement (an MTProto data transfer isn't performed) — same caveat the
    // Android app surfaces to the user via TelegramSpeedDisclaimer.
    inline void EstimateThroughput(Result& r) {
        if (r.avgMs < 0) { r.downloadMbps = 0.0; r.uploadMbps = 0.0; return; }
        // Smooth decay curve instead of a multiplicative factor that blew
        // past the ceiling (and got clamped there) for almost any decent
        // ping — which made the estimate saturate at the same constant
        // value for every good connection instead of actually varying.
        double baseline = 40.0; // Mbps ceiling for an ideal (near-zero latency) connection
        double decay = baseline / (1.0 + (double)r.avgMs / 60.0);
        double jitterPenalty = 1.0 / (1.0 + r.jitterMs / 50.0);
        double lossPenalty   = 1.0 - (r.packetLossPct / 100.0) * 0.9;
        double est = decay * jitterPenalty * lossPenalty;
        if (est < 0.1) est = 0.1;
        r.downloadMbps = est;
        r.uploadMbps = est * 0.6; // upload is typically the smaller half on residential/mobile links
    }

    // Estimated seconds to download a file of fileSizeMB given a Result's
    // estimated download speed. Returns -1 if speed is unavailable.
    inline double EstimateDownloadSeconds(const Result& r, double fileSizeMB) {
        if (r.downloadMbps <= 0.0 || fileSizeMB <= 0.0) return -1.0;
        double megabits = fileSizeMB * 8.0;
        return megabits / r.downloadMbps;
    }

    inline Result Run(const ParsedProxy& proxy, int sampleCount = 8) {
        Result r;
        r.samplesTotal = sampleCount;
        std::vector<int> samples;
        std::string host = PingService::ToNarrow(proxy.server);
        std::string port = PingService::ToNarrow(proxy.port);

        // Resolve the host once up front — gives us the IP to display and a
        // real DNS lookup time, same info the Android app's connection
        // details panel shows.
        {
            WSADATA wsaData; WSAStartup(MAKEWORD(2,2), &wsaData);
            addrinfo hints = {}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
            addrinfo* result = nullptr;
            auto dnsStart = std::chrono::steady_clock::now();
            if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) == 0 && result) {
                auto dnsEnd = std::chrono::steady_clock::now();
                r.dnsLookupMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(dnsEnd - dnsStart).count();
                char ipBuf[INET6_ADDRSTRLEN] = {};
                void* addrPtr = nullptr;
                if (result->ai_family == AF_INET) addrPtr = &((sockaddr_in*)result->ai_addr)->sin_addr;
                else if (result->ai_family == AF_INET6) addrPtr = &((sockaddr_in6*)result->ai_addr)->sin6_addr;
                if (addrPtr && inet_ntop(result->ai_family, addrPtr, ipBuf, sizeof(ipBuf))) {
                    int len = MultiByteToWideChar(CP_UTF8, 0, ipBuf, -1, nullptr, 0);
                    std::wstring wip(len, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, ipBuf, -1, &wip[0], len);
                    if (!wip.empty() && wip.back() == L'\0') wip.pop_back();
                    r.resolvedIp = wip;
                }
                freeaddrinfo(result);
            }
        }

        for (int i = 0; i < sampleCount; ++i) {
            int ms = PingService::PingOnce(host, port);
            if (ms >= 0) samples.push_back(ms);
        }
        r.samplesOk = (int)samples.size();
        r.packetLossPct = sampleCount > 0 ? (100.0 * (sampleCount - r.samplesOk) / sampleCount) : 100.0;

        if (samples.empty()) {
            r.quality = Quality::Offline;
            return r;
        }

        long long sum = 0;
        r.minMs = samples[0];
        r.maxMs = samples[0];
        for (int v : samples) {
            sum += v;
            r.minMs = (std::min)(r.minMs, v);
            r.maxMs = (std::max)(r.maxMs, v);
        }
        r.avgMs = (int)(sum / (long long)samples.size());

        double variance = 0.0;
        for (int v : samples) { double d = v - r.avgMs; variance += d * d; }
        variance /= samples.size();
        r.jitterMs = std::sqrt(variance);

        if (r.packetLossPct > 20.0) r.quality = Quality::Poor;
        else if (r.avgMs <= 100 && r.packetLossPct == 0.0) r.quality = Quality::Excellent;
        else if (r.avgMs <= 250 && r.packetLossPct <= 5.0) r.quality = Quality::Good;
        else if (r.avgMs <= 500) r.quality = Quality::Fair;
        else r.quality = Quality::Poor;

        EstimateThroughput(r);
        return r;
    }

}
