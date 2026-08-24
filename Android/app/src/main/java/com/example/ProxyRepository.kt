package com.example

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.IOException
import java.util.concurrent.TimeUnit

class ProxyRepository(
    private val client: OkHttpClient = OkHttpClient.Builder()
        .connectTimeout(5, TimeUnit.SECONDS)
        .readTimeout(5, TimeUnit.SECONDS)
        .writeTimeout(5, TimeUnit.SECONDS)
        .build()
) {

    private val primaryUrl = "https://raw.githubusercontent.com/Iwanian/Sub/main/Proxy-Channel-%2540I_w_a_n_a.txt"
    private val fallbackUrls = listOf(
        "https://c-mamad.ir/proxies/proxy.txt"
    )

    /**
     * Downloads the proxies raw content string from GitHub raw endpoint,
     * or falls back to alternative sources if the primary link fails or returns empty proxies.
     */
    suspend fun fetchProxiesRaw(): String = withContext(Dispatchers.IO) {
        val urlsToTry = listOf(primaryUrl) + fallbackUrls
        var lastException: Exception? = null

        for (url in urlsToTry) {
            try {
                val request = Request.Builder()
                    .url(url)
                    .header("User-Agent", "IwanaProxyAndroidApp")
                    .build()

                val bodyText = client.newCall(request).execute().use { response ->
                    if (!response.isSuccessful) {
                        throw IOException("HTTP response code: ${response.code} for $url")
                    }
                    response.body?.string() ?: ""
                }

                if (bodyText.isNotBlank()) {
                    val parsed = ProxyParser.parse(bodyText)
                    if (parsed.isNotEmpty()) {
                        return@withContext bodyText
                    }
                }
            } catch (e: Exception) {
                lastException = e
            }
        }

        throw lastException ?: IOException("Failed to fetch proxies from primary and fallback sources")
    }

    /**
     * Downloads the proxies file from GitHub raw and returns parsed, de-duplicated ProxyItems.
     */
    suspend fun fetchProxies(): List<ProxyItem> {
        val bodyString = fetchProxiesRaw()
        return ProxyParser.parse(bodyString)
    }
}
