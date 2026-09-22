// SpeedTester.h — Real per-proxy speed test.
// Metric set: average ping, jitter (population stddev of the samples around
// the mean — this is the actual formula in the Windows source, which is the
// source of truth per PORT_SPEC.md §13; note this differs slightly from the
// "mean absolute deviation between consecutive samples" phrasing in
// PORT_SPEC.md §4, but the shipped code computes stddev, so that's what's
// ported here), min/max, packet loss %, and a quality rating — computed from
// real TCP connect-timing samples via PingService::PingOnce.
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "PingService.h"

namespace SpeedTester {

    struct ParsedProxy { std::string server, port, secret; bool valid = false; };

    inline ParsedProxy ParseInput(const std::string& raw) {
        ParsedProxy out;
        std::string s = raw;
        // Trim whitespace
        while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
        while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
        if (s.empty()) return out;

        if (s.find("tg://proxy") != std::string::npos || s.find("/proxy?") != std::string::npos) {
            auto getParam = [&](const std::string& key) -> std::string {
                size_t q = s.find('?');
                if (q == std::string::npos) return "";
                std::string query = s.substr(q + 1);
                size_t pos = 0;
                while (pos < query.size()) {
                    size_t amp = query.find('&', pos);
                    std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
                    size_t eq = pair.find('=');
                    if (eq != std::string::npos && pair.substr(0, eq) == key) return pair.substr(eq + 1);
                    if (amp == std::string::npos) break;
                    pos = amp + 1;
                }
                return "";
            };
            out.server = getParam("server");
            out.port = getParam("port");
            out.secret = getParam("secret");
        } else {
            // server:port[:secret]
            size_t first = s.find(':');
            if (first == std::string::npos) return out;
            size_t second = s.find(':', first + 1);
            out.server = s.substr(0, first);
            out.port = (second == std::string::npos) ? s.substr(first + 1) : s.substr(first + 1, second - first - 1);
            if (second != std::string::npos) out.secret = s.substr(second + 1);
        }
        if (out.server.empty() || out.port.empty()) return out;
        for (char c : out.port) if (!std::isdigit((unsigned char)c)) return out;
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
        // see TelegramSpeedDisclaimer in Loc.h: these are estimates based on
        // network conditions, not guaranteed/measured Telegram speeds.
        double downloadMbps = 0.0;
        double uploadMbps = 0.0;
        std::string resolvedIp;
        int dnsLookupMs = -1;
    };

    // Estimated *real-world Telegram* download speed in MB/s — per product
    // formula: take the (heuristic) download Mbps figure, convert megabits
    // to megabytes (÷8), then apply a 0.3 real-world derating factor.
    inline double EstimateTelegramMBps(const Result& r) {
        if (r.downloadMbps <= 0.0) return 0.0;
        return (r.downloadMbps / 8.0) * 0.3;
    }

    // Heuristic bandwidth estimate from RTT/jitter/loss. Not a real throughput
    // measurement (an MTProto data transfer isn't performed) — same caveat the
    // app surfaces to the user via TelegramSpeedDisclaimer. Deliberately no
    // hard ceiling clamp (PORT_SPEC.md §4 — an earlier version's clamp bug
    // made every decent-ping connection saturate at the same number).
    inline void EstimateThroughput(Result& r) {
        if (r.avgMs < 0) { r.downloadMbps = 0.0; r.uploadMbps = 0.0; return; }
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
        const std::string& host = proxy.server;
        const std::string& port = proxy.port;

        // Resolve the host once up front — gives us the IP to display and a
        // real DNS lookup time, same info the "Connection info" panel shows.
        {
            addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
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
                    r.resolvedIp = ipBuf;
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
            r.minMs = std::min(r.minMs, v);
            r.maxMs = std::max(r.maxMs, v);
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
