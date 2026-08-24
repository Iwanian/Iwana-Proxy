package com.example

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.widget.Toast
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import coil.compose.AsyncImage
import coil.imageLoader
import coil.request.CachePolicy
import coil.request.ImageRequest
import coil.request.SuccessResult
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun AdBannerSlideshow(
    banners: List<BannerItem>,
    modifier: Modifier = Modifier
) {
    if (banners.isEmpty()) return

    val context = LocalContext.current
    var isImageLoadedAndReady by remember(banners) { mutableStateOf(false) }

    // Pre-verify that the image is actually downloaded and ready before showing any card or frame
    LaunchedEffect(banners) {
        val firstUrl = banners.firstOrNull()?.imageUrl
        if (!firstUrl.isNullOrBlank()) {
            withContext(Dispatchers.IO) {
                try {
                    val request = ImageRequest.Builder(context)
                        .data(firstUrl)
                        .diskCachePolicy(CachePolicy.ENABLED)
                        .memoryCachePolicy(CachePolicy.ENABLED)
                        .build()
                    val result = context.imageLoader.execute(request)
                    if (result is SuccessResult) {
                        isImageLoadedAndReady = true
                    }
                } catch (e: Exception) {
                    // Do nothing if failed or timed out
                }
            }
        }
    }

    // Do not show any card, white box, or container until image is completely ready
    if (!isImageLoadedAndReady) return

    val pagerState = rememberPagerState(
        initialPage = 0,
        pageCount = { banners.size }
    )

    // Auto-advance slideshow timer every 5 seconds when the user is not actively swiping
    LaunchedEffect(banners.size, pagerState) {
        if (banners.size > 1) {
            while (true) {
                delay(5000L)
                if (!pagerState.isScrollInProgress) {
                    val nextPage = (pagerState.currentPage + 1) % banners.size
                    pagerState.animateScrollToPage(nextPage)
                }
            }
        }
    }

    AnimatedVisibility(
        visible = isImageLoadedAndReady,
        enter = fadeIn()
    ) {
        Card(
            modifier = modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 6.dp),
            shape = RoundedCornerShape(16.dp),
            colors = CardDefaults.cardColors(
                containerColor = Color.Transparent
            ),
            elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
        ) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .wrapContentHeight()
                    .clip(RoundedCornerShape(16.dp))
            ) {
                HorizontalPager(
                    state = pagerState,
                    modifier = Modifier
                        .fillMaxWidth()
                        .wrapContentHeight()
                ) { pageIndex ->
                    val banner = banners.getOrNull(pageIndex)
                    if (banner != null && banner.imageUrl.isNotBlank()) {
                        AsyncImage(
                            model = ImageRequest.Builder(context)
                                .data(banner.imageUrl)
                                .crossfade(true)
                                .diskCachePolicy(CachePolicy.ENABLED)
                                .memoryCachePolicy(CachePolicy.ENABLED)
                                .build(),
                            contentDescription = "Ad Banner",
                            contentScale = ContentScale.FillWidth,
                            modifier = Modifier
                                .fillMaxWidth()
                                .wrapContentHeight()
                                .clickable {
                                    openBannerLink(context, banner.targetLink)
                                }
                        )
                    }
                }

                // Indicator dots if multiple banners
                if (banners.size > 1) {
                    Row(
                        modifier = Modifier
                            .align(Alignment.BottomCenter)
                            .padding(bottom = 8.dp)
                            .background(
                                color = Color.Black.copy(alpha = 0.45f),
                                shape = RoundedCornerShape(12.dp)
                            )
                            .padding(horizontal = 8.dp, vertical = 4.dp),
                        horizontalArrangement = Arrangement.spacedBy(4.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        banners.indices.forEach { index ->
                            Box(
                                modifier = Modifier
                                    .size(if (index == pagerState.currentPage) 8.dp else 5.dp)
                                    .background(
                                        color = if (index == pagerState.currentPage) Color.White else Color.White.copy(alpha = 0.5f),
                                        shape = CircleShape
                                    )
                            )
                        }
                    }
                }
            }
        }
    }
}

/**
 * Handles launching the banner link with fallback support for Telegram and web browsers.
 */
private fun openBannerLink(context: Context, link: String?) {
    val raw = link?.trim()
        ?.removePrefix("\uFEFF")
        ?.removePrefix("\"")
        ?.removeSuffix("\"")
        ?.removePrefix("'")
        ?.removeSuffix("'")
        ?.trim()

    val targetUrl = when {
        raw.isNullOrBlank() -> "https://t.me/I_w_a_n_a"
        raw.startsWith("http://", ignoreCase = true) ||
        raw.startsWith("https://", ignoreCase = true) -> raw
        raw.startsWith("tg://", ignoreCase = true) -> raw
        raw.startsWith("t.me/", ignoreCase = true) ||
        raw.startsWith("telegram.me/", ignoreCase = true) -> "https://$raw"
        raw.startsWith("@") -> "https://t.me/${raw.removePrefix("@")}"
        raw.contains(".") -> "https://$raw"
        else -> "https://t.me/$raw"
    }

    try {
        val intent = Intent(Intent.ACTION_VIEW, Uri.parse(targetUrl)).apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        context.startActivity(intent)
    } catch (e: Exception) {
        // Fallback for custom tg:// scheme if Telegram is not installed
        if (targetUrl.startsWith("tg://", ignoreCase = true)) {
            val domain = if (targetUrl.contains("domain=")) {
                targetUrl.substringAfter("domain=").substringBefore("&")
            } else {
                "I_w_a_n_a"
            }
            try {
                val fallbackIntent = Intent(Intent.ACTION_VIEW, Uri.parse("https://t.me/$domain")).apply {
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                }
                context.startActivity(fallbackIntent)
            } catch (e2: Exception) {
                Toast.makeText(context, "امکان باز کردن لینک وجود ندارد", Toast.LENGTH_SHORT).show()
            }
        } else {
            Toast.makeText(context, "امکان باز کردن لینک وجود ندارد", Toast.LENGTH_SHORT).show()
        }
    }
}
