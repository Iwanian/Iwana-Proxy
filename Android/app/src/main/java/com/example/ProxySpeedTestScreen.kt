package com.example

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.widget.Toast
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.example.ui.theme.isAppDarkTheme
import kotlinx.coroutines.launch

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProxySpeedTestScreen(
    viewModel: ProxyViewModel,
    onBackClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val focusManager = LocalFocusManager.current
    val coroutineScope = rememberCoroutineScope()
    val savedLinks by viewModel.savedLinks.collectAsStateWithLifecycle()

    var inputText by remember { mutableStateOf("") }
    var isTesting by remember { mutableStateOf(false) }
    var testProgress by remember { mutableFloatStateOf(0f) }
    var speedTestResult by remember { mutableStateOf<SpeedTestResult?>(null) }
    var errorMessage by remember { mutableStateOf<String?>(null) }

    // Live parsed proxy from user input
    val liveParsedProxy = remember(inputText) {
        ProxySpeedTester.parseInput(inputText)
    }

    val startTesting: () -> Unit = {
        focusManager.clearFocus()
        errorMessage = null
        val proxyToTest = liveParsedProxy ?: ProxySpeedTester.parseInput(inputText)
        if (proxyToTest == null) {
            errorMessage = context.getString(R.string.invalid_proxy_format)
        } else {
            isTesting = true
            testProgress = 0.05f
            speedTestResult = null
            coroutineScope.launch {
                try {
                    val result = ProxySpeedTester.runSpeedTest(
                        proxy = proxyToTest,
                        targetDurationMs = 7000L,
                        onProgress = { progress, _ ->
                            testProgress = progress
                        }
                    )
                    speedTestResult = result
                } catch (e: Exception) {
                    errorMessage = e.localizedMessage ?: context.getString(R.string.quality_offline)
                } finally {
                    isTesting = false
                }
            }
        }
    }

    Scaffold(
        topBar = {
            CenterAlignedTopAppBar(
                title = {
                    Text(
                        text = stringResource(R.string.proxy_speed_test),
                        fontWeight = FontWeight.Bold,
                        style = MaterialTheme.typography.titleLarge
                    )
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
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // Input Card
            Card(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(20.dp),
                colors = CardDefaults.cardColors(
                    containerColor = if (isAppDarkTheme()) {
                        MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f)
                    } else {
                        Color(0xFFF2F3F8)
                    }
                )
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    OutlinedTextField(
                        value = inputText,
                        onValueChange = {
                            inputText = it
                            errorMessage = null
                        },
                        modifier = Modifier.fillMaxWidth(),
                        placeholder = {
                            Text(
                                text = stringResource(R.string.proxy_input_hint),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
                            )
                        },
                        leadingIcon = {
                            Icon(
                                imageVector = Icons.Default.Link,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.primary
                            )
                        },
                        trailingIcon = {
                            if (inputText.isNotEmpty()) {
                                IconButton(onClick = {
                                    inputText = ""
                                    speedTestResult = null
                                    errorMessage = null
                                }) {
                                    Icon(
                                        imageVector = Icons.Default.Clear,
                                        contentDescription = "Clear"
                                    )
                                }
                            }
                        },
                        shape = RoundedCornerShape(14.dp),
                        singleLine = false,
                        maxLines = 3,
                        keyboardOptions = KeyboardOptions(
                            keyboardType = KeyboardType.Uri,
                            imeAction = ImeAction.Done
                        ),
                        keyboardActions = KeyboardActions(
                            onDone = { startTesting() }
                        )
                    )

                    // Quick Paste and Clear Actions
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        OutlinedButton(
                            onClick = {
                                val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
                                val clipText = clipboard?.primaryClip?.getItemAt(0)?.text?.toString()
                                if (!clipText.isNullOrBlank()) {
                                    inputText = clipText.trim()
                                    errorMessage = null
                                } else {
                                    Toast.makeText(context, "کلیپ‌بورد خالی است", Toast.LENGTH_SHORT).show()
                                }
                            },
                            shape = RoundedCornerShape(12.dp),
                            modifier = Modifier.weight(1f)
                        ) {
                            Icon(
                                imageVector = Icons.Default.ContentPaste,
                                contentDescription = null,
                                modifier = Modifier.size(18.dp)
                            )
                            Spacer(modifier = Modifier.width(6.dp))
                            Text(
                                text = stringResource(R.string.paste_clipboard),
                                fontWeight = FontWeight.Bold
                            )
                        }

                        Button(
                            onClick = startTesting,
                            enabled = !isTesting && inputText.isNotBlank(),
                            shape = RoundedCornerShape(12.dp),
                            modifier = Modifier.weight(1.4f)
                        ) {
                            if (isTesting) {
                                CircularProgressIndicator(
                                    modifier = Modifier.size(18.dp),
                                    strokeWidth = 2.dp,
                                    color = MaterialTheme.colorScheme.onPrimary
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(
                                    text = stringResource(R.string.testing),
                                    fontWeight = FontWeight.Bold,
                                    fontSize = 13.sp
                                )
                            } else {
                                Icon(
                                    imageVector = Icons.Default.PlayArrow,
                                    contentDescription = null,
                                    modifier = Modifier.size(18.dp)
                                )
                                Spacer(modifier = Modifier.width(6.dp))
                                Text(
                                    text = stringResource(R.string.start_test),
                                    fontWeight = FontWeight.Bold
                                )
                            }
                        }
                    }

                    // Error Message
                    if (errorMessage != null) {
                        Text(
                            text = errorMessage!!,
                            color = MaterialTheme.colorScheme.error,
                            style = MaterialTheme.typography.bodySmall,
                            fontWeight = FontWeight.Medium
                        )
                    }
                }
            }

            // Testing Progress Bar
            if (isTesting) {
                LinearProgressIndicator(
                    progress = { testProgress },
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(6.dp)
                        .clip(CircleShape),
                    color = MaterialTheme.colorScheme.primary,
                    trackColor = MaterialTheme.colorScheme.surfaceVariant
                )
            }

            // Speed Test Result Display
            AnimatedVisibility(
                visible = speedTestResult != null,
                enter = fadeIn() + slideInVertically(initialOffsetY = { 40 }),
                exit = fadeOut()
            ) {
                speedTestResult?.let { result ->
                    ResultCard(
                        result = result,
                        isSaved = savedLinks.contains(result.parsedProxy.originalLink),
                        onToggleSave = { viewModel.toggleSave(result.parsedProxy.originalLink) },
                        onConnect = {
                            TelegramLauncher.launchProxy(context, result.parsedProxy.originalLink)
                        },
                        onCopy = {
                            val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
                            val clip = ClipData.newPlainText("Proxy", result.parsedProxy.originalLink)
                            clipboard?.setPrimaryClip(clip)
                            Toast.makeText(context, context.getString(R.string.copied), Toast.LENGTH_SHORT).show()
                        }
                    )
                }
            }
        }
    }
}

