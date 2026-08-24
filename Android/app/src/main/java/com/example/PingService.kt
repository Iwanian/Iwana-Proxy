package com.example

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.withContext
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket

object PingService {

    /**
     * Connects to a proxy's server on its port using a Socket.
     * Accurately measures TCP round-trip handshake time without DNS pollution.
     * Returns connection latency in milliseconds if successful, otherwise -1.
     */
    suspend fun ping(server: String, port: Int, timeoutMs: Int = 2000): Long = withContext(Dispatchers.IO) {
        var socket: Socket? = null
        try {
            val address = InetAddress.getByName(server)
            val socketAddress = InetSocketAddress(address, port)
            socket = Socket().apply {
                tcpNoDelay = true
                keepAlive = false
                reuseAddress = true
            }
            val startTime = System.currentTimeMillis()
            socket.connect(socketAddress, timeoutMs)
            val latency = (System.currentTimeMillis() - startTime).coerceAtLeast(1L)
            latency
        } catch (e: Exception) {
            -1L
        } finally {
            try {
                socket?.close()
            } catch (ignored: Exception) {}
        }
    }

    /**
     * Pings all supplied proxies in parallel with bounded concurrency.
     * Returns an updated list of [ProxyItem] with ping latency and alive status.
     */
    suspend fun pingAll(proxies: List<ProxyItem>): List<ProxyItem> = withContext(Dispatchers.IO) {
        val semaphore = Semaphore(24) // Concurrent socket tests for responsive scanning
        proxies.map { proxy ->
            async {
                semaphore.withPermit {
                    val latency = ping(proxy.server, proxy.port)
                    if (latency > 0) {
                        proxy.copy(ping = latency, isAlive = true, isScanned = true)
                    } else {
                        proxy.copy(ping = -1, isAlive = false, isScanned = true)
                    }
                }
            }
        }.awaitAll()
    }
}
