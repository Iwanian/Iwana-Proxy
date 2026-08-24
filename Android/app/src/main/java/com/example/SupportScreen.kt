package com.example

import android.content.Intent
import android.net.Uri
import android.widget.Toast
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.filled.OpenInNew
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SupportScreen(
    viewModel: ProxyViewModel,
    onBackClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    val selectedLanguage by viewModel.selectedLanguage.collectAsStateWithLifecycle()
    val context = LocalContext.current
    val clipboardManager = LocalClipboardManager.current

    val lang = selectedLanguage ?: "fa"
    val isFa = lang == "fa"
    val isRu = lang == "ru"

    // Text translations based on language
    val titleText = when {
        isFa -> "حمایت از ما"
        isRu -> "Поддержать нас"
        else -> "Support Us"
    }

    val bannerText = when {
        isFa -> "تمام امکانات Iwana Proxy رایگان بوده، رایگان هست و رایگان خواهد ماند."
        isRu -> "Все функции Iwana Proxy были, есть и всегда будут бесплатными."
        else -> "All features of Iwana Proxy are free, always have been, and always will be."
    }

    val cryptoHeader = when {
        isFa -> "💰 حمایت با ارز دیجیتال"
        isRu -> "💰 Поддержка криптовалютой"
        else -> "💰 Support with Cryptocurrency"
    }

    val trxLabel = when {
        isFa -> "TRX (TRON) (پیشنهادی)"
        isRu -> "TRX (TRON) (Рекомендуемый)"
        else -> "TRX (TRON) (Recommended)"
    }

    val networkWarning = when {
        isFa -> "⚠️ لطفاً هنگام انتقال، شبکه را دقیقاً مطابق موارد بالا انتخاب کنید."
        isRu -> "⚠️ Пожалуйста, выбирайте сеть точно в соответствии с указанной выше при переводе."
        else -> "⚠️ Please ensure you select the exact network specified above when transferring."
    }

    val starHeader = when {
        isFa -> "⭐ حمایت از پروژه"
        isRu -> "⭐ Поддержка проекта"
        else -> "⭐ Support the Project"
    }

    val starSubtext = when {
        isFa -> "با دادن یک Star در GitHub به رشد پروژه کمک کنید:"
        isRu -> "Помогите проекту расти, поставив Star на GitHub:"
        else -> "Help the project grow by giving a Star on GitHub:"
    }

    val telegramSubtext = when {
        isFa -> "ما را در تلگرام دنبال کنید:"
        isRu -> "Подписывайтесь на нас в Telegram:"
        else -> "Follow us on Telegram:"
    }

    val thankYouText = when {
        isFa -> "ممنون که از Iwana Proxy حمایت میکنید. ❤️"
        isRu -> "Спасибо за поддержку Iwana Proxy! ❤️"
        else -> "Thank you for supporting Iwana Proxy! ❤️"
    }

    val copyToastFormat = when {
        isFa -> "آدرس %s کپی شد"
        isRu -> "Адрес %s скопирован"
        else -> "%s address copied"
    }

    val copyToClipboard: (String, String) -> Unit = { label, text ->
        clipboardManager.setText(AnnotatedString(text))
        Toast.makeText(context, String.format(copyToastFormat, label), Toast.LENGTH_SHORT).show()
    }

    val openUrl: (String) -> Unit = { url ->
        try {
            val intent = Intent(Intent.ACTION_VIEW, Uri.parse(url)).apply {
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            }
            context.startActivity(intent)
        } catch (e: Exception) {
            e.printStackTrace()
            try {
                val browserIntent = Intent.makeMainSelectorActivity(Intent.ACTION_MAIN, Intent.CATEGORY_APP_BROWSER).apply {
                    data = Uri.parse(url)
                    flags = Intent.FLAG_ACTIVITY_NEW_TASK
                }
                context.startActivity(browserIntent)
            } catch (ex: Exception) {
                ex.printStackTrace()
                Toast.makeText(context, "Could not open link", Toast.LENGTH_SHORT).show()
            }
        }
    }

    Scaffold(
        topBar = {
            CenterAlignedTopAppBar(
                title = {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Favorite,
                            contentDescription = null,
                            tint = Color(0xFFE91E63)
                        )
                        Text(
                            text = titleText,
                            fontWeight = FontWeight.Bold,
                            style = MaterialTheme.typography.titleLarge
                        )
                    }
                },
                navigationIcon = {
                    IconButton(onClick = onBackClick) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = "Back"
                        )
                    }
                },
                colors = TopAppBarDefaults.centerAlignedTopAppBarColors(
                    containerColor = MaterialTheme.colorScheme.background
                )
            )
        },
        modifier = modifier
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 20.dp, vertical = 16.dp),
            verticalArrangement = Arrangement.spacedBy(18.dp)
        ) {
            // Intro banner card
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.45f)
                ),
                shape = RoundedCornerShape(16.dp)
            ) {
                Text(
                    text = bannerText,
                    style = MaterialTheme.typography.bodyLarge,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.primary,
                    textAlign = TextAlign.Center,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp)
                )
            }

            // 1. Crypto Donation Section Header
            Text(
                text = cryptoHeader,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.onSurface
            )

            // Crypto Wallet Items
            CryptoWalletCard(
                currency = "USDT (Polygon)",
                address = "0x3d76c651ee3f76ac468e2769c9d9fbfcaa545088",
                onCopy = { copyToClipboard("USDT", "0x3d76c651ee3f76ac468e2769c9d9fbfcaa545088") }
            )

            CryptoWalletCard(
                currency = "BTC (Ethereum)",
                address = "0x3d76c651ee3f76ac468e2769c9d9fbfcaa545088",
                onCopy = { copyToClipboard("BTC", "0x3d76c651ee3f76ac468e2769c9d9fbfcaa545088") }
            )

            CryptoWalletCard(
                currency = trxLabel,
                address = "TFaCWNT4N9wHJ2e1Z9MSuz1waUoMseRGqx",
                onCopy = { copyToClipboard("TRX", "TFaCWNT4N9wHJ2e1Z9MSuz1waUoMseRGqx") }
            )

            // Network Warning Notice
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.35f)
                ),
                shape = RoundedCornerShape(12.dp)
            ) {
                Text(
                    text = networkWarning,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.error,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.padding(14.dp)
                )
            }

            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            // 2. Star & Community Section Header
            Text(
                text = starHeader,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.onSurface
            )

            Text(
                text = starSubtext,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            // GitHub Official Link Card
            OutlinedCard(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { openUrl("https://github.com/Iwanian/Iwana-Proxy") },
                shape = RoundedCornerShape(16.dp)
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(14.dp)
                    ) {
                        Icon(
                            painter = painterResource(id = R.drawable.ic_github),
                            contentDescription = "GitHub",
                            tint = MaterialTheme.colorScheme.onSurface,
                            modifier = Modifier.size(28.dp)
                        )
                        Text(
                            text = "GitHub",
                            fontWeight = FontWeight.Bold,
                            style = MaterialTheme.typography.bodyLarge
                        )
                    }
                    Icon(
                        imageVector = Icons.Default.OpenInNew,
                        contentDescription = "Open Link",
                        modifier = Modifier.size(20.dp),
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            // Telegram subtext
            Text(
                text = telegramSubtext,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            // Telegram Official Link Card
            OutlinedCard(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { TelegramLauncher.launchChannel(context) },
                shape = RoundedCornerShape(16.dp)
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(14.dp)
                    ) {
                        Icon(
                            painter = painterResource(id = R.drawable.ic_telegram),
                            contentDescription = "Telegram",
                            tint = Color(0xFF2AABEE),
                            modifier = Modifier.size(28.dp)
                        )
                        Text(
                            text = "Telegram",
                            fontWeight = FontWeight.Bold,
                            style = MaterialTheme.typography.bodyLarge
                        )
                    }
                    Icon(
                        imageVector = Icons.Default.OpenInNew,
                        contentDescription = "Open Link",
                        modifier = Modifier.size(20.dp),
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Final Gratitude Note
            Text(
                text = thankYouText,
                style = MaterialTheme.typography.bodyLarge,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.primary,
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth()
            )

            Spacer(modifier = Modifier.height(24.dp))
        }
    }
}

@Composable
private fun CryptoWalletCard(
    currency: String,
    address: String,
    onCopy: () -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f)
        ),
        shape = RoundedCornerShape(14.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                text = currency,
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.primary
            )

            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text(
                    text = address,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.weight(1f),
                    maxLines = 1
                )

                IconButton(
                    onClick = onCopy,
                    modifier = Modifier.size(36.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.ContentCopy,
                        contentDescription = "Copy Address",
                        modifier = Modifier.size(20.dp),
                        tint = MaterialTheme.colorScheme.primary
                    )
                }
            }
        }
    }
}