@Composable
private fun ResultCard(
    result: SpeedTestResult,
    isSaved: Boolean,
    onToggleSave: () -> Unit,
    onConnect: () -> Unit,
    onCopy: () -> Unit
) {
    val qualityColor = when (result.quality) {
        ConnectionQuality.EXCELLENT -> Color(0xFF2E7D32) // Emerald Green
        ConnectionQuality.GOOD -> Color(0xFF388E3C)      // Green
        ConnectionQuality.FAIR -> Color(0xFFF57C00)      // Amber Orange
        ConnectionQuality.POOR -> Color(0xFFE65100)      // Deep Orange
        ConnectionQuality.OFFLINE -> Color(0xFFD32F2F)   // Red
    }

    val qualityTitle = when (result.quality) {
        ConnectionQuality.EXCELLENT -> stringResource(R.string.quality_excellent)
        ConnectionQuality.GOOD -> stringResource(R.string.quality_good)
        ConnectionQuality.FAIR -> stringResource(R.string.quality_fair)
        ConnectionQuality.POOR -> stringResource(R.string.quality_poor)
        ConnectionQuality.OFFLINE -> stringResource(R.string.quality_offline)
    }

    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        colors = CardDefaults.cardColors(
            containerColor = if (isAppDarkTheme()) {
                MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.45f)
            } else {
                Color.White
            }
        ),
        elevation = CardDefaults.cardElevation(defaultElevation = 3.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(18.dp)
        ) {
            // Speedometer / Gauge Header
            Column(
                modifier = Modifier.fillMaxWidth(),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                // Latency Value Gauge Box
                Box(
                    contentAlignment = Alignment.Center,
                    modifier = Modifier
                        .size(130.dp)
                        .background(
                            color = qualityColor.copy(alpha = 0.12f),
                            shape = CircleShape
                        )
                        .border(
                            width = 4.dp,
                            color = qualityColor.copy(alpha = 0.6f),
                            shape = CircleShape
                        )
                ) {
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.Center
                    ) {
                        if (result.isSuccess) {
                            Text(
                                text = "${result.avgPing}",
                                fontSize = 34.sp,
                                fontWeight = FontWeight.Black,
                                color = qualityColor
                            )
                            Text(
                                text = "ms",
                                fontSize = 14.sp,
                                fontWeight = FontWeight.Bold,
                                color = qualityColor.copy(alpha = 0.8f)
                            )
                        } else {
                            Icon(
                                imageVector = Icons.Default.CloudOff,
                                contentDescription = null,
                                tint = qualityColor,
                                modifier = Modifier.size(36.dp)
                            )
                            Spacer(modifier = Modifier.height(4.dp))
                            Text(
                                text = "OFFLINE",
                                fontSize = 12.sp,
                                fontWeight = FontWeight.Bold,
                                color = qualityColor
                            )
                        }
                    }
                }

                // Quality Badge
                Surface(
                    color = qualityColor.copy(alpha = 0.15f),
                    shape = RoundedCornerShape(12.dp)
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 14.dp, vertical = 6.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .size(8.dp)
                                .background(qualityColor, CircleShape)
                        )
                        Text(
                            text = "${stringResource(R.string.connection_quality)}: $qualityTitle",
                            style = MaterialTheme.typography.bodyMedium,
                            fontWeight = FontWeight.Bold,
                            color = qualityColor
                        )
                    }
                }
            }

            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))

            // Metrics Grid (6 items)
            // Row 1: Avg Ping & Stability
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                MetricItem(
                    title = stringResource(R.string.avg_ping),
                    value = if (result.isSuccess) "${result.avgPing} ms" else "--",
                    icon = Icons.Default.NetworkCheck,
                    modifier = Modifier.weight(1f)
                )
                MetricItem(
                    title = stringResource(R.string.stability_label),
                    value = if (result.isSuccess) "${result.stabilityPercent}%" else "0%",
                    icon = Icons.Default.CheckCircle,
                    modifier = Modifier.weight(1f)
                )
            }

            // Row 2: Download Speed & Upload Speed
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                MetricItem(
                    title = stringResource(R.string.download_speed),
                    value = if (result.isSuccess) "${result.downloadSpeedMbps} Mbps" else "--",
                    icon = Icons.Default.ArrowDownward,
                    modifier = Modifier.weight(1f)
                )
                MetricItem(
                    title = stringResource(R.string.upload_speed),
                    value = if (result.isSuccess) "${result.uploadSpeedMbps} Mbps" else "--",
                    icon = Icons.Default.ArrowUpward,
                    modifier = Modifier.weight(1f)
                )
            }

            // Row 3: Jitter & Packet Loss
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                MetricItem(
                    title = stringResource(R.string.jitter_label),
                    value = if (result.isSuccess) "±${result.jitter} ms" else "--",
                    icon = Icons.Default.ShowChart,
                    modifier = Modifier.weight(1f)
                )
                MetricItem(
                    title = stringResource(R.string.packet_loss_label),
                    value = "${result.packetLossPercent}%",
                    icon = Icons.Default.DataUsage,
                    modifier = Modifier.weight(1f)
                )
            }

            // Real Telegram Download Speed & Calculator Card
            if (result.isSuccess) {
                TelegramSpeedEstimatorCard(rawSpeedMbps = result.downloadSpeedMbps)
            }

            // Server & IP details card
            Surface(
                shape = RoundedCornerShape(14.dp),
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
                modifier = Modifier.fillMaxWidth()
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(12.dp),
                    verticalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    Text(
                        text = "${result.parsedProxy.server}:${result.parsedProxy.port}",
                        style = MaterialTheme.typography.bodySmall,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    if (!result.ipAddress.isNullOrBlank() && result.ipAddress != result.parsedProxy.server) {
                        Text(
                            text = "IP: ${result.ipAddress}",
                            style = MaterialTheme.typography.labelSmall,
                            fontFamily = FontFamily.Monospace,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    if (result.dnsLookupMs >= 0) {
                        Text(
                            text = "DNS Lookup: ${result.dnsLookupMs} ms",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
                        )
                    }
                }
            }

            // Actions Row
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Button(
                    onClick = onConnect,
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(14.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.primary
                    )
                ) {
                    Icon(
                        imageVector = Icons.AutoMirrored.Filled.Send,
                        contentDescription = null,
                        modifier = Modifier.size(18.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = stringResource(R.string.connect),
                        fontWeight = FontWeight.Bold
                    )
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedButton(
                        onClick = onToggleSave,
                        shape = RoundedCornerShape(12.dp),
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(
                            imageVector = if (isSaved) Icons.Default.Bookmark else Icons.Default.BookmarkBorder,
                            contentDescription = null,
                            tint = if (isSaved) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface,
                            modifier = Modifier.size(18.dp)
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(
                            text = stringResource(R.string.save),
                            fontWeight = FontWeight.Bold
                        )
                    }

                    OutlinedButton(
                        onClick = onCopy,
                        shape = RoundedCornerShape(12.dp),
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(
                            imageVector = Icons.Default.ContentCopy,
                            contentDescription = null,
                            modifier = Modifier.size(18.dp)
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(
                            text = stringResource(R.string.copy),
                            fontWeight = FontWeight.Bold
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun MetricItem(
    title: String,
    value: String,
    icon: ImageVector,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(14.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(20.dp)
            )
            Column {
                Text(
                    text = title,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Text(
                    text = value,
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onSurface
                )
            }
        }
    }
}

@Composable
private fun TelegramSpeedEstimatorCard(
    rawSpeedMbps: Float,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    var fileSizeInput by remember { mutableStateOf("") }

    val cleanRawSpeed = if (rawSpeedMbps.isNaN() || rawSpeedMbps.isInfinite() || rawSpeedMbps < 0f) 0f else rawSpeedMbps
    // Formula: estimatedTelegramSpeedMBps = rawSpeedMbps * 0.035
    val estimatedTelegramSpeedMBps = (cleanRawSpeed * 0.035).coerceAtLeast(0.0)
    val formattedSpeed = String.format(java.util.Locale.US, "%.2f", estimatedTelegramSpeedMBps)

    val cleanInput = normalizeDecimalInput(fileSizeInput)
    val parsedFileSize = cleanInput.toDoubleOrNull()

    val estimatedDownloadTime = remember(parsedFileSize, estimatedTelegramSpeedMBps) {
        if (parsedFileSize != null && parsedFileSize > 0.0 && estimatedTelegramSpeedMBps > 0.0001) {
            // downloadTimeSeconds = fileSizeMB / estimatedTelegramSpeedMBps
            val totalSeconds = Math.round(parsedFileSize / estimatedTelegramSpeedMBps)
            formatEstimatedDownloadTime(context, totalSeconds)
        } else {
            null
        }
    }

    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.35f),
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.primary.copy(alpha = 0.25f)
        )
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Header Row with Title and "Estimated" Badge
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    modifier = Modifier.weight(1f)
                ) {
                    Icon(
                        imageVector = Icons.Default.CloudDownload,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(22.dp)
                    )
                    Text(
                        text = stringResource(R.string.real_telegram_download_speed),
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                }

                Surface(
                    shape = RoundedCornerShape(8.dp),
                    color = MaterialTheme.colorScheme.primary.copy(alpha = 0.15f)
                ) {
                    Text(
                        text = stringResource(R.string.estimated_badge),
                        style = MaterialTheme.typography.labelSmall,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp)
                    )
                }
            }

            // Big Speed Display
            Row(
                verticalAlignment = Alignment.Bottom,
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Text(
                    text = formattedSpeed,
                    fontSize = 30.sp,
                    fontWeight = FontWeight.Black,
                    fontFamily = FontFamily.Monospace,
                    color = MaterialTheme.colorScheme.primary
                )
                Text(
                    text = "MB/s",
                    fontSize = 16.sp,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.primary.copy(alpha = 0.85f),
                    modifier = Modifier.padding(bottom = 4.dp)
                )
            }

            // Disclaimer
            Text(
                text = stringResource(R.string.telegram_speed_disclaimer),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.85f),
                fontSize = 11.5.sp,
                lineHeight = 16.sp
            )

            HorizontalDivider(
                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.15f),
                thickness = 1.dp
            )

            // File Size Input & Download Time Estimator
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = stringResource(R.string.file_download_estimator),
                    style = MaterialTheme.typography.labelMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onSurface
                )

                OutlinedTextField(
                    value = fileSizeInput,
                    onValueChange = { input ->
                        // Allow numbers, decimal separators in Latin, Persian and Arabic
                        if (input.isEmpty() || input.matches(Regex("^[0-9۰-۹٠-٩]*[.,٫،]?[0-9۰-۹٠-٩]*$"))) {
                            fileSizeInput = input
                        }
                    },
                    placeholder = {
                        Text(
                            text = stringResource(R.string.file_size_mb_hint),
                            style = MaterialTheme.typography.bodySmall
                        )
                    },
                    trailingIcon = {
                        if (fileSizeInput.isNotEmpty()) {
                            IconButton(onClick = { fileSizeInput = "" }) {
                                Icon(
                                    imageVector = Icons.Default.Clear,
                                    contentDescription = "Clear",
                                    modifier = Modifier.size(18.dp)
                                )
                            }
                        }
                    },
                    keyboardOptions = KeyboardOptions(
                        keyboardType = KeyboardType.Decimal,
                        imeAction = ImeAction.Done
                    ),
                    singleLine = true,
                    shape = RoundedCornerShape(12.dp),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedContainerColor = MaterialTheme.colorScheme.surface,
                        unfocusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.7f),
                        focusedBorderColor = MaterialTheme.colorScheme.primary,
                        unfocusedBorderColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.3f)
                    ),
                    modifier = Modifier.fillMaxWidth()
                )

                // Quick presets (10 MB, 50 MB, 100 MB, 500 MB, 1000 MB)
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    listOf("10", "50", "100", "500", "1000").forEach { preset ->
                        val isSelected = normalizeDecimalInput(fileSizeInput) == preset
                        Surface(
                            shape = RoundedCornerShape(8.dp),
                            color = if (isSelected) {
                                MaterialTheme.colorScheme.primary
                            } else {
                                MaterialTheme.colorScheme.surface
                            },
                            border = androidx.compose.foundation.BorderStroke(
                                1.dp,
                                if (isSelected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outline.copy(alpha = 0.2f)
                            ),
                            modifier = Modifier
                                .weight(1f)
                                .clickable {
                                    fileSizeInput = if (isSelected) "" else preset
                                }
                        ) {
                            Text(
                                text = "$preset M",
                                style = MaterialTheme.typography.labelSmall,
                                fontWeight = FontWeight.Bold,
                                color = if (isSelected) {
                                    MaterialTheme.colorScheme.onPrimary
                                } else {
                                    MaterialTheme.colorScheme.onSurfaceVariant
                                },
                                textAlign = TextAlign.Center,
                                maxLines = 1,
                                modifier = Modifier.padding(vertical = 6.dp)
                            )
                        }
                    }
                }

                // Estimated Result Banner
                AnimatedVisibility(
                    visible = estimatedDownloadTime != null,
                    enter = fadeIn() + slideInVertically(),
                    exit = fadeOut()
                ) {
                    if (estimatedDownloadTime != null) {
                        Surface(
                            shape = RoundedCornerShape(12.dp),
                            color = MaterialTheme.colorScheme.primary.copy(alpha = 0.18f),
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 12.dp, vertical = 10.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(10.dp)
                            ) {
                                Icon(
                                    imageVector = Icons.Default.Timer,
                                    contentDescription = null,
                                    tint = MaterialTheme.colorScheme.primary,
                                    modifier = Modifier.size(22.dp)
                                )
                                Column {
                                    Text(
                                        text = stringResource(R.string.estimated_time_result),
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                    Text(
                                        text = "≈ $estimatedDownloadTime",
                                        style = MaterialTheme.typography.bodyMedium,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colorScheme.primary
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

/**
 * Normalizes Eastern Arabic / Persian digits and decimal points to standard ASCII decimal.
 */
private fun normalizeDecimalInput(input: String): String {
    return input.trim()
        .replace('۰', '0').replace('۱', '1').replace('۲', '2')
        .replace('۳', '3').replace('۴', '4').replace('۵', '5')
        .replace('۶', '6').replace('۷', '7').replace('۸', '8')
        .replace('۹', '9')
        .replace('٠', '0').replace('١', '1').replace('٢', '2')
        .replace('٣', '3').replace('٤', '4').replace('٥', '5')
        .replace('٦', '6').replace('٧', '7').replace('٨', '8')
        .replace('٩', '9')
        .replace('٫', '.')
        .replace('،', '.')
        .replace(',', '.')
}

/**
 * Formats seconds into human-readable minutes and seconds based on active language.
 */
private fun formatEstimatedDownloadTime(context: Context, totalSeconds: Long): String {
    if (totalSeconds <= 0) return "0"
    val days = totalSeconds / 86400
    val hours = (totalSeconds % 86400) / 3600
    val minutes = (totalSeconds % 3600) / 60
    val seconds = totalSeconds % 60

    val lang = try {
        context.resources.configuration.locales[0].language
    } catch (e: Exception) {
        context.resources.configuration.locale.language
    }
    return when (lang) {
        "fa" -> {
            when {
                days > 0 -> "$days روز و $hours ساعت"
                hours > 0 -> {
                    if (seconds > 0) "$hours ساعت و $minutes دقیقه و $seconds ثانیه"
                    else "$hours ساعت و $minutes دقیقه"
                }
                minutes > 0 -> {
                    if (seconds > 0) "$minutes دقیقه و $seconds ثانیه"
                    else "$minutes دقیقه"
                }
                else -> "$seconds ثانیه"
            }
        }
        "ru" -> {
            when {
                days > 0 -> "$days дн $hours ч"
                hours > 0 -> {
                    if (seconds > 0) "$hours ч $minutes мин $seconds сек"
                    else "$hours ч $minutes мин"
                }
                minutes > 0 -> {
                    if (seconds > 0) "$minutes мин $seconds сек"
                    else "$minutes мин"
                }
                else -> "$seconds сек"
            }
        }
        else -> {
            when {
                days > 0 -> "$days d $hours h"
                hours > 0 -> {
                    if (seconds > 0) "$hours hr $minutes min $seconds sec"
                    else "$hours hr $minutes min"
                }
                minutes > 0 -> {
                    if (seconds > 0) "$minutes min $seconds sec"
                    else "$minutes min"
                }
                else -> "$seconds sec"
            }
        }
    }
}

