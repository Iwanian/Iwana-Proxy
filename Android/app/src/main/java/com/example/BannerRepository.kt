package com.example

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONArray
import java.io.IOException
import java.util.concurrent.TimeUnit

data class BannerItem(
    val imageUrl: String,
    val targetLink: String? = null
)

class BannerRepository(
    private val client: OkHttpClient = OkHttpClient.Builder()
        .connectTimeout(5, TimeUnit.SECONDS)
        .readTimeout(5, TimeUnit.SECONDS)
        .build()
) {
    private val apiEndpoints = listOf(
        "https://api.github.com/repos/Iwanian/Sub/contents/pic",
        "https://api.github.com/repos/Iwanian/Sub/contents",
        "https://api.github.com/repos/Iwanian/Iwana-Proxy/contents/pic"
    )

    private val rawFolderBaseUrls = listOf(
        "https://raw.githubusercontent.com/Iwanian/Sub/main/pic",
        "https://raw.githubusercontent.com/Iwanian/Sub/main",
        "https://raw.githubusercontent.com/Iwanian/Iwana-Proxy/main/pic"
    )

    suspend fun fetchBannerItems(): List<BannerItem> = withContext(Dispatchers.IO) {
        val bannerItems = mutableListOf<BannerItem>()
        val txtFiles = mutableMapOf<String, String>() // baseName -> rawUrl
        val rawImageUrls = mutableListOf<Pair<String, String>>() // baseName -> imageUrl
        var generalLinks: List<String> = emptyList()

        // 1. Attempt GitHub API folder inspection
        for (apiUrl in apiEndpoints) {
            try {
                val request = Request.Builder()
                    .url(apiUrl)
                    .header("User-Agent", "IwanaProxyAndroidApp")
                    .header("Accept", "application/vnd.github.v3+json")
                    .build()

                val jsonString = client.newCall(request).execute().use { response ->
                    if (response.isSuccessful) response.body?.string() else null
                }

                if (!jsonString.isNullOrBlank()) {
                    val array = JSONArray(jsonString)
                    for (i in 0 until array.length()) {
                        val item = array.getJSONObject(i)
                        val type = item.optString("type")
                        val name = item.optString("name")
                        val downloadUrl = item.optString("download_url")

                        if (type == "file") {
                            val baseName = name.substringBeforeLast('.')
                            val rawUrl = if (downloadUrl.isNotBlank() && downloadUrl != "null") {
                                downloadUrl
                            } else {
                                "${rawFolderBaseUrls.first()}/$name"
                            }

                            if (isImageFile(name)) {
                                if (rawImageUrls.none { it.second == rawUrl }) {
                                    rawImageUrls.add(baseName to rawUrl)
                                }
                            } else if (name.lowercase().endsWith(".txt")) {
                                txtFiles[baseName.lowercase()] = rawUrl
                            }
                        }
                    }
                    if (rawImageUrls.isNotEmpty()) {
                        break // Found image files successfully
                    }
                }
            } catch (e: Exception) {
                // Continue to next endpoint or fallback
            }
        }

        // Fetch general links if any general link file exists
        val generalLinkUrl = txtFiles["link"] ?: txtFiles["links"] ?: txtFiles["url"] ?: txtFiles["urls"]
        if (generalLinkUrl != null) {
            val generalText = fetchTextContent(generalLinkUrl)
            if (!generalText.isNullOrBlank()) {
                generalLinks = generalText.lines()
                    .map { cleanUrl(it) }
                    .filter { it.isNotBlank() }
            }
        }

        if (rawImageUrls.isNotEmpty()) {
            val deferredItems = rawImageUrls.mapIndexed { index, (baseName, imgUrl) ->
                async {
                    val txtUrl = txtFiles[baseName.lowercase()]
                    var linkText = if (txtUrl != null) fetchTextContent(txtUrl) else null
                    if (linkText.isNullOrBlank() && generalLinks.isNotEmpty()) {
                        linkText = generalLinks.getOrNull(index) ?: generalLinks.firstOrNull()
                    }
                    BannerItem(
                        imageUrl = imgUrl,
                        targetLink = linkText?.let { cleanUrl(it) }
                    )
                }
            }
            bannerItems.addAll(deferredItems.awaitAll())
        } else {
            // Fallback direct probe across common names and folder locations
            val commonNames = listOf(
                "1.png", "1.jpg", "1.jpeg", "1.webp",
                "2.png", "2.jpg", "2.jpeg", "2.webp",
                "3.png", "3.jpg", "3.jpeg", "3.webp",
                "banner.png", "banner.jpg", "banner.jpeg", "banner.webp",
                "banner1.png", "banner1.jpg", "banner2.png", "banner2.jpg",
                "pic.png", "pic.jpg", "pic1.png", "pic1.jpg"
            )

            for (baseUrl in rawFolderBaseUrls) {
                val fallbackGeneralLink = fetchTextContent("$baseUrl/link.txt")
                    ?: fetchTextContent("$baseUrl/links.txt")
                if (!fallbackGeneralLink.isNullOrBlank()) {
                    generalLinks = fallbackGeneralLink.lines()
                        .map { cleanUrl(it) }
                        .filter { it.isNotBlank() }
                }

                val deferredItems = commonNames.mapIndexed { index, name ->
                    async {
                        val imgUrl = "$baseUrl/$name"
                        if (checkUrlExists(imgUrl)) {
                            val baseName = name.substringBeforeLast('.')
                            val txtUrl = "$baseUrl/$baseName.txt"
                            var linkText = fetchTextContent(txtUrl)
                            if (linkText.isNullOrBlank() && generalLinks.isNotEmpty()) {
                                linkText = generalLinks.getOrNull(index) ?: generalLinks.firstOrNull()
                            }
                            BannerItem(
                                imageUrl = imgUrl,
                                targetLink = linkText?.let { cleanUrl(it) }
                            )
                        } else null
                    }
                }
                val found = deferredItems.awaitAll().filterNotNull()
                if (found.isNotEmpty()) {
                    bannerItems.addAll(found)
                    break
                }
            }
        }

        return@withContext bannerItems
    }

    private fun checkUrlExists(url: String): Boolean {
        return try {
            val request = Request.Builder()
                .url(url)
                .header("User-Agent", "IwanaProxyAndroidApp")
                .head()
                .build()
            client.newCall(request).execute().use { response ->
                response.isSuccessful
            }
        } catch (e: Exception) {
            false
        }
    }

    private fun fetchTextContent(url: String): String? {
        return try {
            val request = Request.Builder()
                .url(url)
                .header("User-Agent", "IwanaProxyAndroidApp")
                .build()
            client.newCall(request).execute().use { response ->
                if (response.isSuccessful) {
                    val body = response.body?.string()?.trim()
                    if (!body.isNullOrBlank()) {
                        val cleanBody = body.removePrefix("\uFEFF").trim()
                        val lines = cleanBody.lines().map { cleanUrl(it) }.filter { it.isNotBlank() }
                        lines.firstOrNull { line ->
                            line.startsWith("http://", ignoreCase = true) ||
                            line.startsWith("https://", ignoreCase = true) ||
                            line.startsWith("tg://", ignoreCase = true) ||
                            line.startsWith("t.me/", ignoreCase = true) ||
                            line.startsWith("@")
                        } ?: lines.firstOrNull()
                    } else null
                } else null
            }
        } catch (e: Exception) {
            null
        }
    }

    private fun cleanUrl(raw: String): String {
        return raw.trim()
            .removePrefix("\uFEFF")
            .removePrefix("\"")
            .removeSuffix("\"")
            .removePrefix("'")
            .removeSuffix("'")
            .trim()
    }

    private fun isImageFile(fileName: String): Boolean {
        val lower = fileName.lowercase()
        return lower.endsWith(".png") || lower.endsWith(".jpg") ||
               lower.endsWith(".jpeg") || lower.endsWith(".webp") ||
               lower.endsWith(".gif")
    }
}
