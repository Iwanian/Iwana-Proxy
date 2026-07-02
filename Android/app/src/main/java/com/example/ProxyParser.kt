package com.example

import android.net.Uri
import android.util.Log

object ProxyParser {
    
    // Regular expression to aggressively find Telegram MTProto proxies in raw file text
    private val PROXY_REGEX = Regex("""(tg://proxy\?[^\s"']+|https?://(t\.me|telegram\.me)/proxy\?[^\s"']+)""")

    fun parse(rawText: String): List<ProxyItem> {
        val proxies = mutableListOf<ProxyItem>()
        var idCounter = 1
        
        val matches = PROXY_REGEX.findAll(rawText)
        for (match in matches) {
            val link = match.value.trim()
            try {
                val uri = Uri.parse(link)
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
                            link = link,
                            ping = -1,
                            isAlive = false,
                            isFavorite = false
                        )
                    )
                }
            } catch (e: Exception) {
                Log.e("ProxyParser", "Error parsing link: $link", e)
            }
        }
        
        // De-duplicate items based on server and port for cleaner presentation
        return proxies.distinctBy { "${it.server}:${it.port}" }
    }
}
