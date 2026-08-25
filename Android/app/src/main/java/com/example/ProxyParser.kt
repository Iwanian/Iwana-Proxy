package com.example

import android.net.Uri
import android.util.Log

object ProxyParser {
    
    // Regular expression to aggressively find Telegram MTProto proxies in raw file text
    private val PROXY_REGEX = Regex("""(tg://proxy\?[^\s"']+|https?://(t\.me|telegram\.me)/proxy\?[^\s"']+)""")

    fun parse(rawText: String): List<ProxyItem> {
        val proxies = mutableListOf<ProxyItem>()
        var idCounter = 1
        
        val lines = rawText.lines()
        for (line in lines) {
            val trimmedLine = line.trim()
            if (trimmedLine.isEmpty()) continue

            val match = PROXY_REGEX.find(trimmedLine)
            if (match != null) {
                var rawLink = match.value.trim()
                val matchEnd = match.range.last + 1
                val matchStart = match.range.first
                
                val afterLink = if (matchEnd < trimmedLine.length) trimmedLine.substring(matchEnd).trim() else ""
                val beforeLink = if (matchStart > 0) trimmedLine.substring(0, matchStart).trim() else ""

                // 1. Check for '⭐' / '★' (For Download marker)
                if (rawLink.endsWith(".") && !rawLink.endsWith(".txt")) {
                    rawLink = rawLink.removeSuffix(".").trim()
                }

                val isForDownload = trimmedLine.contains("⭐")
                    || trimmedLine.contains("★")
                    || trimmedLine.contains("\u2B50")
                    || trimmedLine.contains("\u2605")
                    || trimmedLine.contains("✨")
                    || afterLink.contains("⭐")
                    || afterLink.contains("★")
                    || afterLink.contains("\u2B50")
                    || afterLink.contains("\u2605")
                    || beforeLink.contains("⭐")
                    || beforeLink.contains("★")

                // 2. Check for 'ru' (Russian proxy marker)
                val isRussian = afterLink.contains(Regex("""\b(ru|rus|russia|russian)\b""", RegexOption.IGNORE_CASE))
                    || beforeLink.contains(Regex("""\b(ru|rus|russia|russian)\b""", RegexOption.IGNORE_CASE))
                    || rawLink.contains("#ru", ignoreCase = true)
                    || rawLink.contains("#rus", ignoreCase = true)
                    || rawLink.contains("#russia", ignoreCase = true)
                    || rawLink.contains("&tag=ru", ignoreCase = true)
                    || afterLink.startsWith("ru", ignoreCase = true)
                    || afterLink.endsWith("ru", ignoreCase = true)
                    || trimmedLine.contains("🇷🇺")
                    || trimmedLine.contains("روسی")

                try {
                    val uri = Uri.parse(rawLink)
                    val server = uri.getQueryParameter("server") ?: ""
                    val portStr = uri.getQueryParameter("port") ?: ""
                    val secret = uri.getQueryParameter("secret") ?: ""
                    
                    if (server.isNotEmpty() && portStr.isNotEmpty()) {
                        val port = portStr.toIntOrNull() ?: continue
                        proxies.add(
                            ProxyItem(
                                id = idCounter++,
                                server = server,
                                port = port,
                                secret = secret,
                                link = rawLink,
                                ping = -1,
                                isAlive = false,
                                isFavorite = false,
                                isScanned = false,
                                isForDownload = isForDownload,
                                isRussian = isRussian
                            )
                        )
                    }
                } catch (e: Exception) {
                    Log.e("ProxyParser", "Error parsing link: $rawLink", e)
                }
            }
        }
        
        // Fallback for single-line unseparated strings
        if (proxies.isEmpty()) {
            val matches = PROXY_REGEX.findAll(rawText)
            for (match in matches) {
                var rawLink = match.value.trim()
                if (rawLink.endsWith(".") && !rawLink.endsWith(".txt")) {
                    rawLink = rawLink.removeSuffix(".").trim()
                }
                val isForDownload = rawLink.contains("⭐") || rawLink.contains("★")
                val isRussian = rawLink.contains("#ru", ignoreCase = true)
                try {
                    val uri = Uri.parse(rawLink)
                    val server = uri.getQueryParameter("server") ?: ""
                    val portStr = uri.getQueryParameter("port") ?: ""
                    val secret = uri.getQueryParameter("secret") ?: ""
                    
                    if (server.isNotEmpty() && portStr.isNotEmpty()) {
                        val port = portStr.toIntOrNull() ?: continue
                        proxies.add(
                            ProxyItem(
                                id = idCounter++,
                                server = server,
                                port = port,
                                secret = secret,
                                link = rawLink,
                                ping = -1,
                                isAlive = false,
                                isFavorite = false,
                                isScanned = false,
                                isForDownload = isForDownload,
                                isRussian = isRussian
                            )
                        )
                    }
                } catch (e: Exception) {
                    Log.e("ProxyParser", "Error parsing link: $rawLink", e)
                }
            }
        }

        // De-duplicate items based on server and port for cleaner presentation while preserving attributes
        return proxies.distinctBy { "${it.server}:${it.port}" }
    }
}

