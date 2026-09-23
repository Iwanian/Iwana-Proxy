// PingService.swift — Real latency measurement via raw TCP connect timing.
// Ported 1:1 from the Windows build's PingService.h (WinSock2 -> Darwin
// sockets), which itself mirrors the Android app's PingService.kt: attempts
// a TCP connect to server:port and measures elapsed time; a connect success
// = "alive", failure/timeout = unreachable.
import Foundation
#if canImport(Darwin)
import Darwin
#endif

enum PingService {

    static let timeoutMs = 3000
    static let maxConcurrent = 24 // matches Android's / Windows build's parallel cap

    /// Returns elapsed ms on success, -1 on failure/timeout. Blocking — call
    /// from a background thread/task, never from the main actor.
    static func pingOnce(host: String, port: String) -> Int {
        var hints = addrinfo()
        hints.ai_family = AF_UNSPEC
        hints.ai_socktype = SOCK_STREAM
        var resultPtr: UnsafeMutablePointer<addrinfo>?

        let gaiStatus = getaddrinfo(host, port, &hints, &resultPtr)
        guard gaiStatus == 0, let addrInfo = resultPtr else { return -1 }
        defer { freeaddrinfo(resultPtr) }

        let sock = socket(addrInfo.pointee.ai_family, addrInfo.pointee.ai_socktype, addrInfo.pointee.ai_protocol)
        guard sock >= 0 else { return -1 }
        defer { close(sock) }

        // Non-blocking so we can enforce our own timeout via select().
        let flags = fcntl(sock, F_GETFL, 0)
        _ = fcntl(sock, F_SETFL, flags | O_NONBLOCK)

        let start = DispatchTime.now()
        _ = connect(sock, addrInfo.pointee.ai_addr, addrInfo.pointee.ai_addrlen)

        var writeSet = fd_set()
        darwinFDZero(&writeSet)
        darwinFDSet(sock, &writeSet)

        var tv = timeval(tv_sec: timeoutMs / 1000, tv_usec: Int32((timeoutMs % 1000) * 1000))
        let sel = select(sock + 1, nil, &writeSet, nil, &tv)

        guard sel > 0, darwinFDIsSet(sock, &writeSet) else { return -1 }

        var err: Int32 = 0
        var errLen = socklen_t(MemoryLayout<Int32>.size)
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &errLen)
        guard err == 0 else { return -1 }

        let end = DispatchTime.now()
        return Int((end.uptimeNanoseconds - start.uptimeNanoseconds) / 1_000_000)
    }

    /// Pings every item in `items` using a bounded worker pool and returns a
    /// new array with `pingMs`/`isScanned` filled in (value semantics, so
    /// this is safe to call from a detached Task without inout-capture
    /// issues). `onProgress(index)` fires after each item completes, so the
    /// caller can post incremental UI updates. Blocking — call from a
    /// background task, not the main actor.
    static func pingAll(_ items: [ProxyItem], onProgress: ((Int) -> Void)? = nil) -> [ProxyItem] {
        var items = items
        let total = items.count
        guard total > 0 else { return items }
        let workerCount = min(maxConcurrent, total)

        let nextIndex = NSLock()
        var cursor = 0
        func claimNext() -> Int? {
            nextIndex.lock(); defer { nextIndex.unlock() }
            guard cursor < total else { return nil }
            let i = cursor
            cursor += 1
            return i
        }

        // Results are computed in parallel, written back sequentially.
        var results = [Int: Int](minimumCapacity: total)
        let resultsLock = NSLock()
        let group = DispatchGroup()
        let queue = DispatchQueue(label: "iwanaproxy.ping", attributes: .concurrent)
        let snapshot = items // read-only inside the workers

        for _ in 0..<workerCount {
            group.enter()
            queue.async {
                while let i = claimNext() {
                    let host = snapshot[i].server
                    let port = snapshot[i].port
                    let ms = pingOnce(host: host, port: port)
                    resultsLock.lock()
                    results[i] = ms
                    resultsLock.unlock()
                    onProgress?(i)
                }
                group.leave()
            }
        }
        group.wait()

        for (i, ms) in results {
            items[i].pingMs = ms
            items[i].isScanned = true
        }
        return items
    }
}

// MARK: - fd_set helpers
// Swift doesn't import the FD_SET/FD_ZERO/FD_ISSET macros, so re-implement
// the same bit-twiddling libc uses under the hood.
private func darwinFDZero(_ set: inout fd_set) {
    set.fds_bits = (0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)
}

private func darwinFDSet(_ fd: Int32, _ set: inout fd_set) {
    let intOffset = Int(fd / 32)
    let bitOffset = fd % 32
    let mask: Int32 = 1 << bitOffset
    withUnsafeMutablePointer(to: &set.fds_bits) { ptr in
        ptr.withMemoryRebound(to: Int32.self, capacity: 32) { bits in
            bits[intOffset] |= mask
        }
    }
}

private func darwinFDIsSet(_ fd: Int32, _ set: inout fd_set) -> Bool {
    let intOffset = Int(fd / 32)
    let bitOffset = fd % 32
    let mask: Int32 = 1 << bitOffset
    return withUnsafeMutablePointer(to: &set.fds_bits) { ptr -> Bool in
        ptr.withMemoryRebound(to: Int32.self, capacity: 32) { bits in
            (bits[intOffset] & mask) != 0
        }
    }
}
