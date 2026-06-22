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

    private val proxyUrl = "https://raw.githubusercontent.com/Iwanian/Sub/main/Proxy-Channel-%2540I_w_a_n_a.txt"

    /**
     * Downloads the proxies file from GitHub raw and returns parsed, de-duplicated ProxyItems.
     */
    suspend fun fetchProxies(): List<ProxyItem> = withContext(Dispatchers.IO) {
        val request = Request.Builder()
            .url(proxyUrl)
            .header("User-Agent", "IwanaProxyAndroidApp")
            .build()
            
        client.newCall(request).execute().use { response ->
            if (!response.isSuccessful) {
                throw IOException("Unexpected HTTP response code: ${response.code}")
            }
            val bodyString = response.body?.string() ?: ""
            ProxyParser.parse(bodyString)
        }
    }
}
