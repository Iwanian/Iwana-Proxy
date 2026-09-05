// PingService.h — Real latency measurement via raw TCP connect timing.
// Mirrors Android PingService.kt: attempts a TCP connect to server:port and
// measures elapsed time; a connect success = "alive", failure/timeout = unreachable.
#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include "ProxyItem.h"

#pragma comment(lib, "ws2_32.lib")

namespace PingService {

    constexpr int kTimeoutMs = 3000;
    constexpr int kMaxConcurrent = 24; // matches Android's parallel cap

    // Narrow (ASCII) conversion helper — hostnames/ports are always ASCII.
    inline std::string ToNarrow(const std::wstring& w) {
        if (w.empty()) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], len, nullptr, nullptr);
        return out;
    }

    // Returns elapsed ms on success, -1 on failure/timeout.
    inline int PingOnce(const std::string& host, const std::string& port) {
        addrinfo hints = {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;

        if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0 || !result) {
            return -1;
        }

        SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (sock == INVALID_SOCKET) {
            freeaddrinfo(result);
            return -1;
        }

        // Non-blocking connect so we can enforce our own timeout.
        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);

        auto start = std::chrono::steady_clock::now();
        connect(sock, result->ai_addr, (int)result->ai_addrlen);
        freeaddrinfo(result);

        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(sock, &writeSet);
        timeval tv;
        tv.tv_sec = kTimeoutMs / 1000;
        tv.tv_usec = (kTimeoutMs % 1000) * 1000;

        int sel = select(0, nullptr, &writeSet, nullptr, &tv);
        int elapsedMs = -1;
        if (sel > 0 && FD_ISSET(sock, &writeSet)) {
            int err = 0;
            int errLen = sizeof(err);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&err, &errLen);
            if (err == 0) {
                auto end = std::chrono::steady_clock::now();
                elapsedMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            }
        }

        closesocket(sock);
        return elapsedMs;
    }

    // Pings every item in `items` (in place, by index) using a bounded thread pool.
    // Blocking call — intended to run on a background worker thread, not the UI thread.
    // `onProgress` (optional) is invoked after each item completes, with the index,
    // so the caller can post incremental UI updates.
    inline void PingAll(std::vector<ProxyItem>& items,
                         const std::function<void(size_t)>& onProgress = nullptr) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;

        std::atomic<size_t> nextIndex{0};
        size_t total = items.size();
        int workerCount = (std::min)((size_t)kMaxConcurrent, total);
        if (workerCount < 1) { WSACleanup(); return; }

        std::vector<std::thread> workers;
        for (int w = 0; w < workerCount; ++w) {
            workers.emplace_back([&]() {
                size_t i;
                while ((i = nextIndex.fetch_add(1)) < total) {
                    std::string host = ToNarrow(items[i].server);
                    std::string port = ToNarrow(items[i].port);
                    items[i].pingMs = PingOnce(host, port);
                    items[i].isScanned = true;
                    if (onProgress) onProgress(i);
                }
            });
        }
        for (auto& t : workers) t.join();

        WSACleanup();
    }

}
