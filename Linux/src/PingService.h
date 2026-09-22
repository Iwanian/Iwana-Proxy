// PingService.h — Real latency measurement via raw TCP connect timing.
// Same algorithm as the Windows build (non-blocking connect + select with a
// 3000ms timeout, 24-way thread pool over a shared atomic work index); only
// the socket API is swapped for POSIX (sys/socket.h) instead of Winsock2.
#pragma once
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <algorithm>
#include <cerrno>
#include "ProxyItem.h"

namespace PingService {

    constexpr int kTimeoutMs = 3000;
    constexpr int kMaxConcurrent = 24; // matches the Android/Windows parallel cap

    // Returns elapsed ms on success, -1 on failure/timeout.
    inline int PingOnce(const std::string& host, const std::string& port) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;

        if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0 || !result) {
            return -1;
        }

        int sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (sock < 0) {
            freeaddrinfo(result);
            return -1;
        }

        // Non-blocking connect so we can enforce our own timeout.
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        auto start = std::chrono::steady_clock::now();
        int rc = connect(sock, result->ai_addr, result->ai_addrlen);
        freeaddrinfo(result);

        int elapsedMs = -1;
        if (rc == 0) {
            // Connected immediately (e.g. localhost).
            auto end = std::chrono::steady_clock::now();
            elapsedMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        } else if (errno == EINPROGRESS) {
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(sock, &writeSet);
            timeval tv;
            tv.tv_sec = kTimeoutMs / 1000;
            tv.tv_usec = (kTimeoutMs % 1000) * 1000;

            int sel = select(sock + 1, nullptr, &writeSet, nullptr, &tv);
            if (sel > 0 && FD_ISSET(sock, &writeSet)) {
                int err = 0;
                socklen_t errLen = sizeof(err);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &errLen);
                if (err == 0) {
                    auto end = std::chrono::steady_clock::now();
                    elapsedMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                }
            }
        }

        close(sock);
        return elapsedMs;
    }

    // Pings every item in `items` (in place, by index) using a bounded thread pool.
    // Blocking call — intended to run on a background worker thread, not the UI thread.
    // `onProgress` (optional) is invoked after each item completes, with the index,
    // so the caller can post incremental UI updates (e.g. via g_idle_add).
    inline void PingAll(std::vector<ProxyItem>& items,
                         const std::function<void(size_t)>& onProgress = nullptr) {
        std::atomic<size_t> nextIndex{0};
        size_t total = items.size();
        size_t workerCount = std::min((size_t)kMaxConcurrent, total);
        if (workerCount < 1) return;

        std::vector<std::thread> workers;
        for (size_t w = 0; w < workerCount; ++w) {
            workers.emplace_back([&]() {
                size_t i;
                while ((i = nextIndex.fetch_add(1)) < total) {
                    items[i].pingMs = PingOnce(items[i].server, items[i].port);
                    items[i].isScanned = true;
                    if (onProgress) onProgress(i);
                }
            });
        }
        for (auto& t : workers) t.join();
    }

}
