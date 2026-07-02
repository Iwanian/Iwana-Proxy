package com.example

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.withContext
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import java.net.InetSocketAddress
import java.net.Socket
import android.util.Log

object PingService {

    /**
     * Connects to a proxy's server on its port using a Socket.
     * Returns connection latency in milliseconds if successful, otherwise -1.
     */
    suspend fun ping(server: String, port: Int, timeoutMs: Int = 2500): Long = withContext(Dispatchers.IO) {
        val startTime = System.currentTimeMillis()
        var socket: Socket? = null
        try {
            socket = Socket()
            val socketAddress = InetSocketAddress(server, port)
            socket.connect(socketAddress, timeoutMs)
            val latency = System.currentTimeMillis() - startTime
            latency
        } catch (e: Exception) {
            // Log.v("PingService", "Unable to connect to $server:$port - ${e.message}")
            -1L
        } finally {
            try {
                socket?.close()
            } catch (ignored: Exception) {}
        }
    }

    /**
     * Pings all supplied proxies in parallel with bounded concurrency to prevent thread starvation.
     * Returns an updated list of [ProxyItem] with ping latency and alive status.
     */
    suspend fun pingAll(proxies: List<ProxyItem>): List<ProxyItem> = withContext(Dispatchers.IO) {
        val semaphore = Semaphore(8) // Protect Dispatchers.IO from starvation
        proxies.map { proxy ->
            async {
                semaphore.withPermit {
                    val latency = ping(proxy.server, proxy.port)
                    if (latency >= 0) {
                        proxy.copy(ping = latency, isAlive = true, isScanned = true)
                    } else {
                        proxy.copy(ping = -1, isAlive = false, isScanned = true)
                    }
                }
            }
        }.awaitAll()
    }
}
